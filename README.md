# mitsubishi2wb
Use MQTT and ESP8266/ESP32 module to control Mitsubishi HVAC unit.
<br>It use SwiCago librairies: https://github.com/SwiCago/HeatPump
<br>And based on gysmo38 repo: https://github.com/gysmo38/mitsubishi2MQTT

### Features
 - Initial config:  WIFI AP mode and web portal
 - Web interface for configuration, status and control, firmware upgrade
 - Wirenboard-compatible MQTT topics

### Hardware
 - Mitsubishi AC unit (tested on MSZ-SF15VA, MSZ-SF20VA, MSZ-SF42VE, see full supported models list on [SwiCago wiki](https://github.com/SwiCago/HeatPump/wiki/Supported-models))
 - WeMos D1 Mini or any other ESP8266 board capable to handle 5V power source
 - Mitsubishi CN105 Cable (or analogue, i.e. https://nl.aliexpress.com/item/1005003232354177.html select 5P option)
 - Solder 5V (brown) and ground (orange) pinouts to your D1 Mini.
Main Page | Control Page
![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/Wemos_D1_Solder1.jpg)  |  ![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/Wemos_D1_Solder2.jpg)

### Screenshots

Web-interface
Main Page | Control Page | Config Page
:--:|:--:|:--:
![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/main_page.png)  |  ![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/control_page.png) | ![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/config_page.png)

Wirenboard integration
Main Page | Control Page | Config Page
:--:|:--:|:--:
![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/main_page.png)  |  ![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/control_page.png) | ![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/config_page.png)


***
How to use:
 - Step 1: flash the sketch with flash size include SPIFFS option.
 - Step 2: connect to device AP with name HVAC_XXXX (XXXX last 4 character MAC address)
 - Step 3: You should be automatically redirected to the web portal or go to 192.168.1.1
 - Step 4: set Wifi information, save & reboot. Fall back to AP mode if WiFi connection fails (AP password sets to default SSID name from step 2).
 - Step 5: find the device IP with last 4 character MAC address in your router
 - Step 6: (optional): Set MQTT information for use with Wirenboard
