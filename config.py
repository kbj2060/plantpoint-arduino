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
    TOPIC_DEVICE_UPDATE = "device/update"  # 장비 추가/수정/삭제 알림

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


class BackendConfig:
    """PlantPoint Backend API 설정"""
    BASE_URL = os.getenv('BACKEND_URL', 'http://172.30.1.38:3000/api')
    REPORT_ENDPOINT = '/reports/create'
    DEVICES_ENDPOINT = '/devices'  # 장비 목록 조회
