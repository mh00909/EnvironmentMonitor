from flask import Flask, redirect, render_template, jsonify, request
from flask_socketio import SocketIO, emit
import paho.mqtt.client as mqtt
import requests
import time
import os
import sqlite3
import json

app = Flask(__name__)
socketio = SocketIO(app)

# Konfiguracja MQTT brokera
BROKER = "192.168.57.30"


# Domyślny adres ESP32 w trybie STA
ESP32_IP = "192.168.57.123"  

MQTT_CONFIG_FILE = "mqtt_config.json"


def init_db():
    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()

    # Tworzenie tabel
    cursor.execute('''CREATE TABLE IF NOT EXISTS users (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        name TEXT UNIQUE NOT NULL)''')
    cursor.execute('''CREATE TABLE IF NOT EXISTS subscriptions (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        user_id INTEGER NOT NULL,
                        topic TEXT NOT NULL,
                        FOREIGN KEY (user_id) REFERENCES users (id))''')

    # Dodanie użytkownika domyślnego
    cursor.execute('INSERT OR IGNORE INTO users (id, name) VALUES (1, "default_user")')

    conn.commit()
    conn.close()


@app.route('/users', methods=['POST'])
def add_user():
    data = request.json
    name = data.get('name')
    if not name:
        return jsonify({"error": "Name is required"}), 400

    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()
    try:
        cursor.execute('INSERT INTO users (name) VALUES (?)', (name,))
        conn.commit()
        user_id = cursor.lastrowid
    except sqlite3.IntegrityError:
        return jsonify({"error": "User already exists"}), 400
    finally:
        conn.close()

    return jsonify({"id": user_id, "name": name}), 201


@app.route('/users', methods=['GET'])
def get_users():
    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()
    cursor.execute('SELECT id, name FROM users')
    users = [{"id": row[0], "name": row[1]} for row in cursor.fetchall()]
    conn.close()
    return jsonify(users)



@app.route('/subscriptions', methods=['POST'])
def add_subscription():
    data = request.json
    user_id = data.get('user_id')
    topic = data.get('topic')

    if not user_id or not topic:
        return jsonify({"error": "User ID and topic are required"}), 400

    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()
    cursor.execute('INSERT INTO subscriptions (user_id, topic) VALUES (?, ?)', (user_id, topic))
    conn.commit()
    conn.close()

    return jsonify({"message": "Subscription added successfully"}), 201


@app.route('/subscriptions', methods=['GET'])
def get_subscriptions():
    user_id = request.args.get('user_id')
    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()
    cursor.execute('SELECT topic FROM subscriptions WHERE user_id = ?', (user_id,))
    topics = [row[0] for row in cursor.fetchall()]
    conn.close()
    return jsonify(topics)



def get_user_topics(user_id):
    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()
    cursor.execute('SELECT topic FROM subscriptions WHERE user_id = ?', (user_id,))
    topics = [row[0] for row in cursor.fetchall()]
    conn.close()
    return topics



def load_mqtt_config():
    try:
        with open(MQTT_CONFIG_FILE, 'r') as file:
            config = json.load(file)
            if not all(key in config for key in ["broker", "port", "username", "password"]):
                raise ValueError("Incomplete MQTT configuration.")
            return config
    except (FileNotFoundError, json.JSONDecodeError, ValueError) as e:
        print(f"Error loading MQTT configuration: {e}")
        default_config = {
            "broker": "192.168.57.30",
            "port": 1883,
            "username": "username",
            "password": "password"
        }
        save_mqtt_config(default_config)
        return default_config

    
    # Domyślna konfiguracja
    default_config = {
        "broker": "192.168.57.30",
        "port": 1883,
        "username": "username",
        "password": "password"
    }
    save_mqtt_config(default_config)
    return default_config

def save_mqtt_config(config):
    with open(MQTT_CONFIG_FILE, 'w') as file:
        json.dump(config, file, indent=4)





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

@app.route('/ota_update', methods=['POST'])
def ota_update():
    try:
        mqtt_client.publish("/user1/device1/ota_update", "start")
        return jsonify({"message": "Aktualizacja OTA została zainicjowana."}), 200
    except Exception as e:
        print(f"Błąd podczas inicjowania OTA: {e}")
        return jsonify({"error": "Nie udało się zainicjować aktualizacji OTA."}), 500





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



