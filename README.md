# ESP8266 Accelerometer Bike Computer

This project uses a D1 Mini (ESP8266) and an MPU-6050 accelerometer to track the rotations of an exercise bike pedal. The data is stored on the device and exposed via a web server.

## Features

- Tracks pedal rotations using an MPU-6050 accelerometer.
- Stores rotation count in LittleFS.
- Hosts a web server to display the rotation count.
- Provides a reset button on the web interface to clear the rotation count.

## Hardware

* Wemos D1 Mini
* MPU-6050 Accelerometer
* Breadboard and jumper wires

## Wiring

Connect the MPU-6050 to the D1 Mini as follows:

| MPU-6050 | D1 Mini |
|---|---|
| VCC | 5V |
| GND | GND |
| SCL | D1 |
| SDA | D2 |

## Installation

1. **Clone the repository:**
   ```bash
   git clone <repository-url>
   cd ESP8266_Accelerometer
   ```

2. **Install PlatformIO:**
   Follow the instructions on the [PlatformIO website](https://platformio.org/install).

3. **Configure WiFi:**
   Open `src/main.cpp` and replace `"YOUR_SSID"` and `"YOUR_PASSWORD"` with your WiFi credentials.

4. **Build and Upload:**
   Connect the D1 Mini to your computer and run the following command:
   ```bash
   platformio run --target upload --target uploadfs
   ```
   This will compile the code, upload it to the D1 Mini, and upload the LittleFS filesystem image.

## Usage

1. Once the project is uploaded and the D1 Mini is powered on, it will connect to your WiFi network.
2. Open a serial monitor to see the IP address of the device.
3. Open a web browser and navigate to the IP address of the D1 Mini.
4. You will see the rotation count, which updates automatically.
5. Click the "Reset" button to set the rotation count back to zero.
