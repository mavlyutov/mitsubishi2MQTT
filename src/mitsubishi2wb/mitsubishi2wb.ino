
#include "FS.h"                   // SPIFFS for store config

#ifdef ESP32
    #include <WiFi.h>             // WIFI for ESP32
    #include <WiFiUdp.h>
    #include <ESPmDNS.h>          // mDNS for ESP32
    #include <WebServer.h>        // webServer for ESP32
    #include "SPIFFS.h"           // ESP32 SPIFFS for store config
    WebServer server(80);         // ESP32 web
#else
    #include <ESP8266WiFi.h>      // WIFI for ESP8266
    #include <WiFiClient.h>
    #include <ESP8266mDNS.h>      // mDNS for ESP8266
    #include <ESP8266WebServer.h> // webServer for ESP8266
    ESP8266WebServer server(80);  // ESP8266 web
#endif

#include <ArduinoJson.h>          // json to process MQTT: ArduinoJson 6.21.3
#include <PubSubClient.h>         // MQTT: PubSubClient 2.8.0
#include <DNSServer.h>            // DNS for captive portal
#include <ArduinoOTA.h>           // for OTA
#include <HeatPump.h>             // SwiCago library: https://github.com/SwiCago/HeatPump
#include "config.h"               // config file
#include "html_common.h"          // common code HTML (like header, footer)
#include "javascript_common.h"    // common code javascript (like refresh page)
#include "html_init.h"            // code html for initial config
#include "html_menu.h"            // code html for menu
#include "html_pages.h"           // code html for pages

// Languages
#ifndef MY_LANGUAGE
    #include "languages/en-GB.h"  // default language is English
#else
    #define QUOTEME(x) QUOTEME_1(x)
    #define QUOTEME_1(x) #x
    #define INCLUDE_FILE(x) QUOTEME(languages/x.h)
    #include INCLUDE_FILE(MY_LANGUAGE)
#endif

// wifi, mqtt and heatpump client instances
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

// Captive portal variables, only used for config page
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
IPAddress netMsk(255, 255, 255, 0);
DNSServer dnsServer;

boolean captive = false;
boolean mqtt_config = false;
boolean wifi_config = false;

// HVAC
HeatPump hp;
unsigned long lastTempSend;
unsigned long lastMqttRetry;
unsigned long lastHpSync;
unsigned int hpConnectionRetries;

// Local state
StaticJsonDocument<JSON_OBJECT_SIZE(7)> rootInfo;

// Web OTA
int uploaderror = 0;

void setup() {
    // Start serial for debug before HVAC connect to serial
    Serial.begin(115200);
    // Serial.println(F("Starting mitsubishi2wb"));
    // Mount SPIFFS filesystem
    if (SPIFFS.begin()) {
        // Serial.println(F("Mounted file system"));
    } else {
        // Serial.println(F("Failed to mount FS -> formating"));
        SPIFFS.format();
        // if (SPIFFS.begin())
        // Serial.println(F("Mounted file system after formating"));
    }
    // set led pin as output
    pinMode(blueLedPin, OUTPUT);

    // Define hostname
    hostname += hostnamePrefix;
    hostname += getId();
    mqtt_client_id = hostname;
#ifdef ESP32
    WiFi.setHostname(hostname.c_str());
#else
    WiFi.hostname(hostname.c_str());
#endif

    wifi_config_exists = loadWifi();
    loadUnit();
    if (initWifi()) {
        if (SPIFFS.exists(console_file)) {
            SPIFFS.remove(console_file);
        }
        // Web interface
        server.on("/", handleRoot);
        server.on("/control", handleControl);
        server.on("/setup", handleSetup);
        server.on("/mqtt", handleMqtt);
        server.on("/wifi", handleWifi);
        server.on("/unit", handleUnit);
        server.on("/status", handleStatus);
        server.on("/upgrade", handleUpgrade);
        server.on("/upload", HTTP_POST, handleUploadDone, handleUploadLoop);
        server.onNotFound(handleNotFound);

        server.begin();
        lastMqttRetry = 0;
        lastHpSync = 0;
        hpConnectionRetries = 0;
        if (loadMqtt()) {
            String deviceName = hostname;
            deviceName.toLowerCase();

            main_topic      = "/devices/" + deviceName + "/meta";
            power_topic     = "/devices/" + deviceName + "/controls/power"; power_set_topic = power_topic + "/on";
            mode_topic      = "/devices/" + deviceName + "/controls/mode"; mode_set_topic = mode_topic + "/on";
            fan_topic       = "/devices/" + deviceName + "/controls/fan"; fan_set_topic = fan_topic + "/on";
            vane_topic      = "/devices/" + deviceName + "/controls/vane"; vane_set_topic = vane_topic + "/on";
            widevane_topic  = "/devices/" + deviceName + "/controls/widevane"; widevane_set_topic = widevane_topic + "/on";
            temp_topic      = "/devices/" + deviceName + "/controls/temperature"; temp_set_topic = temp_topic + "/on";
            room_temp_topic = "/devices/" + deviceName + "/controls/room_temperature";

            // startup mqtt connection
            initMqtt();
        }
        hp.setSettingsChangedCallback(hpSettingsChanged);
        hp.setStatusChangedCallback(hpStatusChanged);
        hp.enableExternalUpdate();  // Allow Remote/Panel
        hp.connect(&Serial);

        readHeatPump();
        lastTempSend = millis();
    } else {
        dnsServer.start(DNS_PORT, "*", apIP);
        initCaptivePortal();
    }
    initOTA();
}

bool loadWifi() {
    ap_ssid = "";
    ap_pwd = "";
    if (!SPIFFS.exists(wifi_conf)) {
        // Serial.println(F("Wifi config file not exist!"));
        return false;
    }
    File configFile = SPIFFS.open(wifi_conf, "r");
    if (!configFile) {
        // Serial.println(F("Failed to open wifi config file"));
        return false;
    }
    size_t size = configFile.size();
    if (size > 1024) {
        // Serial.println(F("Wifi config file size is too large"));
        return false;
    }

    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    const size_t capacity = JSON_OBJECT_SIZE(4) + 130;
    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, buf.get());
    hostname = doc["hostname"].as<String>();
    ap_ssid = doc["ap_ssid"].as<String>();
    ap_pwd = doc["ap_pwd"].as<String>();
    // prevent ota password is "null" if not exist key
    if (doc.containsKey("ota_pwd")) {
        ota_pwd = doc["ota_pwd"].as<String>();
    } else {
        ota_pwd = "";
    }
    return true;
}

void saveMqtt(String mqttHost, String mqttPort, String mqttUser, String mqttPwd) {
    const size_t capacity = JSON_OBJECT_SIZE(4) + 400;
    DynamicJsonDocument doc(capacity);
    // if mqtt port is empty, we use default port
    if (mqttPort[0] == '\0') mqttPort = "1883";
    doc["mqtt_host"] = mqttHost;
    doc["mqtt_port"] = mqttPort;
    doc["mqtt_user"] = mqttUser;
    doc["mqtt_pwd"] = mqttPwd;
    File configFile = SPIFFS.open(mqtt_conf, "w");
    serializeJson(doc, configFile);
    configFile.close();
}

