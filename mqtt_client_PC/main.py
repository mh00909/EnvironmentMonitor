from flask import Flask, render_template
from flask_socketio import SocketIO, emit
import paho.mqtt.client as mqtt
import json

app = Flask(__name__)
socketio = SocketIO(app)

BROKER = "192.168.0.66"
TOPICS = ["/user1/device1/bmp280/temperature", "/user1/device1/bmp280/pressure", "/user1/device1/photoresistor/light"]

@app.route('/')
def index():
    return render_template('index.html')

def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with result code", rc)
    for topic in TOPICS:
        client.subscribe(topic)

def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())  
        topic = msg.topic
        print(f"Received data from topic {topic}: {data}")
        socketio.emit('mqtt_message', {'topic': topic, 'data': data})
    except json.JSONDecodeError:
        print("Received invalid JSON payload")


mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.username_pw_set("username", "password")
mqtt_client.connect(BROKER, 1883, 60)

@socketio.on('connect')
def handle_web_connect():
    print("Web client connected")

if __name__ == '__main__':
    mqtt_client.loop_start()
    socketio.run(app, host='0.0.0.0', port=5000)
