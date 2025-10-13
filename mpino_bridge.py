#!/usr/bin/env python3
"""
mpino_pi_strict_bridge.py

Behavior:
- Serial -> MQTT:
  Expect serial lines as JSON like:
    {"cmd":"current","dev":"led","val":true|false}
    {"cmd":"switch","dev":"fan","val":true|false}
  These are published to:
    current/<dev>  (payload: {"pattern":"current/<dev>","data":{"name":"<dev>","value":val}})
    switch/<dev>   (payload: {"pattern":"switch/<dev>","data":{"name":"<dev>","value":val}})

Usage:
  pip3 install paho-mqtt pyserial
  python3 mpino_pi_strict_bridge.py /dev/ttyUSB0 115200 localhost 1883
"""

import sys, time, json, threading, queue, signal, logging, atexit
import paho.mqtt.client as mqtt
import serial
import requests
from config import MQTTConfig, SerialConfig, LoggingConfig, BackendConfig

logging.basicConfig(level=getattr(logging, LoggingConfig.LEVEL), format=LoggingConfig.FORMAT)

# 마지막 상태를 저장하여 변경사항만 출력
last_states = {}

# Shutdown 플래그
shutdown_flag = False

# Config에서 설정값 가져오기
SERIAL_DEV = SerialConfig.DEVICE
BAUD = SerialConfig.BAUD_RATE
MQTT_HOST = MQTTConfig.HOST
MQTT_PORT = MQTTConfig.PORT

logging.info("Configuration: SERIAL_DEV=%s, BAUD=%d, MQTT_HOST=%s, MQTT_PORT=%d", SERIAL_DEV, BAUD, MQTT_HOST, MQTT_PORT)

MQTT_SWITCH_WILDCARD = MQTTConfig.SWITCH_WILDCARD
TOPIC_CURRENT_PREFIX = MQTTConfig.TOPIC_CURRENT_PREFIX
TOPIC_SWITCH_PREFIX = MQTTConfig.TOPIC_SWITCH_PREFIX
TOPIC_DEVICE_UPDATE = MQTTConfig.TOPIC_DEVICE_UPDATE
TOPIC_RAW = "mpino/raw"
STATUS_TOPIC = f"{MQTTConfig.TOPIC_STATUS_PREFIX}/mpino_bridge_strict"

# queue for outgoing serial lines
ser_tx_q = queue.Queue(maxsize=200)

# Serial 객체를 전역으로 관리 (device update 핸들러에서 접근하기 위함)
serial_port = None

# MQTT client
import uuid
client_id = f"mpino_pi_strict_bridge_{uuid.uuid4().hex[:8]}"
client = mqtt.Client(client_id)
client.will_set(STATUS_TOPIC, payload="offline", qos=0, retain=True)
logging.info("MQTT Client ID: %s", client_id)


def safe_json_loads(s):
    try:
        return json.loads(s)
    except Exception:
        return None

def on_mqtt_connect(c, userdata, flags, rc):
    logging.info("MQTT connected rc=%s", rc)
    c.publish(STATUS_TOPIC, "online", retain=True)
    c.subscribe(MQTT_SWITCH_WILDCARD)
    c.subscribe(TOPIC_DEVICE_UPDATE)
    logging.info("Subscribed to %s and %s", MQTT_SWITCH_WILDCARD, TOPIC_DEVICE_UPDATE)

def on_mqtt_message(c, userdata, msg):
    payload = msg.payload.decode('utf-8', errors='ignore').strip()
    logging.info("MQTT recv on %s: %s", msg.topic, payload)

    # device/update 토픽 처리
    if msg.topic == TOPIC_DEVICE_UPDATE:
        handle_device_update(payload)
        return

    j = safe_json_loads(payload)
    if not isinstance(j, dict):
        logging.warning("Ignored: payload is not JSON object")
        return

    # {"pattern":"switch/<dev>","data":{"name":"<dev>","value":true|false}} 형식만 처리
    pattern = j.get("pattern")
    data = j.get("data")

    if not (isinstance(pattern, str) and isinstance(data, dict)):
        logging.warning("Ignored: missing or invalid pattern/data")
        return

    if not pattern.startswith("switch/"):
        logging.warning("Ignored: pattern not switch/*")
        return

    name = data.get("name")
    val = data.get("value")
    if not isinstance(name, str) or not isinstance(val, bool):
        logging.warning("Ignored: data.name must be string and data.value must be boolean")
        return

    # MPINO로 switch 명령 전송
    out_obj = {"cmd":"switch", "dev": name, "val": val}
    line = json.dumps(out_obj) + "\n"
    try:
        ser_tx_q.put_nowait(line)
        logging.info("Enqueued to serial: %s", line.strip())
    except queue.Full:
        logging.error("Serial queue full - dropping command")

