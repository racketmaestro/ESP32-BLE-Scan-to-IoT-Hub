# Azure IoT Hub ESP32
This project demonstrates how to use an ESP32 device to collect Bluetooth data and upload it to Azure IoT Hub using the Azure SDK for C and MQTT protocol. The ESP32 device scans for nearby Bluetooth devices, collects relevant data, and then uploads this data to the IoT Hub where it can be processed, stored, and analyzed.

## Project Description
This project utilizes the Azure SDK for C Arduino library to connect an ESP32 device to Azure IoT Hub. The ESP32 device is programmed to continuously scan for nearby Bluetooth devices. When a Bluetooth device is found, the ESP32 collects information such as the device's address and signal strength. This data is then sent to Azure IoT Hub via MQTT, a lightweight messaging protocol ideal for IoT devices.

Azure IoT Hub serves as the MQTT server, receiving the Bluetooth data sent by the ESP32 device. The IoT Hub can then route this data to other Azure services for storage, processing, and analysis. This can be used in a variety of applications, such as tracking the presence and proximity of Bluetooth devices in an area, analyzing patterns in device activity, and more.

## Prerequisites
Platformio IDE
An ESP32 device
An Azure subscription
An instance of Azure IoT Hub
Setup
Install the Azure SDK for C: This can be done using the Arduino Library Manager. In the Arduino IDE, go to Sketch > Include Library > Manage Libraries. Then search for "Azure SDK for C" and install it.

Set up Azure IoT Hub: Follow the instructions here to create an IoT Hub instance. Once created, you'll need to register your ESP32 device and get its connection string. Instructions for that can be found here.

Update the connection string in the code: Open the iot_configs.h file and replace the placeholder text <Your IoT Hub Device Connection String> with the connection string you got from Azure IoT Hub.

## Usage
Upload the code to your ESP32: You can do this using the Platformio IDE. Connect your ESP32 to your computer, select the correct board and port from the Tools menu, then click on Upload.

Monitor the device: You can monitor the messages sent by the device to the IoT Hub using the Azure portal or any MQTT client.
