#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PlantPoint Arduino Project Configuration
모든 Python 스크립트에서 사용하는 공통 설정을 관리합니다.
"""

import os


class MQTTConfig:
    """MQTT 브로커 설정"""
    HOST = os.getenv('MQTT_HOST', '172.30.1.38')
    PORT = int(os.getenv('MQTT_PORT', '1883'))
    KEEPALIVE = 60

    # Topic 설정
    TOPIC_CURRENT_PREFIX = "current"
    TOPIC_SWITCH_PREFIX = "switch"
    TOPIC_ENVIRONMENT_PREFIX = "environment"
    TOPIC_STATUS_PREFIX = "status"

    # Wildcard subscriptions
    SWITCH_WILDCARD = "switch/+"


class SerialConfig:
    """시리얼 통신 설정 (MPINO Bridge)"""
    DEVICE = os.getenv('SERIAL_DEV', '/dev/ttyUSB0')
    BAUD_RATE = int(os.getenv('BAUD', '115200'))
    TIMEOUT = 0
    WRITE_TIMEOUT = 2
    # Arduino 리셋 방지
    DSRDTR = False
    RTSCTS = False


class SensorConfig:
    """센서 설정 (Atlas Jet)"""
    DHT_PIN = int(os.getenv('DHT_PIN', '26'))
    DEFAULT_POLL_INTERVAL = float(os.getenv('POLL_INTERVAL', '5.0'))

    # Atlas Scientific 센서 타입 매핑
    SENSOR_NAME_MAPPING = {
        "RTD": "water_temperature",
        "PH": "ph",
        "EC": "ec"
    }


class LoggingConfig:
    """로깅 설정"""
    LEVEL = os.getenv('LOG_LEVEL', 'INFO')
    FORMAT = "%(asctime)s %(levelname)s: %(message)s"


# 하위 호환성을 위한 legacy config (deprecated)
class Config:
    """Legacy config class for atlas_jet.py"""
    BROKER_ADDRESS = MQTTConfig.HOST
    PORT = MQTTConfig.PORT
    DHT_PIN = SensorConfig.DHT_PIN
    DEFAULT_POLL_INTERVAL = SensorConfig.DEFAULT_POLL_INTERVAL
    SENSOR_NAME_MAPPING = SensorConfig.SENSOR_NAME_MAPPING