def serial_reader_loop(ser):
    global shutdown_flag
    buf = ""
    while not shutdown_flag:
        try:
            data = ser.read(ser.in_waiting or 1)
            if not data:
                time.sleep(0.01)
                continue
            try:
                s = data.decode('utf-8', errors='ignore')
            except Exception:
                s = ''.join(chr(b) for b in data)
            buf += s
            while '\n' in buf:
                line, buf = buf.split('\n', 1)
                line = line.strip()
                if not line:
                    continue
                logging.debug("Serial recv: %s", line)
                process_serial_line(line)
        except Exception as e:
            if shutdown_flag:
                break
            logging.exception("Serial reader error: %s", e)
            time.sleep(1)

def process_serial_line(line):
    global last_states

    j = safe_json_loads(line)
    if not isinstance(j, dict):
        # ignore non-json lines (or publish to raw if you want)
        logging.warning("Ignored non-JSON serial line")
        return

    cmd = j.get("cmd")
    dev = j.get("dev")
    val = j.get("val")

    if cmd == "current" and isinstance(dev, str):
        # 전류값은 변경 여부 상관없이 바로 발행
        state_key = f"current/{dev}"
        last_states[state_key] = val
        # MPINO 형식에 맞춰 발행: {"pattern":"current/dev","data":{"name":"dev","value":val}}
        payload = {
            "pattern": f"current/{dev}",
            "data": {"name": dev, "value": val}
        }
        topic = f"{TOPIC_CURRENT_PREFIX}/{dev}"
        client.publish(topic, json.dumps(payload))
        logging.info("Published %s -> %s", topic, payload)
        return

    if cmd == "switch" and isinstance(dev, str):
        # 상태 변경된 경우만 출력
        state_key = f"switch/{dev}"
        if last_states.get(state_key) != val:
            last_states[state_key] = val
            logging.info("Switch state from MPINO: %s = %s", dev, val)
        else:
            logging.debug("No change for switch %s (still %s)", dev, val)
        return

    logging.warning("Ignored serial JSON with unknown/unsupported cmd: %s", cmd)

def serial_writer_loop(ser):
    global shutdown_flag
    while not shutdown_flag:
        try:
            line = ser_tx_q.get(timeout=0.5)
            if line is None:
                continue
            logging.info("Serial send: %s", line.strip())
            ser.write(line.encode('utf-8'))
            ser.flush()
        except queue.Empty:
            continue
        except Exception as e:
            if shutdown_flag:
                break
            logging.exception("Serial write error: %s", e)
            time.sleep(1)

def get_auth_token():
    """백엔드에서 인증 토큰을 가져옴"""
    try:
        signin_url = f"{BackendConfig.BASE_URL}/authentication/signin"
        auth_data = {
            'username': BackendConfig.USERNAME,
            'password': BackendConfig.PASSWORD
        }
        
        logging.info("Getting auth token from: %s", signin_url)
        response = requests.post(signin_url, json=auth_data, timeout=10)
        response.raise_for_status()
        
        token_data = response.json()
        access_token = token_data.get('accessToken')
        
        if not access_token:
            logging.error("No access token in response")
            return None
            
        logging.info("Successfully obtained auth token")
        return access_token
        
    except Exception as e:
        logging.exception("Error getting auth token: %s", e)
        return None

def fetch_devices_from_backend():
    """백엔드에서 장비 목록 및 핀 설정을 가져옴 (machine만)"""
    try:
        # 인증 토큰 가져오기
        token = get_auth_token()
        if not token:
            logging.error("Failed to get auth token")
            return []
        
        # 인증 헤더 설정
        headers = {'Authorization': f'Bearer {token}'}
        
        devices_url = f"{BackendConfig.BASE_URL}{BackendConfig.DEVICES_ENDPOINT}"
        logging.info("Fetching devices from backend: %s", devices_url)
        response = requests.get(devices_url, headers=headers, timeout=10)

        if response.status_code == 200:
            all_devices = response.json()
            # type이 'machine'인 장비만 필터링
            devices_data = [d for d in all_devices if d.get('type') == 'machine']
            logging.info("Fetched %d machine devices from backend (total: %d)", len(devices_data), len(all_devices))
            return devices_data
        else:
            logging.error("Failed to fetch devices: HTTP %d - %s", response.status_code, response.text)
            return []
    except Exception as e:
        logging.exception("Error fetching devices from backend: %s", e)
        return []

def handle_device_update(payload):
    """device/update MQTT 메시지 처리"""
    global serial_port

    logging.info("Device update notification received")

    j = safe_json_loads(payload)
    if not isinstance(j, dict):
        logging.warning("Invalid device update payload: not JSON object")
        return

    # 백엔드에서 최신 장비 목록 가져오기 (인증 포함)
    devices_data = fetch_devices_from_backend()

    if not devices_data:
        logging.warning("No machine devices found after update")
        return

    # MPINO에 config 전송
    if serial_port:
        send_config_to_mpino(serial_port, devices_data)
    else:
        logging.error("Serial port not available for device update")

