from flask import Flask, redirect, render_template, jsonify, request
from flask_socketio import SocketIO, emit
import paho.mqtt.client as mqtt
import requests
import time
import os

import json

app = Flask(__name__)
socketio = SocketIO(app)

# Konfiguracja MQTT brokera
BROKER = "192.168.57.30"
TOPICS = [
    "/user1/device1/bmp280/temperature", 
    "/user1/device1/bmp280/pressure", 
    "/user1/device1/photoresistor/light", 
    "/user1/device1/ble/temperature", 
    "/user1/device1/ble/humidity"
]

# Domyślny adres ESP32 w trybie STA
ESP32_IP = "192.168.57.123"  

MQTT_CONFIG_FILE = "mqtt_config.json"

def load_mqtt_config():
    """Ładuje konfigurację MQTT z pliku."""
    if os.path.exists(MQTT_CONFIG_FILE):
        with open(MQTT_CONFIG_FILE, 'r') as file:
            return json.load(file)
    else:
        # Domyślna konfiguracja
        return {
            "broker": "192.168.57.30",
            "port": 1883,
            "username": "username",
            "password": "password"
        }

def save_mqtt_config(config):
    """Zapisuje konfigurację MQTT do pliku."""
    with open(MQTT_CONFIG_FILE, 'w') as file:
        json.dump(config, file)

@app.route('/')
def index():
    return render_template('index.html')


def detect_esp32_mode():
    try:
        response = requests.get("http://192.168.4.1/wifi_config", timeout=3)
        if response.status_code == 200:
            print("ESP32 działa w trybie AP")
            return "AP"
    except requests.exceptions.RequestException as e:
        pass

    try:
        response = requests.get("http://192.168.57.123/wifi_config", timeout=3)
        if response.status_code == 200:
            print("ESP32 działa w trybie STA")
            return "STA"
    except requests.exceptions.RequestException as e:
        pass

    return "UNKNOWN"



@app.route('/data')
def data_page():
    return render_template('data.html')

@app.route('/switch_to_ap', methods=['GET'])
def switch_to_ap():
    mqtt_client.publish("/user1/device1/config", "switch_to_ap")
    time.sleep(3)  # Poczekaj na przełączenie ESP32
    return "ESP32 przełączone w tryb AP. Połącz się z siecią ESP32 i wróć do konfiguracji.", 200


@app.route('/switch_to_sta', methods=['GET'])
def switch_to_sta():
    try:
        response = requests.get(f"http://{ESP32_IP}/switch_to_sta", timeout=3)
        if response.status_code == 200:
            return jsonify({"message": "ESP32 przełączono do trybu Station."}), 200
        else:
            return jsonify({"message": "Nie udało się przełączyć ESP32 do trybu Station."}), 500
    except requests.exceptions.RequestException as e:
        print(f"Błąd podczas łączenia z ESP32: {e}")
        return jsonify({"message": "Nie udało się połączyć z ESP32."}), 500



@app.route('/bmp280_config.html', methods=['GET'])
def bmp280_config_page():
    return render_template('bmp280_config.html')

@app.route('/mqtt_config.html', methods=['GET'])
def mqtt_config_page():
    return render_template('mqtt_config.html')

@app.route('/config')
def config():
    global ESP32_IP
    ESP32_IP = "192.168.4.1"
    try:
        response = requests.get(f"http://{ESP32_IP}/config", timeout=5)
        if response.status_code == 200:
            return render_template('config.html')
    except requests.exceptions.RequestException:
        ESP32_IP = "192.168.128.123"  # Wróć do STA w razie problemów
        return "ESP32 w trybie STA", 500

@app.route('/mqtt_config', methods=['GET', 'POST'])
def mqtt_config():
    if request.method == 'GET':
        # Pobierz aktualną konfigurację
        config = load_mqtt_config()
        return jsonify(config), 200
    elif request.method == 'POST':
        # Zaktualizuj konfigurację
        new_config = request.json
        if not new_config:
            return jsonify({"error": "Invalid configuration data"}), 400

        save_mqtt_config(new_config)
        return jsonify({"message": "Configuration updated successfully"}), 200