void saveUnit(String supportMode, String minTemp, String maxTemp, String tempStep) {
    const size_t capacity = JSON_OBJECT_SIZE(5) + 200;
    DynamicJsonDocument doc(capacity);
    // if minTemp is empty, we use default 16
    if (minTemp.isEmpty()) minTemp = 16;
    doc["min_temp"] = minTemp;
    // if maxTemp is empty, we use default 31
    if (maxTemp.isEmpty()) maxTemp = 31;
    doc["max_temp"] = maxTemp;
    // if tempStep is empty, we use default 1
    if (tempStep.isEmpty()) tempStep = 1.00;
    doc["temp_step"] = tempStep;
    // if support mode is empty, we use default all mode
    if (supportMode.isEmpty()) supportMode = "all";
    doc["support_mode"] = supportMode;

    File configFile = SPIFFS.open(unit_conf, "w");
    serializeJson(doc, configFile);
    configFile.close();
}

void saveWifi(String apSsid, String apPwd, String hostName, String otaPwd) {
    const size_t capacity = JSON_OBJECT_SIZE(4) + 130;
    DynamicJsonDocument doc(capacity);
    doc["ap_ssid"] = apSsid;
    doc["ap_pwd"] = apPwd;
    doc["hostname"] = hostName;
    doc["ota_pwd"] = otaPwd;
    File configFile = SPIFFS.open(wifi_conf, "w");
    serializeJson(doc, configFile);
    delay(10);
    configFile.close();
}

// Initialize captive portal page
void initCaptivePortal() {
    // Serial.println(F("Starting captive portal"));
    server.on("/", handleInitSetup);
    server.on("/save", handleSaveWifi);
    server.on("/reboot", handleReboot);
    server.onNotFound(handleNotFound);
    server.begin();
    captive = true;
}

void initMqtt() {
    mqtt_client.setServer(mqtt_server.c_str(), atoi(mqtt_port.c_str()));
    mqtt_client.setCallback(mqttCallback);
    mqttConnect();
}

// Enable OTA only when connected as a client.
void initOTA() {
    // write_log("Start OTA Listener");
    ArduinoOTA.setHostname(hostname.c_str());
    if (ota_pwd.length() > 0) {
        ArduinoOTA.setPassword(ota_pwd.c_str());
    }
    ArduinoOTA.begin();
}

bool loadMqtt() {
    if (!SPIFFS.exists(mqtt_conf)) {
        Serial.println(F("MQTT config file not exist!"));
        return false;
    }
    // write_log("Loading MQTT configuration");
    File configFile = SPIFFS.open(mqtt_conf, "r");
    if (!configFile) {
        // write_log("Failed to open MQTT config file");
        return false;
    }

    size_t size = configFile.size();
    if (size > 1024) {
        // write_log("Config file size is too large");
        return false;
    }
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    const size_t capacity = JSON_OBJECT_SIZE(4) + 400;
    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, buf.get());
    mqtt_server = doc["mqtt_host"].as<String>();
    mqtt_port = doc["mqtt_port"].as<String>();
    mqtt_username = doc["mqtt_user"].as<String>();
    mqtt_password = doc["mqtt_pwd"].as<String>();

    mqtt_config = true;
    return true;
}

bool loadUnit() {
    if (!SPIFFS.exists(unit_conf)) {
        // Serial.println(F("Unit config file not exist!"));
        return false;
    }
    File configFile = SPIFFS.open(unit_conf, "r");
    if (!configFile) {
        return false;
    }

    size_t size = configFile.size();
    if (size > 1024) {
        return false;
    }
    std::unique_ptr<char[]> buf(new char[size]);

    configFile.readBytes(buf.get(), size);
    const size_t capacity = JSON_OBJECT_SIZE(3) + 200;
    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, buf.get());
    // unit
    min_temp = doc["min_temp"].as<float>();
    max_temp = doc["max_temp"].as<float>();
    temp_step = doc["temp_step"].as<String>();
    // mode
    String supportMode = doc["support_mode"].as<String>();
    if (supportMode == "nht") supportHeatMode = false;
    return true;
}

boolean initWifi() {
    bool connectWifiSuccess = true;
    if (ap_ssid[0] != '\0') {
        connectWifiSuccess = wifi_config = connectWifi();
        if (connectWifiSuccess) {
            return true;
        } else {
            // reset hostname back to default before starting AP mode for privacy
            hostname = hostnamePrefix;
            hostname += getId();
        }
    }

    WiFi.mode(WIFI_AP);
    wifi_timeout = millis() + WIFI_RETRY_INTERVAL_MS;
    WiFi.persistent(false);  //fix crash esp32 https://github.com/espressif/arduino-esp32/issues/2025
    WiFi.softAPConfig(apIP, apIP, netMsk);
    WiFi.softAP(hostname.c_str());
    delay(2000); // VERY IMPORTANT
    wifi_config = false;
    return false;
}

// Handler webserver response

void sendWrappedHTML(String content) {
    String headerContent = FPSTR(html_common_header);
    String footerContent = FPSTR(html_common_footer);
    String toSend = headerContent + content + footerContent;
    toSend.replace(F("_UNIT_NAME_"), hostname);
    toSend.replace(F("_VERSION_"), m2wb_version);
    server.send(200, F("text/html"), toSend);
}

void handleNotFound() {
    if (captive) {
        String initSetupContent = FPSTR(html_init_setup);
        initSetupContent.replace("_TXT_INIT_TITLE_", FPSTR(txt_init_title));
        initSetupContent.replace("_TXT_INIT_HOST_", FPSTR(txt_wifi_hostname));
        initSetupContent.replace("_UNIT_NAME_", hostname);
        initSetupContent.replace("_TXT_INIT_SSID_", FPSTR(txt_wifi_SSID));
        initSetupContent.replace("_TXT_INIT_PSK_", FPSTR(txt_wifi_psk));
        initSetupContent.replace("_TXT_INIT_OTA_", FPSTR(txt_wifi_otap));
        initSetupContent.replace("_TXT_SAVE_", FPSTR(txt_save));
        initSetupContent.replace("_TXT_REBOOT_", FPSTR(txt_reboot));

        sendWrappedHTML(initSetupContent);
    } else {
        server.sendHeader("Location", "/");
        server.sendHeader("Cache-Control", "no-cache");
        server.send(302);
        return;
    }
}

void handleSaveWifi() {
    // Serial.println(F("Saving wifi config"));
    if (server.method() == HTTP_POST) {
        saveWifi(server.arg("ssid"), server.arg("psk"), server.arg("hn"), server.arg("otapwd"));
    }
    String initSavePage = FPSTR(html_init_save);
    initSavePage.replace("_TXT_INIT_REBOOT_MESS_", FPSTR(txt_init_reboot_mes));
    sendWrappedHTML(initSavePage);
    delay(500);
    ESP.restart();
}

void handleReboot() {
    String initRebootPage = FPSTR(html_init_reboot);
    initRebootPage.replace("_TXT_INIT_REBOOT_", FPSTR(txt_init_reboot));
    sendWrappedHTML(initRebootPage);
    delay(500);
    ESP.restart();
}

