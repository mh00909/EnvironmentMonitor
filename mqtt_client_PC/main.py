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
        response = requests.get("http://192.168.4.1/wifi_config", timeout=5)
        if response.status_code == 200:
            print("ESP32 działa w trybie AP")
            return "AP"
    except requests.exceptions.RequestException as e:
        pass

    try:
        response = requests.get("http://192.168.128.123/wifi_config", timeout=5)
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
    time.sleep(5)  # Poczekaj na przełączenie ESP32
    return "ESP32 przełączone w tryb AP. Połącz się z siecią ESP32 i wróć do konfiguracji.", 200

@app.route('/switch_to_sta', methods=['GET'])
def switch_to_sta():
    mqtt_client.publish("/user1/device1/config", "switch_to_sta")
    time.sleep(5)  # Poczekaj na przełączenie ESP32
    return redirect('/')


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
