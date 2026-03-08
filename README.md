## ESP32 Environment Monitor
 
### Overview
IoT system for monitoring environmental parameters such as temperature, humidity, atmospheric pressure, and light intensity using an ESP32 microcontroller, MQTT communication, and a web dashboard built with Flask.

The system collects data from multiple sensors, transmits it via MQTT, and visualizes it in real time using a browser-based interface.

### System Architecture
The project consists of three main layers:
```
Sensors
   ↓
ESP32 Firmware (ESP-IDF)
   ↓
MQTT Broker
   ↓
Flask Server (Python)
   ↓
Web Dashboard
```
---
### Hardware
#### Microcontroller
- ESP32
#### Sensors
| Sensor            | Function                             | Interface    |
| ----------------- | ------------------------------------ | ------------ |
| BMP280            | Temperature and atmospheric pressure | I2C          |
| Photoresistor     | Ambient light measurement            | Analog (ADC) |
| Xiaomi LYWSD03MMC | Temperature and humidity             | BLE          |
#### Additional Components
- 2× LED indicators (alerts)
- ESP32 onboard LED (WiFi state)
- External button (GPIO13)
- ESP32 onboard button (GPIO0)

#### Hardware setup
<img width="824" height="604" alt="im" src="https://github.com/user-attachments/assets/e65d84bd-507c-4eea-8a7a-00ba0b6f73d3" />

---
### Firmware (ESP32)
The firmware is implemented using ESP-IDF.
Responsibilities:
- sensor data acquisition
- BLE scanning and communication
- MQTT publishing
- Wi-Fi connection management
- configuration storage in NVS
- button and LED handling
---
### Network Modes
The ESP32 supports two network modes.
#### Station Mode (STA)
- connects to existing Wi-Fi
- sends data to MQTT broker
- normal operation mode
#### Access Point Mode (AP)
- ESP32 creates its own Wi-Fi network
- hosts configuration interface
- allows setting: Wi-Fi credentials,  MQTT broker settings, alert thresholds
---
### BLE Integration
The system reads data from a Xiaomi LYWSD03MMC thermometer via BLE.
BLE workflow:
1. ESP32 scans nearby BLE devices
2. finds Xiaomi thermometer
3. establishes connection
4. reads temperature and humidity
5. publishes data via MQTT

---
### MQTT Communication
Sensor data is transmitted using MQTT.
#### Topic structure
```
{user_id}/{device_id}/{sensor_id}/{metric_id}
```
#### Example topics
```
user/device/bmp280/temperature
user/device/bmp280/pressure
user/device/photoresistor/light
user/device/ble/temperature
user/device/ble/humidity
```
#### System topics
```
/system/settings/temp_range
/system/settings/light_range
/system/add_client
/system/add_device
/system/add_sensor
/system/add_metric
```
#### Message format
All messages use JSON.
Example:
```
{
  "pressure": 1012.5
}
```
### Server Application
The backend is implemented in Python using Flask.
#### Responsibilities:
- MQTT client
- user authentication
- device management
- data storage
- dashboard rendering
- real-time updates via Socket.IO
#### Features
- user registration and login
- device assignment to users
- sensor configuration
- alert threshold configuration
- real-time dashboard
- configuration management
- Data is stored in a SQLite database.
---
### Web Dashboard
The frontend is a simple Flask-rendered web interface.
#### Technologies:
- HTML
- Jinja2 templates
- JavaScript
- Socket.IO
#### Features:
- user login and registration
- device management
- configuration interface
- real-time environmental monitoring
- data visualization with charts and indicators
#### Dashboard displays:
- temperature
- humidity
- atmospheric pressure
- light intensity
#### Dashboard screenshots
<img width="763" alt="register" src="https://github.com/user-attachments/assets/6c86683f-e754-47c6-b4f0-dc0f05c1e83f" />
<img width="763" alt="login" src="https://github.com/user-attachments/assets/cd079fd7-b4be-407f-8cf3-1d56813fbbd9" />
<img width="1468" alt="index" src="https://github.com/user-attachments/assets/3c39f971-14f3-4a5e-be18-f820a4233ca7" />
<img width="1468" height="457" alt="add_sensor" src="https://github.com/user-attachments/assets/fc0af16d-8f2f-4df7-adfd-2c779fdc8a72" />
<img width="1839" height="929" alt="account_settings" src="https://github.com/user-attachments/assets/2a9cb777-0cdd-4530-9b89-4a6360ab2bd9" />
<img width="1468" alt="data1" src="https://github.com/user-attachments/assets/a54537ae-ab19-4b92-833e-e9fd368f966e" />
<img width="1342" height="649" alt="data2" src="https://github.com/user-attachments/assets/4d237fa1-e427-492b-b52c-4e56f1bd2b38" />
<img width="674" height="522" alt="config" src="https://github.com/user-attachments/assets/505b2639-560a-40af-89cf-bd415d0e81bc" />
<img width="1454" height="415" alt="wifi_config" src="https://github.com/user-attachments/assets/1a3810f8-3764-45d1-9d42-8b1015efccb5" />
<img width="1400" height="696" alt="bmp280_config" src="https://github.com/user-attachments/assets/7523029a-afc7-4990-8d97-8a7d980921ed" />
<img width="1490" height="672" alt="mqtt_config" src="https://github.com/user-attachments/assets/fe45b8c6-bab2-478a-8b96-fd96f4b1f7b7" />


---
### Project structure
```
EnvironmentMonitor
│
├── components/              # ESP-IDF components
│   ├── bmp280/              # BMP280 sensor driver
│   └── sensor_handler/      # Sensor utilities
│
├── main/                    # ESP32 firmware
│
├── project/                 # Server application
│   │
│   ├── app/
│   │   ├── routes/          # HTTP routes
│   │   ├── templates/       # Web UI
│   │   ├── database.py      # Database layer
│   │   ├── extensions.py    # Flask extensions
│   │   ├── main.py          # Application entry
│   │   └── mqtt_handler.py  # MQTT communication
│   │
│   ├── environment_monitor.db
│   ├── bmp280_config.json
│   ├── mqtt_config.json
│   └── run.py
│
└── partitions.csv           # ESP32 partition table
```
---
### Installation
1. Clone repository
```
git clone https://github.com/mh00909/EnvironmentMonitor.git
cd EnvironmentMonitor
```
2. Firmware (ESP32)
Requirements:
- ESP-IDF
- ESP32 toolchain
Build firmware:
```
idf.py set-target esp32
idf.py build
```
Flash device:
```
idf.py flash monitor
```
3. MQTT Broker
Install Mosquitto (example):
Linux:
```
sudo apt install mosquitto
```
or run using Docker.
4. Server Application
Navigate to the server directory:
```
cd project
```
Install dependencies:
```
pip install flask flask-socketio paho-mqtt
```
Run the server:
```
python run.py
```
Server will start at:
```
http://localhost:5000
```
---
### Usage
- Open the web dashboard in a browser.
- Register a new user.
- Add a device.
- Configure MQTT and Wi-Fi settings.
- Start monitoring environmental data in real time.
---
### System Features
- real-time environmental monitoring
- MQTT-based IoT communication
- BLE sensor integration
- web-based configuration
- live dashboard
- alert thresholds with LED indicators
- persistent configuration storage (NVS)
---
### License
This project is intended for educational purposes within the Internet of Things and Embedded Systems course.