void handleRoot() {
    if (server.hasArg("REBOOT")) {
        String rebootPage = FPSTR(html_page_reboot);
        String countDown = FPSTR(count_down_script);
        rebootPage.replace("_TXT_M_REBOOT_", FPSTR(txt_m_reboot));
        sendWrappedHTML(rebootPage + countDown);
        delay(500);
#ifdef ESP32
        ESP.restart();
#else
        ESP.reset();
#endif
    } else {
        String menuRootPage = FPSTR(html_menu_root);
        // not show control button if hp not connected
        menuRootPage.replace("_SHOW_CONTROL_", (String)(hp.isConnected()));
        menuRootPage.replace("_TXT_CONTROL_", FPSTR(txt_control));
        menuRootPage.replace("_TXT_SETUP_", FPSTR(txt_setup));
        menuRootPage.replace("_TXT_STATUS_", FPSTR(txt_status));
        menuRootPage.replace("_TXT_FW_UPGRADE_", FPSTR(txt_firmware_upgrade));
        menuRootPage.replace("_TXT_REBOOT_", FPSTR(txt_reboot));
        menuRootPage.replace("_TXT_LOGOUT_", FPSTR(txt_logout));
        sendWrappedHTML(menuRootPage);
    }
}

void handleInitSetup() {
    String initSetupPage = FPSTR(html_init_setup);
    initSetupPage.replace("_TXT_INIT_TITLE_", FPSTR(txt_init_title));
    initSetupPage.replace("_TXT_INIT_HOST_", FPSTR(txt_wifi_hostname));
    initSetupPage.replace("_TXT_INIT_SSID_", FPSTR(txt_wifi_SSID));
    initSetupPage.replace("_TXT_INIT_PSK_", FPSTR(txt_wifi_psk));
    initSetupPage.replace("_TXT_INIT_OTA_", FPSTR(txt_wifi_otap));
    initSetupPage.replace("_TXT_SAVE_", FPSTR(txt_save));
    initSetupPage.replace("_TXT_REBOOT_", FPSTR(txt_reboot));
    sendWrappedHTML(initSetupPage);
}

void handleSetup() {
    if (server.hasArg("RESET")) {
        String pageReset = FPSTR(html_page_reset);
        String ssid = hostnamePrefix;
        ssid += getId();
        pageReset.replace("_TXT_M_RESET_", FPSTR(txt_m_reset));
        pageReset.replace("_SSID_", ssid);
        sendWrappedHTML(pageReset);
        SPIFFS.format();
        delay(500);
#ifdef ESP32
        ESP.restart();
#else
        ESP.reset();
#endif
    } else {
        String menuSetupPage = FPSTR(html_menu_setup);
        menuSetupPage.replace("_TXT_MQTT_", FPSTR(txt_MQTT));
        menuSetupPage.replace("_TXT_WIFI_", FPSTR(txt_WIFI));
        menuSetupPage.replace("_TXT_UNIT_", FPSTR(txt_unit));
        menuSetupPage.replace("_TXT_RESET_", FPSTR(txt_reset));
        menuSetupPage.replace("_TXT_BACK_", FPSTR(txt_back));
        menuSetupPage.replace("_TXT_RESETCONFIRM_", FPSTR(txt_reset_confirm));
        sendWrappedHTML(menuSetupPage);
    }
}

void rebootAndSendPage() {
    String saveRebootPage = FPSTR(html_page_save_reboot);
    String countDown = FPSTR(count_down_script);
    saveRebootPage.replace("_TXT_M_SAVE_", FPSTR(txt_m_save));
    sendWrappedHTML(saveRebootPage + countDown);
    delay(500);
    ESP.restart();
}

void handleMqtt() {
    if (server.method() == HTTP_POST) {
        saveMqtt(server.arg("mh"), server.arg("ml"), server.arg("mu"), server.arg("mp"));
        rebootAndSendPage();
    } else {
        String mqttPage = FPSTR(html_page_mqtt);
        mqttPage.replace("_TXT_SAVE_", FPSTR(txt_save));
        mqttPage.replace("_TXT_BACK_", FPSTR(txt_back));
        mqttPage.replace("_TXT_MQTT_TITLE_", FPSTR(txt_mqtt_title));
        mqttPage.replace("_TXT_MQTT_HOST_", FPSTR(txt_mqtt_host));
        mqttPage.replace("_TXT_MQTT_PORT_", FPSTR(txt_mqtt_port));
        mqttPage.replace("_TXT_MQTT_USER_", FPSTR(txt_mqtt_user));
        mqttPage.replace("_TXT_MQTT_PASSWORD_", FPSTR(txt_mqtt_password));
        mqttPage.replace(F("_MQTT_HOST_"), mqtt_server);
        mqttPage.replace(F("_MQTT_PORT_"), String(mqtt_port));
        mqttPage.replace(F("_MQTT_USER_"), mqtt_username);
        mqttPage.replace(F("_MQTT_PASSWORD_"), mqtt_password);
        sendWrappedHTML(mqttPage);
    }
}

void handleUnit() {
    if (server.method() == HTTP_POST) {
        saveUnit(server.arg("md"), server.arg("min_temp"), server.arg("max_temp"), server.arg("temp_step"));
        rebootAndSendPage();
    } else {
        String unitPage = FPSTR(html_page_unit);
        unitPage.replace("_TXT_SAVE_", FPSTR(txt_save));
        unitPage.replace("_TXT_BACK_", FPSTR(txt_back));
        unitPage.replace("_TXT_UNIT_TITLE_", FPSTR(txt_unit_title));
        unitPage.replace("_TXT_UNIT_MINTEMP_", FPSTR(txt_unit_mintemp));
        unitPage.replace("_TXT_UNIT_MAXTEMP_", FPSTR(txt_unit_maxtemp));
        unitPage.replace("_TXT_UNIT_STEPTEMP_", FPSTR(txt_unit_steptemp));
        unitPage.replace("_TXT_UNIT_MODES_", FPSTR(txt_unit_modes));
        unitPage.replace("_TXT_F_ALLMODES_", FPSTR(txt_f_allmodes));
        unitPage.replace("_TXT_F_NOHEAT_", FPSTR(txt_f_noheat));
        unitPage.replace(F("_MIN_TEMP_"), String(min_temp));
        unitPage.replace(F("_MAX_TEMP_"), String(max_temp));
        unitPage.replace(F("_TEMP_STEP_"), String(temp_step));
        //mode
        if (supportHeatMode) unitPage.replace(F("_MD_ALL_"), F("selected"));
        else unitPage.replace(F("_MD_NONHEAT_"), F("selected"));
        sendWrappedHTML(unitPage);
    }
}

void handleWifi() {
    if (server.method() == HTTP_POST) {
        saveWifi(server.arg("ssid"), server.arg("psk"), server.arg("hn"), server.arg("otapwd"));
        rebootAndSendPage();
#ifdef ESP32
        ESP.restart();
#else
        ESP.reset();
#endif
    } else {
        String wifiPage = FPSTR(html_page_wifi);
        wifiPage.replace("_TXT_SAVE_", FPSTR(txt_save));
        wifiPage.replace("_TXT_BACK_", FPSTR(txt_back));
        wifiPage.replace("_TXT_WIFI_TITLE_", FPSTR(txt_wifi_title));
        wifiPage.replace("_TXT_WIFI_HOST_", FPSTR(txt_wifi_hostname));
        wifiPage.replace("_TXT_WIFI_SSID_", FPSTR(txt_wifi_SSID));
        wifiPage.replace("_TXT_WIFI_PSK_", FPSTR(txt_wifi_psk));
        wifiPage.replace("_TXT_WIFI_OTAP_", FPSTR(txt_wifi_otap));
        wifiPage.replace(F("_SSID_"), ap_ssid);
        wifiPage.replace(F("_PSK_"), ap_pwd);
        wifiPage.replace(F("_OTA_PWD_"), ota_pwd);
        sendWrappedHTML(wifiPage);
    }
}

