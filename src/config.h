#pragma once

#include <Arduino.h>

#ifdef DEBUG
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#define debugF3(x,y,z) Serial.printf(x,y,z)
#define debugF2(x,y) Serial.printf(x,y)
#define debugF1(x) Serial.printf(x)
#define debugDelay(x) delay(x)
#else
#define debug(x) 
#define debugln(x) 
#define debugF3(x,y,z)
#define debugF2(x,y) 
#define debugF1(x) 
#define debugDelay(x)
#endif

#define LED                 LED_BUILTIN
#define INTERVAL_MILLIS     1000L

int         ledState        = LOW;

// RFC522 Board Pins
#define RST_PIN             D1          
#define SS_PIN              D2 

// MQTT Broker
const char* topic_request   = "/AccessControl/Request_UID";
char        mqtt_payload[50]= "";
String      client_id       = "";

struct configData
{
    char ssid[50];
    char password[50];
    char hostname[50];
    char mqtt_broker[50];
    char mqtt_username[50];
    char mqtt_password[50];
    char mqtt_port[5];
    char mqtt_subscribe[500];
};