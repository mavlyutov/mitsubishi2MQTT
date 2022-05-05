/*
  mitsubishi2mqtt - Mitsubishi Heat Pump to MQTT control for Home Assistant.
  Copyright (c) 2019 gysmo38, dzungpv, shampeon, endeavour, jascdk, chrdavis, alekslyse.  All right reserved.
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

//#define MY_LANGUAGE fr-FR // define your language

const PROGMEM char* m2mqtt_version = "2022.01";

//Define global variables for files
#ifdef ESP32
  const PROGMEM char* wifi_conf = "/wifi.json";
  const PROGMEM char* mqtt_conf = "/mqtt.json";
  const PROGMEM char* advance_conf = "/advance.json";
  const PROGMEM char* console_file = "/console.log";
  // pinouts
  const PROGMEM  uint8_t blueLedPin = 2;            // The ESP32 has an internal blue LED at D2 (GPIO 02)
#else
  const PROGMEM char* wifi_conf = "wifi.json";
  const PROGMEM char* mqtt_conf = "mqtt.json";
  const PROGMEM char* unit_conf = "unit.json";
  const PROGMEM char* console_file = "console.log";
  // pinouts
  const PROGMEM  uint8_t blueLedPin = LED_BUILTIN; // Onboard LED = digital pin 2 "D4" (blue LED on WEMOS D1-Mini)
#endif
const PROGMEM  uint8_t redLedPin = 0;

// Define global variables for network
const PROGMEM char* hostnamePrefix = "HVAC_";
const PROGMEM uint32_t WIFI_RETRY_INTERVAL_MS = 300000;
unsigned long wifi_timeout;
bool wifi_config_exists;
String hostname = "";
String ap_ssid = "";
String ap_pwd = "";
String ota_pwd;

// Define global variables for MQTT
String mqtt_server;
String mqtt_port;
String mqtt_username;
String mqtt_password;
String mqtt_client_id;

// Define global variables for MQTT topics
String main_topic;
String power_topic;
String mode_topic;
String fan_topic;
String vane_topic;
String widevane_topic;
String temp_topic;
String room_temp_topic;

// Customization
uint8_t min_temp = 16; // Minimum temperature, check value from heatpump remote control
uint8_t max_temp = 31; // Maximum temperature, check value from heatpump remote control
String temp_step = "1"; // Temperature setting step, check value from heatpump remote control

// sketch settings
const PROGMEM uint32_t SEND_ROOM_TEMP_INTERVAL_MS = 45000; // 45 seconds (anything less may cause bouncing)
const PROGMEM uint32_t MQTT_RETRY_INTERVAL_MS = 5000; // 5 seconds
const PROGMEM uint32_t HP_RETRY_INTERVAL_MS = 1000; // 1 seconds
const PROGMEM uint32_t HP_MAX_RETRIES = 5;

// support heat mode settings, some model does not support heat mode
bool supportHeatMode = true;