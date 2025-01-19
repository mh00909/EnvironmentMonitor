import json
from paho.mqtt.client import Client
from app.extensions import socketio
from app.database import get_user_topics
from app.database import get_user_devices, get_device_sensors, get_sensor_metrics

# Inicjalizacja klienta MQTT
mqtt_client = Client()

# Wczytanie konfiguracji MQTT z pliku
def load_mqtt_config():
    try:
        with open("mqtt_config.json", "r") as config_file:
            return json.load(config_file)
    except (FileNotFoundError, json.JSONDecodeError) as e:
        print(f"Error loading MQTT configuration: {e}")
        return None

mqtt_config = load_mqtt_config()
if mqtt_config is None:
    raise Exception("Failed to load MQTT configuration. Check mqtt_config.json.")

# Funkcja obsługi połączenia z brokerem MQTT
def on_connect(client, userdata, flags, rc):
    print(f"MQTT Connected with result code {rc}")
    if rc == 0:  # Sukces
        user_id = userdata.get("user_id", None)
        if user_id:
            topics = userdata.get("topics", [])
            for topic in topics:
                client.subscribe(topic)
                print(f"Subscribed to topic: {topic} for user {user_id}")
    else:
        print(f"Failed to connect to MQTT broker. Return code: {rc}")

# Funkcja obsługi wiadomości MQTT
def on_message(client, userdata, msg):
    try:
        print(f"Message received on topic {msg.topic}: {msg.payload.decode()}")
        data = json.loads(msg.payload.decode())

        # Emitowanie wiadomości do frontend-u za pomocą Flask-SocketIO
        socketio.emit('mqtt_message', {'topic': msg.topic, 'data': data}, namespace='/')
    except json.JSONDecodeError:
        print(f"Error decoding message: {msg.payload.decode()}")

# Funkcja konfiguracji klienta MQTT
def setup_mqtt(user_id=None):
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message

    # Pobranie tematów dla użytkownika z bazy danych
    user_topics = get_user_topics(user_id) if user_id else []

    # Przekazanie danych użytkownika do klienta MQTT
    mqtt_client.user_data_set({"user_id": user_id, "topics": user_topics})

    print(f"Connecting to MQTT broker at {mqtt_config['broker']}:{mqtt_config['port']}")
    mqtt_client.username_pw_set(mqtt_config['username'], mqtt_config['password'])

    try:
        mqtt_client.connect(mqtt_config['broker'], mqtt_config['port'])
        print("MQTT connected successfully.")
    except Exception as e:
        print(f"Error connecting to MQTT broker: {e}")

    mqtt_client.loop_start()

# Funkcja do publikowania danych użytkownika
def publish_user_topic(user_id, device_id, sensor_type, metric, value):
    topic = f"/{user_id}/{device_id}/{sensor_type}/{metric}"
    message = {metric: value}
    try:
        mqtt_client.publish(topic, json.dumps(message))
        print(f"Published to {topic}: {message}")
    except Exception as e:
        print(f"Error publishing to {topic}: {e}")

# Funkcja do publikowania dodania nowego użytkownika i urządzenia
def publish_add_client(user_id):
    topic = "/system/add_client"
    payload = json.dumps({"user_id": str(user_id)})  # Konwersja user_id na string
    try:
        mqtt_client.publish(topic, payload, retain=True)
        print(f"Published to {topic} with retain: {payload}")
    except Exception as e:
        print(f"Error publishing to {topic}: {e}")


def publish_add_device(user_id, device_id):
    topic = "/system/add_device"
    payload = json.dumps({"user_id": str(user_id), "device_id": str(device_id)})  # Konwersja user_id na string
    try:
        mqtt_client.publish(topic, payload, retain=True)
        print(f"Published to {topic}: {payload}")
    except Exception as e:
        print(f"Error publishing to {topic}: {e}")



def publish_add_sensor(user_id, device_id, sensor_type):
    topic = "/system/add_sensor"
    payload = json.dumps({"user_id": str(user_id), "device_id": str(device_id), "sensor_id": str(sensor_type)})  # Konwersja user_id na string
    try:
        mqtt_client.publish(topic, payload, retain=True)
        print(f"Published to {topic}: {payload}")
    except Exception as e:
        print(f"Error publishing to {topic}: {e}")



def publish_add_metric(user_id, device_id, sensor_type, metric):
    topic = "/system/add_metric"
    payload = json.dumps({"user_id": str(user_id), "device_id": str(device_id), "sensor_id": str(sensor_type), "metric_id": str(metric)})
    mqtt_client.publish(topic, payload, retain=True)
    print(f"Published to {topic}: {payload}")


# Funkcja do subskrybowania tematów użytkownika
def subscribe_user_topics(user_id):
    topics = get_user_topics(user_id)
    for topic in topics:
        try:
            mqtt_client.subscribe(topic)
            print(f"Subscribed to topic: {topic}")
        except Exception as e:
            print(f"Error subscribing to {topic}: {e}")

# Funkcja do zatrzymania klienta MQTT
def stop_mqtt():
    try:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        print("MQTT client stopped and disconnected.")
    except Exception as e:
        print(f"Error stopping MQTT client: {e}")









def publish_user_data(user_id):
    # Pobierz urządzenia użytkownika
    devices = get_user_devices(user_id)

    for device in devices:
        # Publikacja urządzenia
        topic = "/system/add_device"
        payload = json.dumps({"user_id": user_id, "device_id": device['device_id']})
        mqtt_client.publish(topic, payload, retain=True)
        print(f"Published to {topic}: {payload}")

        # Pobierz czujniki dla urządzenia
        sensors = get_device_sensors(user_id, device['device_id'])
        for sensor in sensors:
            # Publikacja czujnika
            topic = "/system/add_sensor"
            payload = json.dumps({
                "user_id": user_id,
                "device_id": device['device_id'],
                "sensor_id": sensor['sensor_type']
            })
            mqtt_client.publish(topic, payload, retain=True)
            print(f"Published to {topic}: {payload}")

            # Pobierz metryki dla czujnika
            metrics = get_sensor_metrics(user_id, device['device_id'], sensor['sensor_type'])
            for metric in metrics:
                # Publikacja metryki
                topic = "/system/add_metric"
                payload = json.dumps({
                    "user_id": user_id,
                    "device_id": device['device_id'],
                    "sensor_id": sensor['sensor_type'],
                    "metric_id": metric['metric']
                })
                mqtt_client.publish(topic, payload, retain=True)
                print(f"Published to {topic}: {payload}")



def subscribe_to_metrics(user_id):
    # Pobierz urządzenia użytkownika
    devices = get_user_devices(user_id)

    for device in devices:
        device_id = device['device_id']

        # Pobierz czujniki dla urządzenia
        sensors = get_device_sensors(user_id, device_id)
        for sensor in sensors:
            sensor_id = sensor['sensor_type']

            # Pobierz metryki dla czujnika
            metrics = get_sensor_metrics(user_id, device_id, sensor_id)
            for metric in metrics:
                metric_id = metric['metric']

                # Subskrybuj temat dla każdej metryki
                topic = f"/{user_id}/{device_id}/{sensor_id}/{metric_id}"
                try:
                    mqtt_client.subscribe(topic)
                    print(f"Subscribed to topic: {topic}")
                except Exception as e:
                    print(f"Error subscribing to {topic}: {e}")
