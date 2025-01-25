import json
import time
from flask import Flask, jsonify, redirect, render_template, request, session, url_for
from flask_socketio import SocketIO
import requests
from app.mqtt_handler import setup_mqtt, mqtt_client
from app.extensions import socketio


MQTT_CONFIG_FILE = "mqtt_config.json"

def save_mqtt_config(config):
    with open(MQTT_CONFIG_FILE, 'w') as file:
        json.dump(config, file, indent=4)




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



def create_app():
    app = Flask(__name__)
    app.config['SECRET_KEY'] = 'your_secret_key'
    setup_mqtt()
    from app.database import init_db
    init_db()

    # Inicjalizacja SocketIO z aplikacją
    socketio.init_app(app)
    # Rejestracja blueprintów
    from .routes.auth import auth_bp
    from .routes.subscriptions import subscriptions_bp
    from .routes.devices import devices_bp
    from .routes.config import config_bp
 
    app.register_blueprint(auth_bp, url_prefix='/auth')
    app.register_blueprint(subscriptions_bp, url_prefix='/subscriptions')
    app.register_blueprint(devices_bp)
    app.register_blueprint(config_bp)

    @app.before_request
    def check_login():
        if 'user_id' not in session and request.endpoint not in ['auth.login', 'auth.register', 'static']:
            return redirect(url_for('auth.login'))

    @app.route('/')
    def home():
        if 'user_id' in session:
            return redirect(url_for('devices.index'))
        return redirect(url_for('auth.login'))
    






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
         #   print(f"Błąd podczas łączenia z ESP32: {e}")
            return jsonify({"message": "Nie udało się połączyć z ESP32."}), 500

  





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
            return render_template('mqtt_config.html', config=config)
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
                    return render_template('bmp280_config.html')
            except requests.exceptions.RequestException:
                return jsonify({"error": "ESP32 not reachable"}), 500
        elif request.method == 'POST':
            # Wyślij nowe ustawienia do ESP32
            try:
                config_data = request.json
                response = requests.post(f"http://{ESP32_IP}/bmp280_config", json=config_data, timeout=5)
                if response.status_code == 200:
                    return render_template(''), 200
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








    return app, socketio
