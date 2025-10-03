#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import time
import json
import board
import adafruit_dht
import paho.mqtt.client as mqtt
from typing import List, Tuple, Optional
from AtlasI2C import AtlasI2C
from config import MQTTConfig, SensorConfig


class Config:
    """Application configuration (deprecated - use config.py)"""
    BROKER_ADDRESS = MQTTConfig.HOST
    PORT = MQTTConfig.PORT
    DHT_PIN = SensorConfig.DHT_PIN
    DEFAULT_POLL_INTERVAL = SensorConfig.DEFAULT_POLL_INTERVAL
    SENSOR_NAME_MAPPING = SensorConfig.SENSOR_NAME_MAPPING


class MQTTPublisher:
    """MQTT client wrapper for publishing sensor data"""

    def __init__(self, broker_address: str, port: int):
        self.client = mqtt.Client()
        self.client.on_connect = self._on_connect
        self.broker_address = broker_address
        self.port = port

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print("✅ MQTT 브로커에 성공적으로 연결되었습니다.")
        else:
            print(f"❌ MQTT 연결 실패, 리턴 코드: {rc}")

    def connect(self):
        """Connect to MQTT broker"""
        try:
            self.client.connect(self.broker_address, self.port, 60)
            self.client.loop_start()
        except Exception as e:
            print(f"❌ MQTT 브로커에 연결할 수 없습니다: {e}")
            raise

    def disconnect(self):
        """Disconnect from MQTT broker"""
        self.client.loop_stop()
        self.client.disconnect()
        print("MQTT connection closed.")

    def publish(self, sensor_name: str, value):
        """Publish sensor data to MQTT broker"""
        topic = f"{MQTTConfig.TOPIC_ENVIRONMENT_PREFIX}/{sensor_name.lower()}"
        payload = {
            "pattern": topic,
            "data": {
                "name": sensor_name.lower(),
                "value": value
            }
        }
        result = self.client.publish(topic, json.dumps(payload))
        if result[0] != 0:
            print(f"⚠️ MQTT 메시지 발행 실패: {result}")


class AtlasSensorManager:
    """Manager for Atlas Scientific I2C sensors"""

    def __init__(self):
        self.devices = self._discover_devices()

    def _discover_devices(self) -> List[AtlasI2C]:
        """Discover and initialize all Atlas I2C devices"""
        device = AtlasI2C()
        device_address_list = device.list_i2c_devices()
        device_list = []

        for address in device_address_list:
            device.set_i2c_address(address)
            response = device.query("I")
            try:
                moduletype = response.split(",")[1]
                name = device.query("name,?").split(",")[1]
                device_list.append(AtlasI2C(address=address, moduletype=moduletype, name=name))
            except IndexError:
                continue

        return device_list

    def print_devices(self, current_device=None):
        """Print all discovered devices"""
        for device in self.devices:
            prefix = "--> " if device == current_device else " - "
            print(prefix + device.get_device_info())

    def read_all(self) -> dict:
        """Read values from all Atlas sensors"""
        results = {}

        for dev in self.devices:
            dev.write("R")

        time.sleep(AtlasI2C.LONG_TIMEOUT)

        for dev in self.devices:
            response_str = dev.read()
            print(response_str)

            try:
                value = response_str.split(':')[-1].strip().split('\x00')[0]
                sensor_name = self._get_sensor_name(dev.moduletype)
                results[sensor_name] = value
            except (ValueError, IndexError) as err:
                print(f"⚠️ Error reading {dev.moduletype}: {err}")

        return results

    @staticmethod
    def _get_sensor_name(moduletype: str) -> str:
        """Map module type to sensor name"""
        return Config.SENSOR_NAME_MAPPING.get(moduletype.upper(), moduletype)


class DHT22Sensor:
    """DHT22 temperature and humidity sensor wrapper"""

    def __init__(self, pin: int):
        self.sensor = None
        try:
            pin_object = getattr(board, f"D{pin}")
            self.sensor = adafruit_dht.DHT22(pin_object)
            print(f"✓ DHT22 sensor initialized on GPIO {pin}.")
        except Exception as e:
            print(f"✗ Failed to initialize DHT22 sensor: {e}")

    def read(self) -> Tuple[Optional[float], Optional[float]]:
        """Read temperature and humidity from DHT22 sensor"""
        if not self.sensor:
            return None, None

        try:
            temperature = round(self.sensor.temperature, 1)
            humidity = round(self.sensor.humidity, 1)
            return temperature, humidity
        except (RuntimeError, Exception):
            return None, None


class SensorMonitor:
    """Main application for monitoring and publishing sensor data"""

    def __init__(self):
        self.mqtt_publisher = MQTTPublisher(Config.BROKER_ADDRESS, Config.PORT)
        self.atlas_manager = AtlasSensorManager()
        self.dht_sensor = DHT22Sensor(Config.DHT_PIN)

    def start(self):
        """Start the sensor monitoring application"""
        print(">> Atlas Scientific I2C Sensor Monitor")

        self.mqtt_publisher.connect()

        if self.atlas_manager.devices:
            self.atlas_manager.print_devices(self.atlas_manager.devices[0])

        try:
            self._run_command_loop()
        except KeyboardInterrupt:
            print("\nProgram exiting.")
        finally:
            self.mqtt_publisher.disconnect()

    def _run_command_loop(self):
        """Main command loop"""
        while True:
            user_cmd = input(">> Enter command: ")

            if user_cmd.upper().strip().startswith("POLL"):
                self._handle_poll_command(user_cmd)
            elif user_cmd.upper().strip().startswith("LIST"):
                if self.atlas_manager.devices:
                    self.atlas_manager.print_devices(self.atlas_manager.devices[0])

    def _handle_poll_command(self, user_cmd: str):
        """Handle POLL command to continuously read sensors"""
        cmd_list = user_cmd.split(',')
        delay_time = float(cmd_list[1]) if len(cmd_list) > 1 else Config.DEFAULT_POLL_INTERVAL

        try:
            while True:
                print("\n------- Polling Sensors -------")

                # Read Atlas sensors
                atlas_data = self.atlas_manager.read_all()
                for sensor_name, value in atlas_data.items():
                    self.mqtt_publisher.publish(sensor_name, value)

                # Read DHT22 sensor
                temperature, humidity = self.dht_sensor.read()
                if temperature is not None and humidity is not None:
                    print(f"Temp: {temperature}")
                    print(f"Humid: {humidity}")
                    self.mqtt_publisher.publish("temperature", temperature)
                    self.mqtt_publisher.publish("humidity", humidity)
                else:
                    print("DHT22 Read Error.")

                time.sleep(delay_time)

        except KeyboardInterrupt:
            print("\nContinuous polling stopped")
            if self.atlas_manager.devices:
                self.atlas_manager.print_devices(self.atlas_manager.devices[0])


def main():
    monitor = SensorMonitor()
    monitor.start()


if __name__ == '__main__':
    main()