@app.route('/power_config', methods=['GET', 'POST'])
def power_config():
    if request.method == 'GET':
        # Pobierz aktualne ustawienia z ESP32
        try:
            response = requests.get(f"http://{ESP32_IP}/power_config", timeout=5)
            if response.status_code == 200:
                return jsonify(response.json()), 200
            else:
                return jsonify({"error": "Failed to fetch configuration"}), 500
        except requests.exceptions.RequestException:
            return jsonify({"error": "ESP32 not reachable"}), 500
    elif request.method == 'POST':
        # Wyślij nowe ustawienia do ESP32
        try:
            config_data = request.json
            response = requests.post(f"http://{ESP32_IP}/power_config", json=config_data, timeout=5)
            if response.status_code == 200:
                return jsonify({"message": "Configuration updated successfully"}), 200
            else:
                return jsonify({"error": "Failed to update configuration"}), 500
        except requests.exceptions.RequestException:
            return jsonify({"error": "ESP32 not reachable"}), 500

@app.route('/complete_config', methods=['POST'])
def complete_config():
    try:
        response = requests.post(f"http://{ESP32_IP}/complete_config", timeout=5)
        if response.status_code == 200:
            return jsonify({"message": "Configuration completed"}), 200
        else:
            return jsonify({"error": "Failed to complete configuration"}), 500
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



@app.route('/add_mqtt_user', methods=['POST'])
def add_mqtt_user():
    data = request.json
    user_id = data.get('user_id')
    user_name = data.get('user_name')

    if not user_id or not user_name:
        return jsonify({"error": "User ID and Name are required"}), 400

    topics = get_user_topics(user_id)
    mqtt_payload = {
        "user_id": user_name,
        "device_id": f"device_{user_id}",
        "topics": topics
    }

    try:
        result = mqtt_client.publish("/system/add_client", json.dumps(mqtt_payload))
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"Published to MQTT: {mqtt_payload}")
        else:
            print(f"Failed to publish to MQTT, result code: {result.rc}")
        return jsonify({"message": "User added via MQTT successfully"}), 200
    except Exception as e:
        print(f"Failed to publish MQTT message: {e}")
        return jsonify({"error": "Failed to add user via MQTT"}), 500



# Obsługa zdarzeń MQTT
subscribed_topics = set()
def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with result code", rc)
    try:
        user_id = 1
        topics = get_user_topics(user_id)
        for topic in topics:
            if topic and topic not in subscribed_topics:
                client.subscribe(topic)
                subscribed_topics.add(topic)
                print(f"Subscribed to topic: {topic}")
    except Exception as e:
        print(f"Error during topic subscription: {e}")


def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode()
    try:
        data = json.loads(payload)
        print(f"Received JSON data from topic {topic}: {data}")
        socketio.emit('mqtt_message', {'topic': topic, 'data': data})
    except json.JSONDecodeError:
        print(f"Received raw data from topic {topic}: {payload}")
        socketio.emit('mqtt_message', {'topic': topic, 'data': payload})



@app.route('/')
def index():
    return render_template('users.html')


@app.route('/bmp280_config.html', methods=['GET'])
def bmp280_config_page():
    return render_template('bmp280_config.html')

@app.route('/mqtt_config.html', methods=['GET'])
def mqtt_config_page():
    return render_template('mqtt_config.html')

@app.route('/power_config.html', methods=['GET'])
def power_config_page():
    return render_template('power_config.html')

@app.route('/index')
def index_page():
    return render_template('index.html')

@app.route('/data')
def data_page():
    return render_template('data.html')

# Konfiguracja klienta MQTT
# Wczytaj konfigurację MQTT
mqtt_settings = load_mqtt_config()

# Konfiguracja klienta MQTT
mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.username_pw_set(mqtt_settings["username"], mqtt_settings["password"])


def connect_mqtt():
    while True:
        try:
            mqtt_client.connect(mqtt_settings["broker"], mqtt_settings["port"], 60)
            break
        except Exception as e:
            print(f"Failed to connect to MQTT broker: {e}")
            time.sleep(5)  # Odczekaj przed kolejną próbą



@socketio.on('connect')
def handle_web_connect():
    print("Web client connected")


if __name__ == '__main__':
    init_db()
    connect_mqtt()
    mqtt_client.loop_start()
    socketio.run(app, host='0.0.0.0', port=5000)
