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