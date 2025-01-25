## ESP32 Environment Monitor
 
### 📋 Overview
The ESP32 Environment Monitor is an IoT-based project designed to monitor environmental parameters using sensors connected to an ESP32 microcontroller. It features Wi-Fi, BLE, and MQTT capabilities for real-time data sharing and configuration. The monitor collects data on temperature, pressure, light levels, and BLE-based humidity/temperature and provides a dashboard and API for data visualization and control.

### 🎯 Features
**Sensor Monitoring:**

- Temperature and pressure using the BMP280 sensor (I2C communication).
- Ambient light levels using a photoresistor (analog/ADC input).
- Temperature and humidity via BLE (e.g., Xiaomi Thermometer).

**Wireless Communication:**

- Wi-Fi in STA/AP modes.
- MQTT for real-time data sharing.
- BLE for connecting to external sensors.

**Server Functionality:**

- Web-based configuration via HTTP in Access Point mode.
- Live data dashboard with dynamic updates using Socket.IO.

**User Interactions:**

- Button-based manual measurement trigger.
- Configurable thresholds for alerts (e.g., temperature and light).

### 🛠️ Components Used

- **ESP32 Microcontroller**
- **Sensors:**
    - **BMP280:** Measures temperature and atmospheric pressure via I2C.
    - **Photoresistor:** Measures ambient light levels (analog input).
    - **BLE Sensor** (e.g., Xiaomi Thermometer): Provides temperature and humidity readings via BLE.
- **LED Indicators:** Signal system states and alert thresholds.
- **Button (GPIO):** Manual trigger for sensor measurements.


### Project structure
```
.
├── components/              # Reusable components for ESP32
│   ├── bmp280/              # BMP280 sensor driver  
│   └── sensor_handler/      # Sensor management utilities
├── main/                    # Main firmware source for ESP32
│
├── project/                 # Server-side application (Flask)
│   ├── app/                 # Flask application logic
│   │   ├── routes/          # Routes for handling HTTP requests
│   │   ├── templates/       # HTML templates for the user interface
│   │   ├── database.py      # Database connection and operations
│   │   ├── extensions.py    # Flask extensions 
│   │   ├── main.py          # Main entry point for the Flask application
│   │   └── mqtt_handler.py  # MQTT communication handling
│   ├── environment_monitor.db # SQLite database for user and device management
│   ├── bmp280_config.json   # Default configuration for BMP280
│   ├── mqtt_config.json     # Configuration for the MQTT broker
│   └── run.py               # Script to start the Flask server
│          
└── partitions.csv           # Partition table for the ESP32 firmware
```


---

### Installation
1. Clone the repository:
```
git clone https://github.com/mh00909/monitor-srodowiskowy.git
```
2. Configure the ESP-IDF environment:
```
idf.py set-target esp32
```
3. Build and flash the firmware:
```
idf.py build flash monitor
```
4. Run the server-side application:
```
cd project
python run.py
```