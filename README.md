# mitsubishi2wb
Use MQTT and ESP8266/ESP32 module to control Mitsubishi HVAC unit.
<br>based on SwiCago HeatPump library: https://github.com/SwiCago/HeatPump
<br>and inspired by gysmo38 HA-integration: https://github.com/gysmo38/mitsubishi2MQTT

### Features
 - Initial config:  WIFI AP mode and web portal
 - Web interface for configuration, status and control, firmware upgrade
 - Wirenboard-compatible MQTT topics

### Hardware
 - Mitsubishi AC unit (tested on MSZ-SF15VA, MSZ-SF20VA, MSZ-SF42VE, see full supported models list at [SwiCago wiki](https://github.com/SwiCago/HeatPump/wiki/Supported-models))
 - WeMos D1 Mini or any other ESP32/ESP8266 board capable to handle 5V power source
 - Mitsubishi CN105 Cable (or analogue, i.e. https://nl.aliexpress.com/item/1005003232354177.html). CN105 pins order is: [12V, GND, 5V, Tx, Rx]
 - Solder GND, 5V, Tx and Rx pinouts to your D1 Mini. Note that Rx of AC unit should be connected to Tx of the board. [pic1](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/Wemos_D1_Solder1.jpg), [pic2](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/Wemos_D1_Solder2.jpg), [HiRes](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/wemosd1-hires.jpg)
 - Full installation guide can be found here: https://chrdavis.github.io/hacking-a-mitsubishi-heat-pump-Part-1/

### Screenshots
Web-interface
Main Page | Control Page | Config Page
:--:|:--:|:--:
![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/main_page.png)  |  ![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/control_page.png) | ![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/config_page.png)

Wirenboard integration
WB-device | WB mqtt topics
:--:|:--:
![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/wb_main_page.png)  |  ![](https://github.com/mavlyutov/mitsubishi2wb/blob/master/images/wb_topics.png)


### Quick start:
 - Step 1: flash the sketch with flash size include SPIFFS option.
 - Step 2: connect to device AP with name HVAC_XXXX (XXXX last 4 character MAC address)
 - Step 3: You should be automatically redirected to the web portal or go to 192.168.1.1
 - Step 4: set Wifi information, save & reboot. Hvac will fall back to AP mode if WiFi connection fails (AP password sets to default SSID name from step 2).
 - Step 5: find the device IP with last 4 character MAC address in your router
 - Step 6: (optional): Set MQTT credentials for use with Wirenboard


### WB ac terms
Num | Mode | Fan | Wane | Widevane
:--:|:--:|:--:|:--:|:--:
0 | Auto | Auto | Auto | Swing
1 | Dry | Quiet | Swing | <<
2 | Cool | 1 | 1 | <
3 | Heat | 2 | 2 | |
4 | Fan | 3 | 3 | >
5 | - | 4 | 4 | >>
6 | - | - | 5 | <>