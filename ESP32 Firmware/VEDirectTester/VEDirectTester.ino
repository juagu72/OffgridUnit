#include <WiFiMulti.h>
#include <WiFi.h>
#include <PubSubClient.h>  // MQTT
#include  <SoftwareSerial.h>
#include <HardwareSerial.h>
#include "VeDirectFrameHandler.h"
#include "time.h"

// VE.Direct
//
#define rxPin 19 // Serial port RX
#define txPin 18 // Serial port TX - Not used
VeDirectFrameHandler myve;
//SoftwareSerial veSerial(rxPin, txPin);
HardwareSerial veSerial(1);
 

// WiFi AP
//
#define WIFI_SSID "FRITZ!Box 7583 OG" // AP SSID
#define WIFI_PASSWORD "54080573356898388373" // WiFi password
WiFiMulti wifiMulti;
WiFiClient wifiClient;

// NTP Time sync
//
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"
time_t time_Sync = 0;

// MQTT
//
IPAddress mqttServerIP(192, 168, 172, 2);
PubSubClient mqttClient(wifiClient);

// InfluxDB
//
#define INFLUXDB_URL "https://eu-central-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_ORG "juri.guldner@web.de"
#define INFLUXDB_BUCKET "VEDirect"
#define INFLUXDB_TOKEN "zvJXZiLq7iNTypZqBIEhk__tJRiCvJnJktwCroF40Xye3t8OOZChkDtSWCfli0v2gnXcJiFYsE4N2b3_Vb3vTQ=="
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient influxClient(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point influxDataPoint("VEDirectDataPoint");

// Test
//
int counter = 0;
bool newData = false;

void setup() {
  Serial.begin(115200);

  //Serial2.begin(19200, SERIAL_8N1, rxPin, txPin);
  veSerial.begin(19200, SERIAL_8N1, rxPin, txPin);
  //veSerial.begin(19200, SWSERIAL_8N1, rxPin, txPin);
  //veSerial.flush();

  // Setup WIFI
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  mqttClient.setServer(mqttServerIP, 1883);

  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");


  if (influxClient.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(influxClient.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(influxClient.getLastErrorMessage());
  }

}

void loop() {
  if (wifiMulti.run() == WL_CONNECTED){
    
    if (!mqttClient.connected()){
      reconnect();
    }

    newData = false;

    while (veSerial.available()){
      //Serial.print(char(hSerial1.read()));
      //Serial.print(char(veSerial.read()));
      myve.rxData(veSerial.read());
      newData = true;  
    }

    EverySecond();

    EveryMinute();

    /*
    if (influxClient.validateConnection()){
        // Write point
        influxDataPoint.setTime();
        //influxDataPoint.addTag("device", "ESP32");
        //influxDataPoint.addField("batteryVoltage", myve.veValue[0]);
        influxDataPoint.clearFields();
        influxDataPoint.addField("batteryVoltage", counter);

        if (!influxClient.writePoint(influxDataPoint)) {
          Serial.print("InfluxDB writePoint failed: ");
          Serial.println(influxClient.getLastErrorMessage());
        }
        else{
          // Print what are we exactly writing
          Serial.print("Writing to database: ");
          Serial.println(influxDataPoint.toLineProtocol());
        }
        influxClient.flushBuffer();
    }
    */

    mqttClient.loop();
  }

  //delay(1000);
  //Serial.println("Looped");
}

void EverySecond() {  
    static unsigned long prev_millis1;

    if (millis() - prev_millis1 > 1000) {
        Serial.println("Second trigger");
        PublishData2MQTT();
        prev_millis1 = millis();
    }
}

void EveryMinute() {
    static unsigned long prev_millis2;

    if (millis() - prev_millis2 > 60000) {
        Serial.println("Minute trigger");
        PublishData2DB();
        prev_millis2 = millis();
    }
}

void reconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    Serial.print(mqttClient.state());
    // Attempt to connect
    if (mqttClient.connect("JurisMQTTServer")) {
      Serial.println(" connected");
      delay(1000);
      // ... and resubscribe
      //mqttClient.subscribe("inTopic");
      struct tm timeinfo;
      if(!getLocalTime(&timeinfo)){
        mqttClient.publish("Victron/VE_Direct_Time", "Failed to obtain time");
      }
      else{
        char timeStringBuff[50];
        strftime(timeStringBuff, sizeof(timeStringBuff),"%A, %B %d %Y %H:%M:%S", &timeinfo);
        mqttClient.publish("Victron/VE_Direct_Time", timeStringBuff);
      }
    } else {
      Serial.print(" failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void PrintData() {
    for ( int i = 0; i < myve.veEnd; i++ ) {
    Serial.print(myve.veName[i]);
    Serial.print("= ");
    Serial.println(myve.veValue[i]);    
    }
}

void PublishData2MQTT() {
    mqttClient.publish("Victron/VE_Direct_NewData", newData ? "true" : "false");
    for ( int i = 0; i < myve.veEnd; i++ ) {
      if (i == 2)
        continue;
      mqttClient.publish(((String("Victron/VE_Direct_")) + myve.veName[i]).c_str(), String(myve.veValue[i]).c_str());

      //Serial.print(myve.veName[i]);
      //Serial.print("= ");
      //Serial.println(myve.veValue[i]);    
    }
}

void PublishData2DB() {
  influxDataPoint.clearFields();
  influxDataPoint.setTime();

  for (int i = 0; i < myve.veEnd; i++){

    if (String(myve.veValue[i]).toInt() != 0)
    {
      influxDataPoint.addField(String("VE_Direct_") + String(myve.veName[i]), String(myve.veValue[i]).toInt());
    }
  }

  if (!influxClient.writePoint(influxDataPoint)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(influxClient.getLastErrorMessage());
  }
/*
  influxDataPoint.clearFields();
  //influxDataPoint.addField("myve.veName[3]).c_str()", counter++);
  //influxDataPoint.addField(myve.veName[3], String(myve.veValue[3]).toInt());
  //influxDataPoint.addField("Hallo", String(myve.veValue[3]).toInt());
  //influxDataPoint.addField("Hallo", counter++);
  //strcpy(myve.veValue[3], "666");
  //strcpy(myve.veName[5], "VPV");
  //influxDataPoint.addField(String(myve.veName[5]), String(myve.veValue[3]).toInt());
  //influxDataPoint.addField(String("VPV"), String(myve.veValue[3]).toInt());
  //influxDataPoint.addField(String("V"), String(myve.veValue[3]).toInt());
  influxDataPoint.addField(String("VE_Direct_") + String(myve.veName[5]), String(myve.veValue[3]).toInt());

  if (!influxClient.writePoint(influxDataPoint)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(influxClient.getLastErrorMessage());
  }
*/
  //veSerial.flush();
}

void timeSync() {
  // Synchronize UTC time with NTP servers
  // Accurate time is necessary for certificate validaton and writing in batches
  configTime(0, 0, "pool.ntp.org", "time.nis.gov");
  // Set timezone
  setenv("TZ", TZ_INFO, 1);

  // Wait till time is synced
  Serial.print("Syncing time ");
  int i = 0;
  while (time(nullptr) < 1000000000ul && i < 100) {
    Serial.print(".");
    delay(100);
    i++;
  }
  time_Sync = time(nullptr);
  Serial.print(localtime(&time_Sync));
  Serial.println();
}
