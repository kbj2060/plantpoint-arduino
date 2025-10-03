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

import sys, time, json, threading, queue, signal, logging
import paho.mqtt.client as mqtt
import serial

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")

# 마지막 상태를 저장하여 변경사항만 출력
last_states = {}

# 환경변수에서 설정값을 가져오고, 없으면 기본값 사용
SERIAL_DEV = '/dev/ttyUSB0'
BAUD = int('115200')
MQTT_HOST = '172.30.1.38'
MQTT_PORT = int('1883')

logging.info("Configuration: SERIAL_DEV=%s, BAUD=%d, MQTT_HOST=%s, MQTT_PORT=%d", SERIAL_DEV, BAUD, MQTT_HOST, MQTT_PORT)

MQTT_SWITCH_WILDCARD = "switch/+"
TOPIC_CURRENT_PREFIX = "current"
TOPIC_SWITCH_PREFIX = "switch"
TOPIC_RAW = "mpino/raw"
STATUS_TOPIC = "status/mpino_bridge_strict"

# queue for outgoing serial lines
ser_tx_q = queue.Queue(maxsize=200)

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
    logging.info("Subscribed to %s", MQTT_SWITCH_WILDCARD)

def on_mqtt_message(c, userdata, msg):
    payload = msg.payload.decode('utf-8', errors='ignore').strip()
    logging.info("MQTT recv on %s: %s", msg.topic, payload)

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
    buf = ""
    while True:
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
    while True:
        try:
            line = ser_tx_q.get()
            if line is None:
                continue
            logging.info("Serial send: %s", line.strip())
            ser.write(line.encode('utf-8'))
            ser.flush()
        except Exception as e:
            logging.exception("Serial write error: %s", e)
            time.sleep(1)

def main():
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
            ser = serial.Serial(SERIAL_DEV, BAUD, timeout=0, write_timeout=2)
            logging.info("Opened serial %s @ %d", SERIAL_DEV, BAUD)
            break
        except Exception as e:
            logging.exception("Failed to open serial %s: %s", SERIAL_DEV, e)
            time.sleep(2)

    # threads
    rdr = threading.Thread(target=serial_reader_loop, args=(ser,), daemon=True)
    wtr = threading.Thread(target=serial_writer_loop, args=(ser,), daemon=True)
    rdr.start(); wtr.start()

    def shutdown(signum=None, frame=None):
        logging.info("Shutting down")
        try:
            client.publish(STATUS_TOPIC, "offline", retain=True)
        except:
            pass
        client.loop_stop()
        try:
            ser.close()
        except:
            pass
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    while True:
        time.sleep(1)

if __name__ == "__main__":
    main()