void handleStatus() {
    String statusPage = FPSTR(html_page_status);
    statusPage.replace("_TXT_BACK_", FPSTR(txt_back));
    statusPage.replace("_TXT_STATUS_TITLE_", FPSTR(txt_status_title));
    statusPage.replace("_TXT_STATUS_HVAC_", FPSTR(txt_status_hvac));
    statusPage.replace("_TXT_STATUS_MQTT_", FPSTR(txt_status_mqtt));
    statusPage.replace("_TXT_STATUS_WIFI_", FPSTR(txt_status_wifi));

    if (server.hasArg("mrconn")) mqttConnect();

    String connected = F("<span style='color:#47c266'><b>");
    connected += FPSTR(txt_status_connect);
    connected += F("</b><span>");

    String disconnected = F("<span style='color:#d43535'><b>");
    disconnected += FPSTR(txt_status_disconnect);
    disconnected += F("</b></span>");

    if ((Serial) and hp.isConnected()) statusPage.replace(F("_HVAC_STATUS_"), connected);
    else statusPage.replace(F("_HVAC_STATUS_"), disconnected);
    if (mqtt_client.connected()) statusPage.replace(F("_MQTT_STATUS_"), connected);
    else statusPage.replace(F("_MQTT_STATUS_"), disconnected);
    statusPage.replace(F("_MQTT_REASON_"), String(mqtt_client.state()));
    statusPage.replace(F("_WIFI_STATUS_"), String(WiFi.RSSI()));
    sendWrappedHTML(statusPage);
}

void handleControl() {
    // not connected to hp, redirect to status page
    if (!hp.isConnected()) {
        server.sendHeader("Location", "/status");
        server.sendHeader("Cache-Control", "no-cache");
        server.send(302);
        return;
    }
    heatpumpSettings settings = hp.getSettings();
    settings = change_states(settings);

    String controlPage = FPSTR(html_page_control);
    String headerContent = FPSTR(html_common_header);
    String footerContent = FPSTR(html_common_footer);
    // write_log("Enter HVAC control");
    headerContent.replace("_UNIT_NAME_", hostname);
    footerContent.replace("_VERSION_", m2wb_version);
    controlPage.replace("_TXT_BACK_", FPSTR(txt_back));
    controlPage.replace("_UNIT_NAME_", hostname);
    controlPage.replace("_RATE_", "60");
    controlPage.replace("_ROOMTEMP_", String(hp.getRoomTemperature()));
    controlPage.replace("_HEAT_MODE_SUPPORT_", (String)supportHeatMode);
    controlPage.replace(F("_MIN_TEMP_"), String(min_temp));
    controlPage.replace(F("_MAX_TEMP_"), String(max_temp));
    controlPage.replace(F("_TEMP_STEP_"), String(temp_step));
    controlPage.replace("_TXT_CTRL_CTEMP_", FPSTR(txt_ctrl_ctemp));
    controlPage.replace("_TXT_CTRL_TEMP_", FPSTR(txt_ctrl_temp));
    controlPage.replace("_TXT_CTRL_TITLE_", FPSTR(txt_ctrl_title));
    controlPage.replace("_TXT_CTRL_POWER_", FPSTR(txt_ctrl_power));
    controlPage.replace("_TXT_CTRL_MODE_", FPSTR(txt_ctrl_mode));
    controlPage.replace("_TXT_CTRL_FAN_", FPSTR(txt_ctrl_fan));
    controlPage.replace("_TXT_CTRL_VANE_", FPSTR(txt_ctrl_vane));
    controlPage.replace("_TXT_CTRL_WVANE_", FPSTR(txt_ctrl_wvane));
    controlPage.replace("_TXT_F_ON_", FPSTR(txt_f_on));
    controlPage.replace("_TXT_F_OFF_", FPSTR(txt_f_off));
    controlPage.replace("_TXT_F_AUTO_", FPSTR(txt_f_auto));
    controlPage.replace("_TXT_F_HEAT_", FPSTR(txt_f_heat));
    controlPage.replace("_TXT_F_DRY_", FPSTR(txt_f_dry));
    controlPage.replace("_TXT_F_COOL_", FPSTR(txt_f_cool));
    controlPage.replace("_TXT_F_FAN_", FPSTR(txt_f_fan));
    controlPage.replace("_TXT_F_QUIET_", FPSTR(txt_f_quiet));
    controlPage.replace("_TXT_F_SPEED_", FPSTR(txt_f_speed));
    controlPage.replace("_TXT_F_SWING_", FPSTR(txt_f_swing));
    controlPage.replace("_TXT_F_POS_", FPSTR(txt_f_pos));

    if (strcmp(settings.power, "ON") == 0) {
        controlPage.replace("_POWER_ON_", "selected");
    } else if (strcmp(settings.power, "OFF") == 0) {
        controlPage.replace("_POWER_OFF_", "selected");
    }

    if (strcmp(settings.mode, "HEAT") == 0) {
        controlPage.replace("_MODE_H_", "selected");
    } else if (strcmp(settings.mode, "DRY") == 0) {
        controlPage.replace("_MODE_D_", "selected");
    } else if (strcmp(settings.mode, "COOL") == 0) {
        controlPage.replace("_MODE_C_", "selected");
    } else if (strcmp(settings.mode, "FAN") == 0) {
        controlPage.replace("_MODE_F_", "selected");
    } else if (strcmp(settings.mode, "AUTO") == 0) {
        controlPage.replace("_MODE_A_", "selected");
    }

    if (strcmp(settings.fan, "AUTO") == 0) {
        controlPage.replace("_FAN_A_", "selected");
    } else if (strcmp(settings.fan, "QUIET") == 0) {
        controlPage.replace("_FAN_Q_", "selected");
    } else if (strcmp(settings.fan, "1") == 0) {
        controlPage.replace("_FAN_1_", "selected");
    } else if (strcmp(settings.fan, "2") == 0) {
        controlPage.replace("_FAN_2_", "selected");
    } else if (strcmp(settings.fan, "3") == 0) {
        controlPage.replace("_FAN_3_", "selected");
    } else if (strcmp(settings.fan, "4") == 0) {
        controlPage.replace("_FAN_4_", "selected");
    }

    controlPage.replace("_VANE_V_", settings.vane);
    if (strcmp(settings.vane, "AUTO") == 0) {
        controlPage.replace("_VANE_A_", "selected");
    } else if (strcmp(settings.vane, "1") == 0) {
        controlPage.replace("_VANE_1_", "selected");
    } else if (strcmp(settings.vane, "2") == 0) {
        controlPage.replace("_VANE_2_", "selected");
    } else if (strcmp(settings.vane, "3") == 0) {
        controlPage.replace("_VANE_3_", "selected");
    } else if (strcmp(settings.vane, "4") == 0) {
        controlPage.replace("_VANE_4_", "selected");
    } else if (strcmp(settings.vane, "5") == 0) {
        controlPage.replace("_VANE_5_", "selected");
    } else if (strcmp(settings.vane, "SWING") == 0) {
        controlPage.replace("_VANE_S_", "selected");
    }

    controlPage.replace("_WIDEVANE_V_", settings.wideVane);
    if (strcmp(settings.wideVane, "<<") == 0) {
        controlPage.replace("_WVANE_1_", "selected");
    } else if (strcmp(settings.wideVane, "<") == 0) {
        controlPage.replace("_WVANE_2_", "selected");
    } else if (strcmp(settings.wideVane, "|") == 0) {
        controlPage.replace("_WVANE_3_", "selected");
    } else if (strcmp(settings.wideVane, ">") == 0) {
        controlPage.replace("_WVANE_4_", "selected");
    } else if (strcmp(settings.wideVane, ">>") == 0) {
        controlPage.replace("_WVANE_5_", "selected");
    } else if (strcmp(settings.wideVane, "<>") == 0) {
        controlPage.replace("_WVANE_6_", "selected");
    } else if (strcmp(settings.wideVane, "SWING") == 0) {
        controlPage.replace("_WVANE_S_", "selected");
    }
    controlPage.replace("_TEMP_", String(hp.getTemperature()));

    // We need to send the page content in chunks to overcome
    // a limitation on the maximum size we can send at one
    // time (approx 6k).
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", headerContent);
    server.sendContent(controlPage);
    server.sendContent(footerContent);
    // Signal the end of the content
    server.sendContent("");
    // delay(100);
}