from flask import Flask, redirect, render_template, jsonify, request
from flask_socketio import SocketIO, emit
import paho.mqtt.client as mqtt
import requests
import time
import os
import sqlite3
import json

app = Flask(__name__)
socketio = SocketIO(app)

# Konfiguracja MQTT brokera
BROKER = "192.168.57.30"


# Domyślny adres ESP32 w trybie STA
ESP32_IP = "192.168.57.123"  

MQTT_CONFIG_FILE = "mqtt_config.json"


def init_db():
    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()

    # Tworzenie tabel
    cursor.execute('''CREATE TABLE IF NOT EXISTS users (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        name TEXT UNIQUE NOT NULL)''')
    cursor.execute('''CREATE TABLE IF NOT EXISTS subscriptions (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        user_id INTEGER NOT NULL,
                        topic TEXT NOT NULL,
                        FOREIGN KEY (user_id) REFERENCES users (id))''')

    # Dodanie użytkownika domyślnego
    cursor.execute('INSERT OR IGNORE INTO users (id, name) VALUES (1, "default_user")')

    conn.commit()
    conn.close()


@app.route('/users', methods=['POST'])
def add_user():
    data = request.json
    name = data.get('name')
    if not name:
        return jsonify({"error": "Name is required"}), 400

    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()
    try:
        cursor.execute('INSERT INTO users (name) VALUES (?)', (name,))
        conn.commit()
        user_id = cursor.lastrowid
    except sqlite3.IntegrityError:
        return jsonify({"error": "User already exists"}), 400
    finally:
        conn.close()

    return jsonify({"id": user_id, "name": name}), 201


@app.route('/users', methods=['GET'])
def get_users():
    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()
    cursor.execute('SELECT id, name FROM users')
    users = [{"id": row[0], "name": row[1]} for row in cursor.fetchall()]
    conn.close()
    return jsonify(users)



@app.route('/subscriptions', methods=['POST'])
def add_subscription():
    data = request.json
    user_id = data.get('user_id')
    topic = data.get('topic')

    if not user_id or not topic:
        return jsonify({"error": "User ID and topic are required"}), 400

    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()
    cursor.execute('INSERT INTO subscriptions (user_id, topic) VALUES (?, ?)', (user_id, topic))
    conn.commit()
    conn.close()

    return jsonify({"message": "Subscription added successfully"}), 201


@app.route('/subscriptions', methods=['GET'])
def get_subscriptions():
    user_id = request.args.get('user_id')
    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()
    cursor.execute('SELECT topic FROM subscriptions WHERE user_id = ?', (user_id,))
    topics = [row[0] for row in cursor.fetchall()]
    conn.close()
    return jsonify(topics)



def get_user_topics(user_id):
    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()
    cursor.execute('SELECT topic FROM subscriptions WHERE user_id = ?', (user_id,))
    topics = [row[0] for row in cursor.fetchall()]
    conn.close()
    return topics



def load_mqtt_config():
    try:
        with open(MQTT_CONFIG_FILE, 'r') as file:
            config = json.load(file)
            if not all(key in config for key in ["broker", "port", "username", "password"]):
                raise ValueError("Incomplete MQTT configuration.")
            return config
    except (FileNotFoundError, json.JSONDecodeError, ValueError) as e:
        print(f"Error loading MQTT configuration: {e}")
        default_config = {
            "broker": "192.168.57.30",
            "port": 1883,
            "username": "username",
            "password": "password"
        }
        save_mqtt_config(default_config)
        return default_config

    
    # Domyślna konfiguracja
    default_config = {
        "broker": "192.168.57.30",
        "port": 1883,
        "username": "username",
        "password": "password"
    }
    save_mqtt_config(default_config)
    return default_config

def save_mqtt_config(config):
    with open(MQTT_CONFIG_FILE, 'w') as file:
        json.dump(config, file, indent=4)





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

@app.route('/ota_update', methods=['POST'])
def ota_update():
    try:
        mqtt_client.publish("/user1/device1/ota_update", "start")
        return jsonify({"message": "Aktualizacja OTA została zainicjowana."}), 200
    except Exception as e:
        print(f"Błąd podczas inicjowania OTA: {e}")
        return jsonify({"error": "Nie udało się zainicjować aktualizacji OTA."}), 500





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



