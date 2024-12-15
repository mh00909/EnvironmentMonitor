from flask import Flask, redirect, render_template, jsonify, request
from flask_socketio import SocketIO, emit
import paho.mqtt.client as mqtt
import requests
import time
import json

app = Flask(__name__)
socketio = SocketIO(app)

# Konfiguracja MQTT brokera
BROKER = "192.168.128.30"
TOPICS = ["/user1/device1/bmp280/temperature", "/user1/device1/bmp280/pressure", "/user1/device1/photoresistor/light"]

# Domyślny adres ESP32 w trybie STA
ESP32_IP = "192.168.128.123"  

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
        response = requests.get("http://192.168.128.123/wifi_config", timeout=3)
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
mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.username_pw_set("username", "password")

try:
    mqtt_client.connect(BROKER, 1883, 60)
except Exception as e:
    print(f"Failed to connect to MQTT broker: {e}")


@socketio.on('connect')
def handle_web_connect():
    print("Web client connected")


if __name__ == '__main__':
    mqtt_client.loop_start()
    socketio.run(app, host='0.0.0.0', port=5000)
