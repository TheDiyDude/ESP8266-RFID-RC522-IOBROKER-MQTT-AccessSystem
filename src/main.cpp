/*
Project:  RFID Access with ESP8266 / ESP32 und Arduinos
Author:   Marc-Oliver Blumenauer @ http://projekte.kobuleti.de
Date:     Created 10/06/23
Version:  V1.0

enhance these functions to your needs, i.e. activate relais and/or let LEDs blink etc
void accessGranted();
void accessRefused();

Wirering with different Modules
-RC522      -ESP8266/MCU      -ESP32        -Arduino UNO    -Arduino Mega
VCC         3.3V              3.3V          3.3V            3.3V
RST         D1 / GPIO 5       GPIO 22       GPIO 9          GPIO 5     <-- change
GND         GND               GND           GND             GND
MISO        D6 / GPIO 12      GPIO 19       GPIO 12         GPIO 50
MOSI        D7 / GPIO 13      GPIO 23       GPIO 11         GPIO 51
SCK         D5 / GPIO 14      GPIO 18       GPIO 13         GPIO 52
SDA (SS)    D2 / GPIO 4       GPIO 21       GPIO 10         GPIO 53    <-- change
IRQ         -                 -             -               -
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <SPI.h>      
#include <MFRC522.h>  

// uncomment to save memory
#define DEBUG

#include "config.h"

MFRC522 mfrc522(SS_PIN, RST_PIN); 

// Prototypes
void    updateMillisStart();
void    blink_led(int, unsigned long);
void    wifi_setup();
void    ap_mode();
boolean testWifi();
void    scanNetworks();
void    launchWeb(int);
void    createWebServer(int);
void    callback(char*, byte*, unsigned int);
boolean reconnect();
boolean ourHostname(char *);
void    clearEEPROM();
void    readConfigurationFromEEPROM();
void    writeConfigurationToEEPROM();
void    print_configuration();

unsigned long     currentMillis             = 0L;
unsigned long     previousMillis            = 0L; 
String            wifiNetworksListObjects;
String            content, lastContent;
bool              apmode;
int               numNetworksFound          = 0;
ESP8266WebServer  server(80);
const char*       PARAM_MESSAGE             = "message";
WiFiClient        espClient;
PubSubClient      client(espClient);
char              buffer[50];
long              lastReconnectAttempt      = 0;
configData        data;

void setup() {
  Serial.begin(115200);
  debugDelay(10000);
  wifi_setup();
  pinMode(LED, OUTPUT); 
  SPI.begin();      
  mfrc522.PCD_Init(); 
  delay(4);
  debugln(F("Scan PICC to see UID, SAK, type, and data blocks..."));
  uint16_t port = atoi(data.mqtt_port);
  client.setServer(data.mqtt_broker, port);
  client.setCallback(callback);
  lastReconnectAttempt = 0;
}

// enhance to your needs, i.e. activate relais and/or let LEDs blink etc
void accessGranted() {
  debugln(" Access Granted");
}

// enhance to your needs, i.e. activate relais and/or let LEDs blink etc
void accessRefused() {
   debugln(" Access Refused");
}

void loop() {
  updateMillisStart();
  if (apmode)
  {
      server.handleClient();
      blink_led(LED, 200);
  }
  else
  {
      if (WiFi.status() == WL_CONNECTED) {
          blink_led(LED,1000);
          if (!client.connected()) {
              long now = millis();
              if (now - lastReconnectAttempt > 5000) {
                lastReconnectAttempt = now;
                if (reconnect()) {
                  lastReconnectAttempt = 0;
                }
              }
          } else {
              client.loop();
              MDNS.update(); 
              server.handleClient();
          }
          
              if (!mfrc522.PICC_IsNewCardPresent())
              {
                return;
              }
              if ( ! mfrc522.PICC_ReadCardSerial()) {
                return;
              }
              String content= "";
              for (byte i = 0; i < mfrc522.uid.size; i++) {
                content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
                content.concat(String(mfrc522.uid.uidByte[i], HEX));
              }
              content.toUpperCase();
              if (content.substring(1) != lastContent)
              {
                lastContent = content.substring(1);
                debugln();
                debug(" PICC type: ");
                MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
                debugln(mfrc522.PICC_GetTypeName(piccType));
                char buffer[50];
                sprintf(buffer, "%s;%s", data.hostname, content.substring(1).c_str());
                debugF3("MQTT sent request to topic [%s]: %s\n", topic_request, buffer);
                client.publish(topic_request, buffer);
              }
      }
  }
}
void updateMillisStart() {
    currentMillis = millis();
}

void blink_led(int pin, unsigned long time = INTERVAL_MILLIS)
{
    if (currentMillis - previousMillis >= time) {
        previousMillis = currentMillis;
        if (ledState == LOW) {
                ledState = HIGH;
        } else {
                ledState = LOW;
        }
        digitalWrite(pin, ledState);
    }
}
void wifi_setup()
{
    readConfigurationFromEEPROM();
    if (strlen(data.ssid) == 0)
    { 
      ap_mode();
    }
    else
    {
      if (testWifi())
      { 
        debugln("WiFi Connected!!!");
        apmode = false;
        debugln("Ready");
        debug("IP address: ");
        debugln(WiFi.localIP());
        debug("hostname = ");
        debugln(data.hostname);
        if (MDNS.begin(data.hostname))
        {
          debugln("mDNS gestartet.");
        }
        launchWeb(1);
      }
      else
      {
        ap_mode();
      }
    }
}
void callback(char* topic, byte* payload, unsigned int length) {
  int i = 0, j = 0;
  int len = 0;
  char payload_hostname[30];
  char payload_result[6];

  if (length > 10) { 
    for (i=0; i < length; i++) {
      mqtt_payload[i] = (char)payload[i];
    }
    mqtt_payload[i] = '\0';
    i = 0;
    while (mqtt_payload[i] != ';') {
      payload_hostname[i] = mqtt_payload[i];
      i++;
    }
    payload_hostname[i++] = '\0';
    len = length - i;
    for (j = 0; j < len; j++)
    {
      payload_result[j] = mqtt_payload[i++];
    }
    payload_result[j] = '\0';
    if(ourHostname(payload_hostname)==0){
      debugF2("MQTT Message Result     Hostname: %s\n", payload_hostname);
      debugF2("MQTT Message Result Result-Value: %s\n", payload_result);
      if(strcmp(payload_result,"true")==0) {
          accessGranted();
      } else {
          accessRefused();
      }
    }
  }
}
boolean ourHostname(char* host) {
  return strcmp(host, data.hostname);
}
boolean reconnect() {
  client_id = data.hostname;
  client_id += "-";
  client_id += String(WiFi.macAddress());
  client_id.replace(":","");
  client_id.toLowerCase();
  debugF2("Client %s attempting MQTT connection...\n",client_id.c_str());
  if (client.connect(client_id.c_str(),data.mqtt_username,data.mqtt_password)) {
    debugln("MQTT connected");
    client.subscribe(data.mqtt_subscribe);
  } else {
      debug("failed with state ");
      debugln(client.state());
      delay(2000);
  }
  return client.connected();
}
void clearEEPROM()
{
    EEPROM.begin(512);
    debugln("Clearing EEPROM ");
    for (int i = 0; i < 512; i++)
    {
        debug(".");
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    EEPROM.end();
}
void writeConfigurationToEEPROM(){
  debug("Writing to EEPROM\nWifi ssid: "); 
  debugln(data.ssid);
  debug("Wifi password: ");
  debugln(data.password);
  EEPROM.begin(1000);
  EEPROM.put(0,data);
  EEPROM.commit();
  EEPROM.end();
  debugln("Writing to EEPROM -- FINISHED!"); 
}
void readConfigurationFromEEPROM(){
  debugF2("Reading bytes: %d\n", sizeof(data));
  EEPROM.begin(1000);
  EEPROM.get(0,data);
  EEPROM.end();
  client_id = data.hostname;
  debug("Reading from EEPROM\nWifi ssid: ");
  debugln(data.ssid);
  debug("Wifi password: ");
  debugln(data.password);
}
void print_configuration(){
  debugF2("WIFI SSID  : %s\n",data.ssid);
  debugF2("WIFI PASS  : %s\n",data.password);
  debugF2("Hostname   : %s\n",data.hostname);
  debugF2("MQTT Broker: %s\n",data.mqtt_broker);
  debugF2("MQTT User  : %s\n",data.mqtt_username);
  debugF2("MQTT Pass  : %s\n",data.mqtt_password);
  debugF2("MQTT Port  : %s\n",data.mqtt_port);
  debugF2("MQTT Subs  : %s\n",data.mqtt_subscribe);
  debugF2("Size       : %d\n",sizeof(data));
}
void ap_mode() {                                  
  Serial.println("AP Mode. Please connect to http://192.168.4.1 to configure");
  const char* ssidap = "Setup";
  const char* passap = "";
  scanNetworks();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidap, passap);
  Serial.println(WiFi.softAPIP());
  apmode = true;
  launchWeb(0);                                   
}
boolean testWifi() {                            
  WiFi.softAPdisconnect();
  WiFi.disconnect();
  scanNetworks();
  WiFi.mode(WIFI_STA);
  WiFi.begin(data.ssid, data.password);
  int c = 0;
  while (c < 30) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setAutoReconnect(true);
      WiFi.persistent(true);
      debugln(WiFi.status());
      debugln(WiFi.localIP());
      delay(100);
      return true;
    }
    debug(".");
    delay(900);
    c++;
  }
  debugln("Connection time out...");
  return false;
}
void scanNetworks(){
    numNetworksFound = WiFi.scanNetworks(false, true);
    if (numNetworksFound == 0) {
        debugln("No Wifi Networks found!");
    } else {
        wifiNetworksListObjects = "<ol>";
        for (int i = 0; i < numNetworksFound; ++i)
        {
            wifiNetworksListObjects += "<li>";
            wifiNetworksListObjects += WiFi.SSID(i);
            wifiNetworksListObjects += " (";
            wifiNetworksListObjects += WiFi.RSSI(i);
            wifiNetworksListObjects += ")";
            wifiNetworksListObjects += (WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*";
            wifiNetworksListObjects += "</li>";
        }
        wifiNetworksListObjects += "</ol>";
    }
}
void launchWeb(int webtype) {
  createWebServer(webtype);
  server.begin();
}
void createWebServer(int webtype) {
  if (webtype == 0) {
    server.on("/", []() {
      scanNetworks();
      String ssid = data.ssid;
      String password = data.password;
      String mqtt_broker = data.mqtt_broker;
      String mqtt_username = data.mqtt_username;
      String mqtt_password = data.mqtt_password;
      String mqtt_port = data.mqtt_port;
      String hostname = data.hostname;
      String mqtt_subscribe = data.mqtt_subscribe;
      content = "<html><head><style>.button {background-color: #3CBC8D;";
      content += "color: white;padding: 5px 10px;text-align: center;text-decoration: none;";
      content += "display: inline-block;font-size: 14px;margin: 4px 2px;cursor: pointer;}";
      content += "input[type=text],[type=password]{width: 100%;padding: 5px 10px;";
      content += "margin: 5px 0;box-sizing: border-box;border: none;";
      content += "background-color: #3CBC8D;color: white;}";
      content += ".landingPage {margin: 0 auto 0 auto;width: 100%;min-height: 100vh;display: flex;align-items: center;justify-content: center;}";
      content += "</style></head><body><div class='landingPage'><div>";
      content += "<table><tr>";
      content += "<td><form action='/'><input type='submit' value='Home' /></form></td>";
      content += "<td><form action='/settings'><input type='submit' value='Settings' /></form></td>";
      content += "<td><form action='/clear'><input type='submit' value='Clear EEPROM' /></form></td>";
      content += "<td><form action='/reboot'><input type='submit' value='Reboot' /></form></td>";
      content += "</tr></table>";      
      content += "<h1>Setup</h1>";
      content += "<h3>Current Settings</h3>";
      content += "<table><tr><td><label> WiFi SSID </label></td><td><p style='color:blue;'>" + ssid + "</p></td></tr>";
      content += "<tr><td><label> WiFi Pasword </label></td><td><p style='color:blue;'>*****</p></td></tr></table>";
      content += "<h4>Available Networks:</h4>"+wifiNetworksListObjects;
      content += "<form method='get' action='setting'>";
      content += "<h3>New Settings</h3>";
      content += "<table><tr><td><label>WiFi SSID</label></td><td><input type='text' name = 'ssid' lenght=32 value='"+ssid+"'></td></tr>";
      content += "<tr><td><label> WiFi Password</label></td><td><input type='password' name = 'password' lenght=32 value='"+password+"'></td></tr>";
      content += "<tr><td><label>Enable MQTT</label></td><td><input type='checkbox' name='mqtt' checked='on'></td></tr>";
      content += "<tr><td><label> My Hostname</label></td><td><input type='text' name = 'hostname' lenght=32 value='"+hostname+"'></td></tr>";
      content += "<tr><td><label> MQTT Broker</label></td><td><input type='text' name = 'mqtt_broker' lenght=32 value='"+mqtt_broker+"'></td></tr>";
      content += "<tr><td><label> MQTT Port</label></td><td><input type='text' name = 'mqtt_port' lenght=32 value='"+mqtt_port+"'></td></tr>";
      content += "<tr><td><label> MQTT Username</label></td><td><input type='text' name = 'mqtt_username' lenght=32 value='"+mqtt_username+"'></td></tr>";
      content += "<tr><td><label> MQTT Password</label></td><td><input type='password' name = 'mqtt_password' lenght=32 value='"+mqtt_password+"'></td></tr>";
      content += "<tr><td><label> MQTT Subscribe</label></td><td><textarea name='mqtt_subscribe' rows=5 cols=40>"+mqtt_subscribe+"</textarea></td></tr>";
      
      content += "<tr><td></td><td><input class=button type='submit'></td></tr></table></form>";
      content += "<table><tr>";
      content += "<td><form action='/'><input type='submit' value='Home' /></form></td>";
      content += "<td><form action='/settings'><input type='submit' value='Settings' /></form></td>";
      content += "<td><form action='/clear'><input type='submit' value='Clear EEPROM' /></form></td>";
      content += "<td><form action='/reboot'><input type='submit' value='Reboot' /></form></td>";
      content += "</tr></table>";
      content += "</div></div></body></html>";
      server.send(200, "text/html", content);
    }); 
    server.on("/reboot", []() {
      ESP.restart();
    });   
    server.on("/setting", []() { 
      strcpy(data.ssid           ,server.arg("ssid").c_str());
      strcpy(data.password       ,server.arg("password").c_str());  
      strcpy(data.hostname       ,server.arg("hostname").c_str()); 
      strcpy(data.mqtt_broker    ,server.arg("mqtt_broker").c_str()); 
      strcpy(data.mqtt_username  ,server.arg("mqtt_username").c_str()); 
      strcpy(data.mqtt_password  ,server.arg("mqtt_password").c_str()); 
      strcpy(data.mqtt_port      ,server.arg("mqtt_port").c_str()); 
      strcpy(data.mqtt_subscribe ,server.arg("mqtt_subscribe").c_str());
      print_configuration();
      writeConfigurationToEEPROM();
      content = "Success...rebooting NOW!";
      debugF2("Writing bytes: %d\n", sizeof(data));
      server.send(200, "text/html", content);
      delay(2000);
      ESP.restart();
    });
    server.on("/clear", []() {                      
      clearEEPROM();
      delay(2000);
      ESP.restart();
    });
  }
  if (webtype == 1) {
    server.on("/", []() {
        content = "<html><head><style>.button {background-color: #3CBC8D;";
        content += "color: white;padding: 5px 10px;text-align: center;text-decoration: none;";
        content += "display: inline-block;font-size: 14px;margin: 4px 2px;cursor: pointer;}";
        content += "input[type=text],[type=password]{width: 100%;padding: 5px 10px;";
        content += "margin: 5px 0;box-sizing: border-box;border: none;";
        content += "background-color: #3CBC8D;color: white;}";
        content += ".landingPage {margin: 0 auto 0 auto;width: 100%;min-height: 100vh;display: flex;align-items: center;justify-content: center;}";
        content += "</style></head><body><div class='landingPage'>";
        content += "<table><tr>";
        content += "<td><form action='/'><input type='submit' value='Home' /></form></td>";
        content += "<td><form action='/settings'><input type='submit' value='Settings' /></form></td>";
        content += "<td><form action='/clear'><input type='submit' value='Clear EEPROM' /></form></td>";
        content += "<td><form action='/reboot'><input type='submit' value='Reboot' /></form></td>";
        content += "</tr></table>";
        content += "</div></body></html>";
        server.send(200, "text/html", content);
    });
    server.on("/settings", []() {
      // /AccessControl/Result
      scanNetworks();
      String ssid = data.ssid;
      String password = data.password;
      String mqtt_broker = data.mqtt_broker;
      String mqtt_username = data.mqtt_username;
      String mqtt_password = data.mqtt_password;
      String mqtt_port = data.mqtt_port;
      String hostname = data.hostname;
      String mqtt_subscribe = data.mqtt_subscribe;
      content = "<html><head><style>.button {background-color: #3CBC8D;";
      content += "color: white;padding: 5px 10px;text-align: center;text-decoration: none;";
      content += "display: inline-block;font-size: 14px;margin: 4px 2px;cursor: pointer;}";
      content += "input[type=text],[type=password]{width: 100%;padding: 5px 10px;";
      content += "margin: 5px 0;box-sizing: border-box;border: none;";
      content += "background-color: #3CBC8D;color: white;}";
      content += ".landingPage {margin: 0 auto 0 auto;width: 100%;min-height: 100vh;display: flex;align-items: center;justify-content: center;}";
      content += "</style></head><body><div class='landingPage'><div>";
      content += "<table><tr>";
      content += "<td><form action='/'><input type='submit' value='Home' /></form></td>";
      content += "<td><form action='/settings'><input type='submit' value='Settings' /></form></td>";
      content += "<td><form action='/clear'><input type='submit' value='Clear EEPROM' /></form></td>";
      content += "<td><form action='/reboot'><input type='submit' value='Reboot' /></form></td>";
      content += "</tr></table>";      
      content += "<h1>Setup</h1>";
      content += "<h3>Current Settings</h3>";
      content += "<table><tr><td><label> WiFi SSID </label></td><td><p style='color:blue;'>" + ssid + "</p></td></tr>";
      content += "<tr><td><label> WiFi Pasword </label></td><td><p style='color:blue;'>*****</p></td></tr></table>";
      content += "<h4>Available Networks:</h4>"+wifiNetworksListObjects;
      content += "<form method='get' action='setting'>";
      content += "<h3>New Settings</h3>";
      content += "<table><tr><td><label>WiFi SSID</label></td><td><input type='text' name = 'ssid' lenght=32 value='"+ssid+"'></td></tr>";
      content += "<tr><td><label> WiFi Password</label></td><td><input type='password' name = 'password' lenght=32 value='"+password+"'></td></tr>";
      content += "<tr><td><label>Enable MQTT</label></td><td><input type='checkbox' name='mqtt' checked='on'></td></tr>";
      content += "<tr><td><label> My Hostname</label></td><td><input type='text' name = 'hostname' lenght=32 value='"+hostname+"'></td></tr>";
      content += "<tr><td><label> MQTT Broker</label></td><td><input type='text' name = 'mqtt_broker' lenght=32 value='"+mqtt_broker+"'></td></tr>";
      content += "<tr><td><label> MQTT Port</label></td><td><input type='text' name = 'mqtt_port' lenght=32 value='"+mqtt_port+"'></td></tr>";
      content += "<tr><td><label> MQTT Username</label></td><td><input type='text' name = 'mqtt_username' lenght=32 value='"+mqtt_username+"'></td></tr>";
      content += "<tr><td><label> MQTT Password</label></td><td><input type='password' name = 'mqtt_password' lenght=32 value='"+mqtt_password+"'></td></tr>";
      content += "<tr><td><label> MQTT Subscribe</label></td><td><textarea name='mqtt_subscribe' rows=5 cols=40>"+mqtt_subscribe+"</textarea></td></tr>";
      
      content += "<tr><td></td><td><input class=button type='submit'></td></tr></table></form>";
      content += "<table><tr>";
      content += "<td><form action='/'><input type='submit' value='Home' /></form></td>";
      content += "<td><form action='/settings'><input type='submit' value='Settings' /></form></td>";
      content += "<td><form action='/clear'><input type='submit' value='Clear EEPROM' /></form></td>";
      content += "<td><form action='/reboot'><input type='submit' value='Reboot' /></form></td>";
      content += "</tr></table>";
      content += "</div></div></body></html>";
      server.send(200, "text/html", content);
    });
    server.on("/reboot", []() {
      ESP.restart();
    });
    server.on("/setting", []() { 
      strcpy(data.ssid           ,server.arg("ssid").c_str());
      strcpy(data.password       ,server.arg("password").c_str());  
      strcpy(data.hostname       ,server.arg("hostname").c_str()); 
      strcpy(data.mqtt_broker    ,server.arg("mqtt_broker").c_str()); 
      strcpy(data.mqtt_username  ,server.arg("mqtt_username").c_str()); 
      strcpy(data.mqtt_password  ,server.arg("mqtt_password").c_str()); 
      strcpy(data.mqtt_port      ,server.arg("mqtt_port").c_str()); 
      strcpy(data.mqtt_subscribe ,server.arg("mqtt_subscribe").c_str());
      print_configuration();
      writeConfigurationToEEPROM();
      content = "Success...rebooting NOW!";
      debugF2("Writing bytes: %d\n", sizeof(data));
      server.send(200, "text/html", content);
      delay(2000);
      ESP.restart();
    });
    server.on("/clear", []() {                      
      clearEEPROM();
      delay(2000);
      ESP.restart();
    });
  }
}