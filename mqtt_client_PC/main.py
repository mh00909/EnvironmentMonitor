import paho.mqtt.client as mqtt

BROKER = "192.168.128.30"  
TOPICS = ["/user1/device1/bmp280/temperature", "/user1/device1/bmp280/pressure", "/user1/device1/photoresistor/light"]

def on_connect(client, userdata, flags, rc):
    print("Connected with result code " + str(rc))
    for topic in TOPICS:
        client.subscribe(topic)

def on_message(client, userdata, msg):
    print(f"{msg.topic}: {msg.payload.decode()}")

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.username_pw_set("username", "password")
client.connect(BROKER, 1883, 60)
client.loop_forever()