void handleUpgrade() {
    uploaderror = 0;
    String upgradePage = FPSTR(html_page_upgrade);
    upgradePage.replace("_TXT_B_UPGRADE_", FPSTR(txt_upgrade));
    upgradePage.replace("_TXT_BACK_", FPSTR(txt_back));
    upgradePage.replace("_TXT_UPGRADE_TITLE_", FPSTR(txt_upgrade_title));
    upgradePage.replace("_TXT_UPGRADE_INFO_", FPSTR(txt_upgrade_info));
    upgradePage.replace("_TXT_UPGRADE_START_", FPSTR(txt_upgrade_start));

    sendWrappedHTML(upgradePage);
}

void handleUploadDone() {
    // Serial.printl(PSTR("HTTP: Firmware upload done"));
    bool restartflag = false;
    String uploadDonePage = FPSTR(html_page_upload);
    String content = F("<div style='text-align:center;'><b>Upload ");
    if (uploaderror) {
        content += F("<span style='color:#d43535'>failed</span></b><br/><br/>");
        if (uploaderror == 1) {
            content += FPSTR(txt_upload_nofile);
        } else if (uploaderror == 2) {
            content += FPSTR(txt_upload_filetoolarge);
        } else if (uploaderror == 3) {
            content += FPSTR(txt_upload_fileheader);
        } else if (uploaderror == 4) {
            content += FPSTR(txt_upload_flashsize);
        } else if (uploaderror == 5) {
            content += FPSTR(txt_upload_buffer);
        } else if (uploaderror == 6) {
            content += FPSTR(txt_upload_failed);
        } else if (uploaderror == 7) {
            content += FPSTR(txt_upload_aborted);
        } else {
            content += FPSTR(txt_upload_error);
            content += String(uploaderror);
        }
        if (Update.hasError()) {
            content += FPSTR(txt_upload_code);
            content += String(Update.getError());
        }
    } else {
        content += F("<span style='color:#47c266; font-weight: bold;'>");
        content += FPSTR(txt_upload_sucess);
        content += F("</span><br/><br/>");
        content += FPSTR(txt_upload_refresh);
        content += F("<span id='count'>10s</span>...");
        content += FPSTR(count_down_script);
        restartflag = true;
    }
    content += F("</div><br/>");
    uploadDonePage.replace("_UPLOAD_MSG_", content);
    uploadDonePage.replace("_TXT_BACK_", FPSTR(txt_back));
    sendWrappedHTML(uploadDonePage);
    if (restartflag) {
        delay(500);
#ifdef ESP32
        ESP.restart();
#else
        ESP.reset();
#endif
    }
}