def send_config_to_mpino(ser, devices_data):
    """MPINO에 config 명령을 전송하여 장비 설정"""
    if not devices_data:
        logging.warning("No devices to configure")
        return False

    # 백엔드 데이터를 MPINO config 형식으로 변환
    mpino_devices = []
    for device in devices_data:
        # 필수 필드 확인
        if not all(key in device for key in ['name', 'relay_pin', 'current_pin']):
            logging.warning("Device missing required fields: %s", device)
            continue

        mpino_devices.append({
            "name": device['name'],
            "relay": device['relay_pin'],
            "current": device['current_pin']
        })

    if not mpino_devices:
        logging.error("No valid devices to configure")
        return False

    # config 명령 생성
    config_cmd = {
        "cmd": "config",
        "devices": mpino_devices
    }

    config_line = json.dumps(config_cmd) + "\n"

    # MPINO에 전송
    try:
        logging.info("Sending config to MPINO: %s", config_line.strip())
        ser.write(config_line.encode('utf-8'))
        ser.flush()
        time.sleep(1)  # MPINO가 설정을 처리할 시간 대기
        logging.info("Config sent successfully")
        return True
    except Exception as e:
        logging.exception("Failed to send config to MPINO: %s", e)
        return False

def main():
    global serial_port

    client.on_connect = on_mqtt_connect
    client.on_message = on_mqtt_message
    try:
        client.connect(MQTT_HOST, MQTT_PORT, 60)
    except Exception:
        logging.exception("MQTT connect failed; will retry via loop_start")
    client.loop_start()

    # open serial
    ser = None
    while True:
        try:
            ser = serial.Serial(
                SERIAL_DEV,
                BAUD,
                timeout=SerialConfig.TIMEOUT,
                write_timeout=SerialConfig.WRITE_TIMEOUT,
                dsrdtr=SerialConfig.DSRDTR,
                rtscts=SerialConfig.RTSCTS
            )
            logging.info("Opened serial %s @ %d", SERIAL_DEV, BAUD)
            break
        except Exception as e:
            logging.exception("Failed to open serial %s: %s", SERIAL_DEV, e)
            time.sleep(2)

    # 전역 serial_port 설정 (device update 핸들러에서 사용)
    serial_port = ser

    # 백엔드에서 장비 정보 가져오기 및 MPINO 설정 (인증 포함)
    devices_data = fetch_devices_from_backend()
    if devices_data:
        send_config_to_mpino(ser, devices_data)
    else:
        logging.warning("Starting without device configuration")

    # threads
    rdr = threading.Thread(target=serial_reader_loop, args=(ser,), daemon=True)
    wtr = threading.Thread(target=serial_writer_loop, args=(ser,), daemon=True)
    rdr.start(); wtr.start()

    def shutdown(signum=None, frame=None):
        global shutdown_flag
        if shutdown_flag:
            return  # 이미 shutdown 중이면 중복 실행 방지
        shutdown_flag = True

        logging.info("Shutting down")

        # Backend에 에러 리포트 전송 (인증 포함)
        try:
            # 인증 토큰 가져오기
            token = get_auth_token()
            if token:
                headers = {'Authorization': f'Bearer {token}'}
                report_url = f"{BackendConfig.BASE_URL}{BackendConfig.REPORT_ENDPOINT}"
                report_data = {
                    "level": 2,  # 경고 레벨
                    "problem": "MPINO Bridge 종료 - 릴레이 자동 차단됨"
                }
                response = requests.post(report_url, json=report_data, headers=headers, timeout=2)
                if response.status_code == 201:
                    logging.info("Error report sent to backend successfully")
                else:
                    logging.warning("Failed to send error report: HTTP %d", response.status_code)
            else:
                logging.warning("Could not get auth token for error report")
        except Exception as e:
            logging.error("Could not send error report to backend: %s", e)

        # 모든 switch 상태를 false로 전송하여 릴레이 끄기
        for state_key in last_states.keys():
            if state_key.startswith("switch/"):
                dev = state_key.replace("switch/", "")
                payload = {
                    "pattern": f"switch/{dev}",
                    "data": {"name": dev, "value": False}
                }
                topic = f"{TOPIC_SWITCH_PREFIX}/{dev}"
                try:
                    client.publish(topic, json.dumps(payload))
                    logging.info("Shutdown: Published %s -> %s", topic, payload)
                except:
                    pass

        # 모든 current 상태를 false로 전송
        for state_key in last_states.keys():
            if state_key.startswith("current/"):
                dev = state_key.replace("current/", "")
                payload = {
                    "pattern": f"current/{dev}",
                    "data": {"name": dev, "value": False}
                }
                topic = f"{TOPIC_CURRENT_PREFIX}/{dev}"
                try:
                    client.publish(topic, json.dumps(payload))
                    logging.info("Shutdown: Published %s -> %s", topic, payload)
                except:
                    pass

        time.sleep(0.5)  # MQTT 메시지 전송 대기

        try:
            client.publish(STATUS_TOPIC, "offline", retain=True)
        except:
            pass
        client.loop_stop()

        # 스레드 종료 대기
        time.sleep(0.5)

        try:
            ser.close()
        except:
            pass
        logging.info("Shutdown complete")
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)
    atexit.register(shutdown)  # 정상 종료 시에도 실행

    while True:
        time.sleep(1)

if __name__ == "__main__":
    main()
