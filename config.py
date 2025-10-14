#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PlantPoint Arduino Project Configuration
모든 Python 스크립트에서 사용하는 공통 설정을 관리합니다.
"""

import os
import serial.tools.list_ports
import glob


def find_serial_port():
    """
    사용 가능한 시리얼 포트를 자동으로 찾아서 반환합니다.
    우선순위: USB 시리얼 포트 > ACM 포트 > 기타 포트
    """
    # 1. pyserial의 list_ports를 사용하여 USB 시리얼 포트 찾기
    ports = serial.tools.list_ports.comports()
    usb_ports = []
    
    for port in ports:
        # USB 시리얼 포트인지 확인
        if 'USB' in port.description.upper() or 'Serial' in port.description.upper():
            usb_ports.append(port.device)
    
    # USB 포트가 있으면 정렬해서 첫 번째 반환
    if usb_ports:
        usb_ports.sort()
        return usb_ports[0]
    
    # 2. glob을 사용하여 /dev/ttyUSB* 포트 찾기
    usb_devices = glob.glob('/dev/ttyUSB*')
    if usb_devices:
        usb_devices.sort()
        return usb_devices[0]
    
    # 3. /dev/ttyACM* 포트 찾기 (Arduino Uno 등)
    acm_devices = glob.glob('/dev/ttyACM*')
    if acm_devices:
        acm_devices.sort()
        return acm_devices[0]
    
    # 4. 기타 시리얼 포트 찾기
    other_ports = []
    for port in ports:
        if port.device not in usb_ports:
            other_ports.append(port.device)
    
    if other_ports:
        other_ports.sort()
        return other_ports[0]
    
    # 5. 기본값 반환
    return '/dev/ttyUSB0'


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
    # 환경변수로 포트가 지정되지 않으면 자동 스캔
    DEVICE = os.getenv('SERIAL_DEV', find_serial_port())
    BAUD_RATE = int(os.getenv('BAUD', '115200'))
    TIMEOUT = 0
    WRITE_TIMEOUT = 2
    # Arduino 리셋 방지
    DSRDTR = False
    RTSCTS = False
    
    @classmethod
    def get_available_ports(cls):
        """사용 가능한 모든 시리얼 포트 목록을 반환합니다."""
        ports = serial.tools.list_ports.comports()
        return [port.device for port in ports]
    
    @classmethod
    def refresh_device(cls):
        """시리얼 포트를 다시 스캔하여 DEVICE를 업데이트합니다."""
        cls.DEVICE = find_serial_port()
        return cls.DEVICE


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
    DEVICES_ENDPOINT = '/device/all'  # 장비 목록 조회 (인증 필요)
    CURRENTS_ENDPOINT = '/current/read-all'  # 전류 센서 목록 조회 (인증 불필요)
    USERNAME = os.getenv('BACKEND_USERNAME', 'llewyn')  # 백엔드 인증용 사용자명
    PASSWORD = os.getenv('BACKEND_PASSWORD', '1234')  # 백엔드 인증용 비밀번호