void handleUploadLoop() {
    // Based on ESP8266HTTPUpdateServer.cpp uses ESP8266WebServer Parsing.cpp and Cores Updater.cpp (Update)
    // char log[200];
    if (uploaderror) {
        Update.end();
        return;
    }
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        if (upload.filename.c_str()[0] == 0) {
            uploaderror = 1;
            return;
        }
        // save cpu by disconnect/stop retry mqtt server
        if (mqtt_client.state() == MQTT_CONNECTED) {
            mqtt_client.disconnect();
            lastMqttRetry = millis();
        }
        // snprintf_P(log, sizeof(log), PSTR("Upload: File %s ..."), upload.filename.c_str());
        // Serial.printl(log);
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) {    //start with max available size
            //Update.printError(Serial);
            uploaderror = 2;
            return;
        }
    } else if (!uploaderror && (upload.status == UPLOAD_FILE_WRITE)) {
        if (upload.totalSize == 0) {
            if (upload.buf[0] != 0xE9) {
                // Serial.println(PSTR("Upload: File magic header does not start with 0xE9"));
                uploaderror = 3;
                return;
            }
            uint32_t bin_flash_size = ESP.magicFlashChipSize((upload.buf[3] & 0xf0) >> 4);
#ifdef ESP32
            if (bin_flash_size > ESP.getFlashChipSize()) {
#else
            if (bin_flash_size > ESP.getFlashChipRealSize()) {
#endif
                // Serial.printl(PSTR("Upload: File flash size is larger than device flash size"));
                uploaderror = 4;
                return;
            }
            if (ESP.getFlashChipMode() == 3) {
                upload.buf[2] = 3;    // DOUT - ESP8285
            } else {
                upload.buf[2] = 2;    // DIO - ESP8266
            }
        }
        if (!uploaderror && (Update.write(upload.buf, upload.currentSize) != upload.currentSize)) {
            // Update.printError(Serial);
            uploaderror = 5;
            return;
        }
    } else if (!uploaderror && (upload.status == UPLOAD_FILE_END)) {
        if (Update.end(true)) {    // true to set the size to the current progress
            // snprintf_P(log, sizeof(log), PSTR("Upload: Successful %u bytes. Restarting"), upload.totalSize);
            // Serial.printl(log)
        } else {
            //Update.printError(Serial);
            uploaderror = 6;
            return;
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        // Serial.println(PSTR("Upload: Update was aborted"));
        uploaderror = 7;
        Update.end();
    }
    delay(0);
}

void write_log(String log) {
    File logFile = SPIFFS.open(console_file, "a");
    logFile.println(log);
    logFile.close();
}

heatpumpSettings change_states(heatpumpSettings settings) {
    if (server.hasArg("CONNECT")) {
        hp.connect(&Serial);
    } else {
        bool update = false;
        if (server.hasArg("POWER")) {
            settings.power = server.arg("POWER").c_str();
            update = true;
        }
        if (server.hasArg("MODE")) {
            settings.mode = server.arg("MODE").c_str();
            update = true;
        }
        if (server.hasArg("TEMP")) {
            settings.temperature = server.arg("TEMP").toInt();
            update = true;
        }
        if (server.hasArg("FAN")) {
            settings.fan = server.arg("FAN").c_str();
            update = true;
        }
        if (server.hasArg("VANE")) {
            settings.vane = server.arg("VANE").c_str();
            update = true;
        }
        if (server.hasArg("WIDEVANE")) {
            settings.wideVane = server.arg("WIDEVANE").c_str();
            update = true;
        }
        if (update) {
            hp.setSettings(settings);
            hp.update();
        }
    }
    return settings;
}

void readHeatPumpStatus() {
    heatpumpStatus currentStatus = hp.getStatus();
    rootInfo["roomTemperature"] = currentStatus.roomTemperature;
}

void readHeatPumpSettings() {
    heatpumpSettings currentSettings = hp.getSettings();
    rootInfo["power"] = currentSettings.power;
    rootInfo["temperature"] = currentSettings.temperature;
    rootInfo["fan"] = currentSettings.fan;
    rootInfo["vane"] = currentSettings.vane;
    rootInfo["wideVane"] = currentSettings.wideVane;
    rootInfo["mode"] = currentSettings.mode;
}

void readHeatPump() {
    rootInfo.clear();
    readHeatPumpStatus();
    readHeatPumpSettings();
}

void hpSettingsChanged() {
    // send room temp, operating info and all information
    readHeatPumpSettings();

    if (rootInfo["power"] == "ON") {
        mqtt_client.publish(power_topic.c_str(), "1", true);
    } else if (rootInfo["power"] == "OFF") {
        mqtt_client.publish(power_topic.c_str(), "0", true);
    }

    mqtt_client.publish(temp_topic.c_str(), String(rootInfo["temperature"]).c_str(), true);

    if (rootInfo["fan"] == "AUTO") {
        mqtt_client.publish(fan_topic.c_str(), "0", true);
    } else if (rootInfo["fan"] == "QUIET") {
        mqtt_client.publish(fan_topic.c_str(), "1", true);
    } else if (rootInfo["fan"] == "1") {
        mqtt_client.publish(fan_topic.c_str(), "2", true);
    } else if (rootInfo["fan"] == "2") {
        mqtt_client.publish(fan_topic.c_str(), "3", true);
    } else if (rootInfo["fan"] == "3") {
        mqtt_client.publish(fan_topic.c_str(), "4", true);
    } else if (rootInfo["fan"] == "4") {
        mqtt_client.publish(fan_topic.c_str(), "5", true);
    }

    if (rootInfo["vane"] == "AUTO") {
        mqtt_client.publish(vane_topic.c_str(), "0", true);
    } else if (rootInfo["vane"] == "SWING") {
        mqtt_client.publish(vane_topic.c_str(), "1", true);
    } else if (rootInfo["vane"] == "1") {
        mqtt_client.publish(vane_topic.c_str(), "2", true);
    } else if (rootInfo["vane"] == "2") {
        mqtt_client.publish(vane_topic.c_str(), "3", true);
    } else if (rootInfo["vane"] == "3") {
        mqtt_client.publish(vane_topic.c_str(), "4", true);
    } else if (rootInfo["vane"] == "4") {
        mqtt_client.publish(vane_topic.c_str(), "5", true);
    } else if (rootInfo["vane"] == "5") {
        mqtt_client.publish(vane_topic.c_str(), "6", true);
    }

    if (rootInfo["wideVane"] == "SWING") {
        mqtt_client.publish(widevane_topic.c_str(), "0", true);
    } else if (rootInfo["wideVane"] == "<<") {
        mqtt_client.publish(widevane_topic.c_str(), "1", true);
    } else if (rootInfo["wideVane"] == "<") {
        mqtt_client.publish(widevane_topic.c_str(), "2", true);
    } else if (rootInfo["wideVane"] == "|") {
        mqtt_client.publish(widevane_topic.c_str(), "3", true);
    } else if (rootInfo["wideVane"] == ">") {
        mqtt_client.publish(widevane_topic.c_str(), "4", true);
    } else if (rootInfo["wideVane"] == ">>") {
        mqtt_client.publish(widevane_topic.c_str(), "5", true);
    } else if (rootInfo["wideVane"] == "<>") {
        mqtt_client.publish(widevane_topic.c_str(), "6", true);
    }

    if (rootInfo["mode"] == "AUTO") {
        mqtt_client.publish(mode_topic.c_str(), "0", true);
    } else if (rootInfo["mode"] == "DRY") {
        mqtt_client.publish(mode_topic.c_str(), "1", true);
    } else if (rootInfo["mode"] == "COOL") {
        mqtt_client.publish(mode_topic.c_str(), "2", true);
    } else if (rootInfo["mode"] == "HEAT") {
        mqtt_client.publish(mode_topic.c_str(), "3", true);
    } else if (rootInfo["mode"] == "FAN") {
        mqtt_client.publish(mode_topic.c_str(), "4", true);
    }

    hpStatusChanged(hp.getStatus());
}

void hpStatusChanged(heatpumpStatus currentStatus) {
    // only send the temperature every SEND_ROOM_TEMP_INTERVAL_MS
    if (millis() - lastTempSend > SEND_ROOM_TEMP_INTERVAL_MS) {
        rootInfo["roomTemperature"] = currentStatus.roomTemperature;

        mqtt_client.publish(room_temp_topic.c_str(), String(rootInfo["roomTemperature"]).c_str(), true);
        lastTempSend = millis();  //restart counter for waiting enough time for the unit to update before sending a state packet
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // Copy payload into message buffer
    // char message[length + 1];
    // for (unsigned int i = 0; i < length; i++) {
    //     message[i] = (char)payload[i];
    // }
    // message[length] = '\0';
    payload[length] = '\0';    // Null terminator used to terminate the char array
    String message = (char*)payload;

    if (strcmp(topic, power_set_topic.c_str()) == 0) {
        if (message == "0") {
            rootInfo["power"] = "OFF";
        } else if (message == "1") {
            rootInfo["power"] = "ON";
        } else {
            return;
        }

        const char* power = rootInfo["power"];
        hp.setPowerSetting(power);
        hp.update();
    } else if (strcmp(topic, mode_set_topic.c_str()) == 0) {
        if (message == "0") {
            rootInfo["mode"] = "AUTO";
        } else if (message == "1") {
            rootInfo["mode"] = "DRY";
        } else if (message == "2") {
            rootInfo["mode"] = "COOL";
        } else if (message == "3") {
            rootInfo["mode"] = "HEAT";
        } else if (message == "4") {
            rootInfo["mode"] = "FAN";
        } else {
            return;
        }

        const char* mode = rootInfo["mode"];
        hp.setModeSetting(mode);
        hp.update();
    } else if (strcmp(topic, fan_set_topic.c_str()) == 0) {
        if (message == "0") {
            rootInfo["fan"] = "AUTO";
        } else if (message == "1") {
            rootInfo["fan"] = "QUIET";
        } else if (message == "2") {
            rootInfo["fan"] = "1";
        } else if (message == "3") {
            rootInfo["fan"] = "2";
        } else if (message == "4") {
            rootInfo["fan"] = "3";
        } else if (message == "5") {
            rootInfo["fan"] = "4";
        } else {
            return;
        }
        hp.setFanSpeed(rootInfo["fan"]);
        hp.update();
    } else if (strcmp(topic, vane_set_topic.c_str()) == 0) {
        if (message == "0") {
            rootInfo["vane"] = "AUTO";
        } else if (message == "1") {
            rootInfo["vane"] = "SWING";
        } else if (message == "2") {
            rootInfo["vane"] = "1";
        } else if (message == "3") {
            rootInfo["vane"] = "2";
        } else if (message == "4") {
            rootInfo["vane"] = "3";
        } else if (message == "5") {
            rootInfo["vane"] = "4";
        } else if (message == "6") {
            rootInfo["vane"] = "5";
        } else {
            return;
        }
        hp.setVaneSetting(rootInfo["vane"]);
        hp.update();
    } else if (strcmp(topic, widevane_set_topic.c_str()) == 0) {
        if (message == "0") {
            rootInfo["wideVane"] = "SWING";
        } else if (message == "1") {
            rootInfo["wideVane"] = "<<";
        } else if (message == "2") {
            rootInfo["wideVane"] = "<";
        } else if (message == "3") {
            rootInfo["wideVane"] = "|";
        } else if (message == "4") {
            rootInfo["wideVane"] = ">";
        } else if (message == "5") {
            rootInfo["wideVane"] = ">>";
        } else if (message == "6") {
            rootInfo["wideVane"] = "<>";
        } else {
            return;
        }
        hp.setWideVaneSetting(rootInfo["wideVane"]);
        hp.update();
    } else if (strcmp(topic, temp_set_topic.c_str()) == 0) {
        float temperature = message.toFloat();
        if (temperature >= min_temp && temperature <= max_temp) {
            rootInfo["temperature"] = temperature;
            hp.setTemperature(rootInfo["temperature"]);
            hp.update();
        }
    }
}

void sendWbMeta() {
    size_t capacity;
    JsonObject title;
    String mqttOutput, driver_topic, name_topic, type_topic, meta_topic, min_topic, max_topic;

    // main
    capacity = JSON_OBJECT_SIZE(2) + 128;
    DynamicJsonDocument main_meta(capacity);
    main_meta["driver"] = "mitsubishi2wb";
    title = main_meta.createNestedObject("title");
    title["en"] = hostname;
    title["ru"] = hostname;
    mqttOutput = "";
    serializeJson(main_meta, mqttOutput);
    mqtt_client.publish(main_topic.c_str(), mqttOutput.c_str(), true);
    driver_topic = main_topic + "/driver";
    mqtt_client.publish(driver_topic.c_str(), main_meta["driver"], true);
    name_topic = main_topic + "/name";
    mqtt_client.publish(name_topic.c_str(), main_meta["title"]["en"], true);

    // power
    capacity = JSON_OBJECT_SIZE(3) + 128;
    DynamicJsonDocument power_meta(capacity);
    power_meta["type"] = "switch";
    power_meta["readonly"] = false;
    title = power_meta.createNestedObject("title");
    title["en"] = "power";
    title["ru"] = "power";
    mqttOutput = "";
    serializeJson(power_meta, mqttOutput);
    meta_topic = power_topic + "/meta";
    mqtt_client.publish(meta_topic.c_str(), mqttOutput.c_str(), true);
    type_topic = meta_topic + "/type";
    mqtt_client.publish(type_topic.c_str(), String(power_meta["type"]).c_str(), true);

    // mode
    capacity = JSON_OBJECT_SIZE(5) + 128;
    DynamicJsonDocument mode_meta(capacity);
    mode_meta["type"] = "range";
    mode_meta["readonly"] = false;
    mode_meta["min"] = 0.0;
    mode_meta["max"] = 4.0;
    title = mode_meta.createNestedObject("title");
    title["en"] = "mode";
    title["ru"] = "mode";
    mqttOutput = "";
    serializeJson(mode_meta, mqttOutput);
    meta_topic = mode_topic + "/meta";
    mqtt_client.publish(meta_topic.c_str(), mqttOutput.c_str(), true);
    type_topic = meta_topic + "/type";
    mqtt_client.publish(type_topic.c_str(), String(mode_meta["type"]).c_str(), true);
    min_topic = meta_topic + "/min";
    mqtt_client.publish(min_topic.c_str(), String(mode_meta["min"]).c_str(), true);
    max_topic = meta_topic + "/max";
    mqtt_client.publish(max_topic.c_str(), String(mode_meta["max"]).c_str(), true);

    // fan
    capacity = JSON_OBJECT_SIZE(5) + 128;
    DynamicJsonDocument fan_meta(capacity);
    fan_meta["type"] = "range";
    fan_meta["readonly"] = false;
    fan_meta["min"] = 0.0;
    fan_meta["max"] = 5.0;
    title = fan_meta.createNestedObject("title");
    title["en"] = "fan";
    title["ru"] = "fan";
    mqttOutput = "";
    serializeJson(fan_meta, mqttOutput);
    meta_topic = fan_topic + "/meta";
    mqtt_client.publish(meta_topic.c_str(), mqttOutput.c_str(), true);
    type_topic = meta_topic + "/type";
    mqtt_client.publish(type_topic.c_str(), String(fan_meta["type"]).c_str(), true);
    min_topic = meta_topic + "/min";
    mqtt_client.publish(min_topic.c_str(), String(fan_meta["min"]).c_str(), true);
    max_topic = meta_topic + "/max";
    mqtt_client.publish(max_topic.c_str(), String(fan_meta["max"]).c_str(), true);

    // vane
    capacity = JSON_OBJECT_SIZE(5) + 128;
    DynamicJsonDocument vane_meta(capacity);
    vane_meta["type"] = "range";
    vane_meta["readonly"] = false;
    vane_meta["min"] = 0.0;
    vane_meta["max"] = 6.0;
    title = vane_meta.createNestedObject("title");
    title["en"] = "vane";
    title["ru"] = "vane";
    mqttOutput = "";
    serializeJson(vane_meta, mqttOutput);
    meta_topic = vane_topic + "/meta";
    mqtt_client.publish(meta_topic.c_str(), mqttOutput.c_str(), true);
    type_topic = meta_topic + "/type";
    mqtt_client.publish(type_topic.c_str(), String(vane_meta["type"]).c_str(), true);
    min_topic = meta_topic + "/min";
    mqtt_client.publish(min_topic.c_str(), String(vane_meta["min"]).c_str(), true);
    max_topic = meta_topic + "/max";
    mqtt_client.publish(max_topic.c_str(), String(vane_meta["max"]).c_str(), true);

    // widevane
    capacity = JSON_OBJECT_SIZE(5) + 128;
    DynamicJsonDocument widevane_meta(capacity);
    widevane_meta["type"] = "range";
    widevane_meta["readonly"] = false;
    widevane_meta["min"] = 0.0;
    widevane_meta["max"] = 6.0;
    title = widevane_meta.createNestedObject("title");
    title["en"] = "widevane";
    title["ru"] = "widevane";
    mqttOutput = "";
    serializeJson(widevane_meta, mqttOutput);
    meta_topic = widevane_topic + "/meta";
    mqtt_client.publish(meta_topic.c_str(), mqttOutput.c_str(), true);
    type_topic = meta_topic + "/type";
    mqtt_client.publish(type_topic.c_str(), String(widevane_meta["type"]).c_str(), true);
    min_topic = meta_topic + "/min";
    mqtt_client.publish(min_topic.c_str(), String(widevane_meta["min"]).c_str(), true);
    max_topic = meta_topic + "/max";
    mqtt_client.publish(max_topic.c_str(), String(widevane_meta["max"]).c_str(), true);

    // temperature
    capacity = JSON_OBJECT_SIZE(5) + 128;
    DynamicJsonDocument temp_meta(capacity);
    temp_meta["type"] = "temperature";
    temp_meta["readonly"] = false;
    temp_meta["min"] = min_temp;
    temp_meta["max"] = max_temp;
    title = temp_meta.createNestedObject("title");
    title["en"] = "temperature";
    title["ru"] = "temperature";
    mqttOutput = "";
    serializeJson(temp_meta, mqttOutput);
    meta_topic = temp_topic + "/meta";
    mqtt_client.publish(meta_topic.c_str(), mqttOutput.c_str(), true);
    type_topic = meta_topic + "/type";
    mqtt_client.publish(type_topic.c_str(), String(temp_meta["type"]).c_str(), true);
    min_topic = meta_topic + "/min";
    mqtt_client.publish(min_topic.c_str(), String(temp_meta["min"]).c_str(), true);
    max_topic = meta_topic + "/max";
    mqtt_client.publish(max_topic.c_str(), String(temp_meta["max"]).c_str(), true);

    // room_temperature
    capacity = JSON_OBJECT_SIZE(3) + 128;
    DynamicJsonDocument room_temp_meta(capacity);
    room_temp_meta["type"] = "temperature";
    room_temp_meta["readonly"] = true;
    title = room_temp_meta.createNestedObject("title");
    title["en"] = "room_temperature";
    title["ru"] = "room_temperature";
    mqttOutput = "";
    serializeJson(room_temp_meta, mqttOutput);
    meta_topic = room_temp_topic + "/meta";
    mqtt_client.publish(meta_topic.c_str(), mqttOutput.c_str(), true);
    type_topic = meta_topic + "/type";
    mqtt_client.publish(type_topic.c_str(), String(room_temp_meta["type"]).c_str(), true);
}

void mqttConnect() {
    // Loop until we're reconnected
    int attempts = 0;
    while (!mqtt_client.connected()) {
        // Attempt to connect
        mqtt_client.connect(mqtt_client_id.c_str(), mqtt_username.c_str(), mqtt_password.c_str());
        // If state < 0 (MQTT_CONNECTED) => network problem we retry 5 times and then waiting for MQTT_RETRY_INTERVAL_MS and retry reapeatly
        if (mqtt_client.state() < MQTT_CONNECTED) {
            if (attempts == 5) {
                lastMqttRetry = millis();
                return;
            } else {
                delay(10);
                attempts++;
            }
        }
        // If state > 0 (MQTT_CONNECTED) => config or server problem we stop retry
        else if (mqtt_client.state() > MQTT_CONNECTED) {
            return;
        }
        // We are connected
        else {
            sendWbMeta();
            mqtt_client.subscribe(power_set_topic.c_str());
            mqtt_client.subscribe(mode_set_topic.c_str());
            mqtt_client.subscribe(fan_set_topic.c_str());
            mqtt_client.subscribe(vane_set_topic.c_str());
            mqtt_client.subscribe(widevane_set_topic.c_str());
            mqtt_client.subscribe(temp_set_topic.c_str());
        }
    }
}

bool connectWifi() {
#ifdef ESP32
    WiFi.setHostname(hostname.c_str());
#else
    WiFi.hostname(hostname.c_str());
#endif
    if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
        delay(10);
    }
#ifdef ESP32
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
#endif
    WiFi.begin(ap_ssid.c_str(), ap_pwd.c_str());
    // Serial.println("Connecting to " + ap_ssid);
    wifi_timeout = millis() + 30000;
    while (WiFi.status() != WL_CONNECTED && millis() < wifi_timeout) {
        Serial.write('.');
        // Serial.print(WiFi.status());
        // wait 500ms, flashing the blue LED to indicate WiFi connecting...
        digitalWrite(blueLedPin, LOW);
        delay(250);
        digitalWrite(blueLedPin, HIGH);
        delay(250);
    }
    if (WiFi.status() != WL_CONNECTED) {
        // Serial.println(F("Failed to connect to wifi"));
        return false;
    }
    // Serial.println(F("Connected to "));
    // Serial.println(ap_ssid);
    // Serial.println(F("Ready"));
    // Serial.print("IP address: ");
    while (WiFi.localIP().toString() == "0.0.0.0" || WiFi.localIP().toString() == "") {
        // Serial.write('.');
        delay(500);
    }
    if (WiFi.localIP().toString() == "0.0.0.0" || WiFi.localIP().toString() == "") {
        // Serial.println(F("Failed to get IP address"));
        return false;
    }
    // Serial.println(WiFi.localIP());
    // keep LED off (For Wemos D1-Mini)
    digitalWrite(blueLedPin, HIGH);
    return true;
}

String getId() {
#ifdef ESP32
    uint64_t macAddress = ESP.getEfuseMac();
    uint64_t macAddressTrunc = macAddress << 40;
    uint32_t chipID = macAddressTrunc >> 40;
#else
    uint32_t chipID = ESP.getChipId();
#endif
    return String(chipID, HEX);
}

void loop() {
    server.handleClient();
    ArduinoOTA.handle();

    // reset board to attempt to connect to wifi again if in ap mode or wifi dropped out and time limit passed
    if (WiFi.getMode() == WIFI_STA and WiFi.status() == WL_CONNECTED) {
        wifi_timeout = millis() + WIFI_RETRY_INTERVAL_MS;
    } else if (wifi_config_exists and millis() > wifi_timeout) {
        ESP.restart();
    }

    if (!captive) {
        // Sync HVAC UNIT
        if (!hp.isConnected()) {
            // Use exponential backoff for retries, where each retry is double the length of the previous one.
            unsigned long durationNextSync = (1 << hpConnectionRetries) * HP_RETRY_INTERVAL_MS + lastHpSync;
            if ((millis() - lastHpSync > durationNextSync) or lastHpSync == 0) {
                lastHpSync = millis();
                // If we've retried more than the max number of tries, keep retrying at that fixed interval, which is several minutes.
                hpConnectionRetries = min(hpConnectionRetries + 1u, HP_MAX_RETRIES);
                hp.sync();
            }
        } else {
            hpConnectionRetries = 0;
            hp.sync();
        }

        if (mqtt_config) {
            // MQTT failed, retry to connect
            if (mqtt_client.state() < MQTT_CONNECTED) {
                if ((millis() - lastMqttRetry > MQTT_RETRY_INTERVAL_MS) or lastMqttRetry == 0) {
                    mqttConnect();
                }
            }
            // MQTT config problem on MQTT, do nothing
            else if (mqtt_client.state() > MQTT_CONNECTED) return;
            // MQTT connected, send status
            else {
                hpStatusChanged(hp.getStatus());
                mqtt_client.loop();
            }
        }
    } else {
        dnsServer.processNextRequest();
    }
}