@app.route('/power_config', methods=['GET', 'POST'])
def power_config():
    if request.method == 'GET':
        # Pobierz aktualne ustawienia z ESP32
        try:
            response = requests.get(f"http://{ESP32_IP}/power_config", timeout=5)
            if response.status_code == 200:
                return jsonify(response.json()), 200
            else:
                return jsonify({"error": "Failed to fetch configuration"}), 500
        except requests.exceptions.RequestException:
            return jsonify({"error": "ESP32 not reachable"}), 500
    elif request.method == 'POST':
        # Wyślij nowe ustawienia do ESP32
        try:
            config_data = request.json
            response = requests.post(f"http://{ESP32_IP}/power_config", json=config_data, timeout=5)
            if response.status_code == 200:
                return jsonify({"message": "Configuration updated successfully"}), 200
            else:
                return jsonify({"error": "Failed to update configuration"}), 500
        except requests.exceptions.RequestException:
            return jsonify({"error": "ESP32 not reachable"}), 500

@app.route('/complete_config', methods=['POST'])
def complete_config():
    try:
        response = requests.post(f"http://{ESP32_IP}/complete_config", timeout=5)
        if response.status_code == 200:
            return jsonify({"message": "Configuration completed"}), 200
        else:
            return jsonify({"error": "Failed to complete configuration"}), 500
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



@app.route('/add_mqtt_user', methods=['POST'])
def add_mqtt_user():
    data = request.json
    user_id = data.get('user_id')
    user_name = data.get('user_name')

    if not user_id or not user_name:
        return jsonify({"error": "User ID and Name are required"}), 400

    topics = get_user_topics(user_id)
    mqtt_payload = {
        "user_id": user_name,
        "device_id": f"device_{user_id}",
        "topics": topics
    }

    try:
        result = mqtt_client.publish("/system/add_client", json.dumps(mqtt_payload))
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"Published to MQTT: {mqtt_payload}")
        else:
            print(f"Failed to publish to MQTT, result code: {result.rc}")
        return jsonify({"message": "User added via MQTT successfully"}), 200
    except Exception as e:
        print(f"Failed to publish MQTT message: {e}")
        return jsonify({"error": "Failed to add user via MQTT"}), 500



# Obsługa zdarzeń MQTT
subscribed_topics = set()
def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with result code", rc)
    try:
        user_id = 1
        topics = get_user_topics(user_id)
        for topic in topics:
            if topic and topic not in subscribed_topics:
                client.subscribe(topic)
                subscribed_topics.add(topic)
                print(f"Subscribed to topic: {topic}")
    except Exception as e:
        print(f"Error during topic subscription: {e}")


def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode()
    try:
        data = json.loads(payload)
        print(f"Received JSON data from topic {topic}: {data}")
        socketio.emit('mqtt_message', {'topic': topic, 'data': data})
    except json.JSONDecodeError:
        print(f"Received raw data from topic {topic}: {payload}")
        socketio.emit('mqtt_message', {'topic': topic, 'data': payload})



@app.route('/')
def index():
    return render_template('users.html')


@app.route('/bmp280_config.html', methods=['GET'])
def bmp280_config_page():
    return render_template('bmp280_config.html')

@app.route('/mqtt_config.html', methods=['GET'])
def mqtt_config_page():
    return render_template('mqtt_config.html')

@app.route('/power_config.html', methods=['GET'])
def power_config_page():
    return render_template('power_config.html')

@app.route('/index')
def index_page():
    return render_template('index.html')

@app.route('/data')
def data_page():
    return render_template('data.html')

# Konfiguracja klienta MQTT
# Wczytaj konfigurację MQTT
mqtt_settings = load_mqtt_config()

# Konfiguracja klienta MQTT
mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.username_pw_set(mqtt_settings["username"], mqtt_settings["password"])


def connect_mqtt():
    while True:
        try:
            mqtt_client.connect(mqtt_settings["broker"], mqtt_settings["port"], 60)
            break
        except Exception as e:
            print(f"Failed to connect to MQTT broker: {e}")
            time.sleep(5)  # Odczekaj przed kolejną próbą



@socketio.on('connect')
def handle_web_connect():
    print("Web client connected")


if __name__ == '__main__':
    init_db()
    connect_mqtt()
    mqtt_client.loop_start()
    socketio.run(app, host='0.0.0.0', port=5000)
