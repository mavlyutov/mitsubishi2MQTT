# mitsubishi2wb
Use MQTT and ESP8266/ESP32 module to control Mitsubishi HVAC unit.
<br>It use SwiCago librairies: https://github.com/SwiCago/HeatPump
<br>And based on gysmo38 repo: https://github.com/gysmo38/mitsubishi2MQTT

### Features
 - Initial config:  WIFI AP mode and web portal
 - Web interface for configuration, status and control, firmware upgrade
 - Wirenboard-compatible MQTT topics

### Hardware
 - Mitsubishi AC unit (tested on ...)
 - WeMos D1 mini
 - CN105 Cable (https://nl.aliexpress.com/item/1005003232354177.html select 5P option)

Demo Circuit
<br><img src="https://github.com/SwiCago/HeatPump/blob/master/CN105_ESP8266.png"/>

### Screenshots

Solarized dark             |  Solarized Ocean | Solarized Ocean
:-------------------------:|:-------------------------:|:-------------------------:
![](https://github.com/gysmo38/mitsubishi2MQTT/blob/master/images/main_page.png)  |  ![](https://github.com/gysmo38/mitsubishi2MQTT/blob/master/images/control_page.png) | ![](https://github.com/gysmo38/mitsubishi2MQTT/blob/master/images/config_page.png)

Web-interface
![](https://github.com/gysmo38/mitsubishi2MQTT/blob/master/images/main_page.png) ![](https://github.com/gysmo38/mitsubishi2MQTT/blob/master/images/control_page.png) ![](https://github.com/gysmo38/mitsubishi2MQTT/blob/master/images/config_page.png)

Wirenboard integration
![](https://github.com/gysmo38/mitsubishi2MQTT/blob/master/images/main_page.png) ![](https://github.com/gysmo38/mitsubishi2MQTT/blob/master/images/control_page.png) ![](https://github.com/gysmo38/mitsubishi2MQTT/blob/master/images/config_page.png)


***
How to use:
 - Step 1: flash the sketch with flash size include SPIFFS option.
 - Step 2: connect to device AP with name HVAC_XXXX (XXXX last 4 character MAC address)
 - Step 3: You should be automatically redirected to the web portal or go to 192.168.1.1
 - Step 4: set Wifi information, save & reboot. Fall back to AP mode if WiFi connection fails (AP password sets to default SSID name from step 2).
 - Step 5: find the device IP with last 4 character MAC address in your router
 - Step 6: (optional): Set MQTT information for use with Home Assistant
 - Step 7: (optional): Set Login password to prevent unwanted access in SETUP->ADVANCE->Login Password
