from flask import Blueprint, jsonify, request, render_template
import requests
from app.mqtt_handler import mqtt_client  
import json


config_bp = Blueprint('config', __name__)


# Ustawianie zakresu temperatury
@config_bp.route('/set_temp_range', methods=['POST'])
def set_temp_range():
    data = request.get_json()
    min_temp = data.get('min_temperature')
    max_temp = data.get('max_temperature')

    if min_temp is not None and max_temp is not None:
        # Wysyłanie zakresu przez MQTT
        topic = '/system/settings/temp_range'
        payload = json.dumps({"min_temperature": min_temp, "max_temperature": max_temp})
        mqtt_client.publish(topic, payload)
        print(f"Published to {topic}: {payload}")
        return jsonify({"message": "Temperature range updated and published successfully!"}), 200
    return jsonify({"error": "Invalid data"}), 400

# Ustawianie zakresu światła
@config_bp.route('/set_light_range', methods=['POST'])
def set_light_range():
    data = request.get_json()
    min_light = data.get('min_light')
    max_light = data.get('max_light')

    if min_light is not None and max_light is not None:
        # Wysyłanie zakresu przez MQTT
        topic = '/system/settings/light_range'
        payload = json.dumps({"min_light": min_light, "max_light": max_light})
        mqtt_client.publish(topic, payload)
        print(f"Published to {topic}: {payload}")
        return jsonify({"message": "Light range updated and published successfully!"}), 200
    return jsonify({"error": "Invalid data"}), 400


def load_config(file_path):
    try:
        with open(file_path, 'r') as file:
            return json.load(file)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}

def save_config(file_path, config_data):
    with open(file_path, 'w') as file:
        json.dump(config_data, file, indent=4)