@app.route('/wifi_config')
def wifi_config():
    global ESP32_IP
    ESP32_IP = "192.168.4.1"
    try:
        response = requests.get(f"http://{ESP32_IP}/wifi_config", timeout=5)
        if response.status_code == 200:
            return render_template('wifi_config.html')
    except requests.exceptions.RequestException:
        ESP32_IP = "192.168.128.123"  # Wróć do STA w razie problemów
        return "ESP32 w trybie STA", 500


@app.route('/sync_mode', methods=['GET'])
def sync_mode():
    mode = detect_esp32_mode()
    ip = "192.168.4.1" if mode == "AP" else "192.168.128.123" if mode == "STA" else None
    return jsonify({"mode": mode, "ip": ip})


@app.route('/bmp280_config', methods=['GET', 'POST'])
def bmp280_config():
    if request.method == 'GET':
        # Pobierz aktualne ustawienia z ESP32
        try:
            response = requests.get(f"http://{ESP32_IP}/bmp280_config", timeout=5)
            if response.status_code == 200:
                return jsonify(response.json()), 200
        except requests.exceptions.RequestException:
            return jsonify({"error": "ESP32 not reachable"}), 500
    elif request.method == 'POST':
        # Wyślij nowe ustawienia do ESP32
        try:
            config_data = request.json
            response = requests.post(f"http://{ESP32_IP}/bmp280_config", json=config_data, timeout=5)
            if response.status_code == 200:
                return jsonify({"message": "Configuration updated successfully"}), 200
            else:
                return jsonify({"error": "Failed to update configuration"}), 500
        except requests.exceptions.RequestException:
            return jsonify({"error": "ESP32 not reachable"}), 500


@app.route('/set_temp_range', methods=['POST'])
def set_temp_range():
    data = request.json
    min_temp = data.get('min_temperature', None)
    max_temp = data.get('max_temperature', None)

    if min_temp is not None and max_temp is not None:
        mqtt_client.publish("/user1/device1/control/temp_range", f"{min_temp},{max_temp}")
        return jsonify({"message": f"Temperature range set to {min_temp}°C - {max_temp}°C"}), 200
    else:
        return jsonify({"error": "Invalid temperature range"}), 400


@app.route('/set_light_range', methods=['POST'])
def set_light_range():
    data = request.json
    min_light = data.get('min_light', None)
    max_light = data.get('max_light', None)

    if min_light is not None and max_light is not None:
        mqtt_client.publish("/user1/device1/control/light_range", f"{min_light},{max_light}")
        return jsonify({"message": f"Light range set to {min_light} - {max_light}"}), 200
    else:
        return jsonify({"error": "Invalid light range"}), 400


# Obsługa zdarzeń MQTT
def on_connect(client, userdata, flags, rc):
    """Obsługa połączenia MQTT."""
    print("Connected to MQTT broker with result code", rc)
    for topic in TOPICS:
        client.subscribe(topic)


def on_message(client, userdata, msg):
    """Obsługa wiadomości MQTT."""
    try:
        data = json.loads(msg.payload.decode())
        topic = msg.topic
        print(f"Received data from topic {topic}: {data}")
        socketio.emit('mqtt_message', {'topic': topic, 'data': data})
    except json.JSONDecodeError:
        print("Received invalid JSON payload")


# Konfiguracja klienta MQTT
# Wczytaj konfigurację MQTT
mqtt_settings = load_mqtt_config()

# Konfiguracja klienta MQTT
mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.username_pw_set(mqtt_settings["username"], mqtt_settings["password"])

try:
    mqtt_client.connect(mqtt_settings["broker"], mqtt_settings["port"], 60)
except Exception as e:
    print(f"Failed to connect to MQTT broker: {e}")



@socketio.on('connect')
def handle_web_connect():
    print("Web client connected")


if __name__ == '__main__':
    mqtt_client.loop_start()
    socketio.run(app, host='0.0.0.0', port=5000)
