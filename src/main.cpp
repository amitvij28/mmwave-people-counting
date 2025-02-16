#include<Arduino.h>
#include "MyLD2410.h"
#include<SoftwareSerial.h>
#include<ArduinoJson.h>
#include<PubSubClient.h>
#include<WiFiClientSecure.h>
#include "WiFi.h"
#include "secrets.h"
#include<ESP32Ping.h>

#define AWS_IOT_PUB_TOPIC "esp32/pub"
#define AWS_IOT_SUB_TOPIC "esp32/sub"
#define AWS_LOG "esp32/lg"

#define R1_SERIAL Serial1
#define R1_RX 32
#define R1_TX 33

#define R2_SERIAL Serial2 
  
#define R2_RX 25
#define R2_TX 26

#define R3_SERIAL Serial
#define R3_RX 23
#define R3_TX 22


#define SENSOR_BAUD_RATE 256000
#define SERIAL_BAUD_RATE 256000
#define NUM_RADARS 3
#define SAMPLE_PERIOD 500
#define CONNECTION_RETRY_LIMIT 3


void read_sensor_data(void *pvParams);
void read_sensor_data_2(void *pvParams);
void read_sensor_data_3(void *pvParams);
void printStatus(MyLD2410 *r, int i );
void logAWS(char* message);

WiFiClientSecure net = WiFiClientSecure();
PubSubClient client(net);

// EspSoftwareSerial::UART R3_SERIAL;


bool r1Connected= false,r2Connected= false,r3Connected= false; 
int lastUpdated = 0;

MyLD2410 radar1(R1_SERIAL);
MyLD2410 radar2(R2_SERIAL);
MyLD2410 radar3(R3_SERIAL);

MyLD2410 radars[] = {radar1, radar2, radar3};

bool connected[] = {r1Connected,r2Connected, r3Connected};



void ping(char* domain){
 bool ret = Ping.ping(domain);
  Serial.print("Ping result: ");
  Serial.println(ret?"true":"false");
 
}


void connectAWS(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to Wi-Fi");
 
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected");
  Serial.println(WiFi.localIP());
  ping("www.google.com");

  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);
  client.setServer(AWS_IOT_ENDPOINT, 8883);

   Serial.println("Connecting to AWS IOT");
 
  int attempt = 0;
  while (!client.connect(THINGNAME) && attempt < 5) {
      Serial.print("Attempt ");
      Serial.println(attempt + 1);
      delay(2000);
      attempt++;
  }
  if (!client.connected()) {
      Serial.println("Failed to connect to AWS IoT after multiple attempts.");
      return;
  }
  Serial.println("AWS IoT Connected!");
  logAWS("Testing...");
}


void radarInit(MyLD2410 *radar, int rdx, bool *lu){
  delay(1000);
  Serial.print("Initiating radar ");
  Serial.println(rdx);
  int retry = 0;
  while(!*lu && retry < CONNECTION_RETRY_LIMIT){
    if(radar->begin()){
        Serial.print("Success communication with radar ");
        Serial.println(rdx);
        *lu = true;
        break;
    }
    Serial.println("Retrying...");
    retry++;  
    delay(2000);
  }
  
  if(!*lu){
    Serial.println("Radar Connection Failure");
  }
  retry = 0;
  bool engMode = false;
  while(!engMode && retry < CONNECTION_RETRY_LIMIT){
    if(radar->enhancedMode()){
        Serial.println(" engineering: SUCCESS");
        engMode = true;
        break;
    }
    Serial.println("Retrying...");
    retry++;
    delay(1000);
  }
  if(!engMode){
    Serial.println(" engineering: FAIL");
  }

}

void updateBaudRate(MyLD2410 *radar){
  bool baudUpdated = false;
  while(!baudUpdated){
    Serial.println("Setting baud...");
    if(radar->setBaud(7)){
      Serial.println("BAUD SUCCESS");
      baudUpdated = true;
    }else{
      Serial.println("BAUD FAIL");
    }  
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  connectAWS();
  Serial.end();
  R1_SERIAL.begin(SENSOR_BAUD_RATE, SERIAL_8N1, R1_RX, R1_TX);
  R2_SERIAL.begin(SENSOR_BAUD_RATE, SERIAL_8N1, R2_RX, R2_TX);
  
  R3_SERIAL.begin(SENSOR_BAUD_RATE, SERIAL_8N1, R3_RX, R3_TX);

  for(int i = 0; i< NUM_RADARS; i++){
    radarInit(&radars[i], i+1, &connected[i]);
  }
  
  xTaskCreate(read_sensor_data, "", 10000, NULL, 2, NULL);
  xTaskCreate(read_sensor_data_2, "", 10000, NULL, 2, NULL);
  xTaskCreate(read_sensor_data_3, "", 10000, NULL, 2, NULL);
}

void logAWS(char* logMessage){
  StaticJsonDocument<128> message;
  message['message'] = logMessage;
  String output;
  serializeJson(message, output);
  client.publish(AWS_LOG, output.c_str());
}

String getStatusJson(MyLD2410 *radar, int rdx) {
  StaticJsonDocument<512> doc;

  // Check radar status
  if (radar->check() == MyLD2410::Response::DATA) {
    doc["sensorId"] = rdx;
    doc["status"] = radar->statusString();
    doc["enhancedMode"] = radar->inEnhancedMode() ? "true" : "false";
    Serial.println(millis());
    // If presence detected, add distance info
    if (radar->presenceDetected()) {
      doc["presenceDetected"] = true;
      doc["distance"] = radar->detectedDistance();
    } else {
      doc["presenceDetected"] = false;
    }

    // If moving target detected, add related info
    if (radar->movingTargetDetected()) {
      JsonObject movingTarget = doc.createNestedObject("movingTarget");
      movingTarget["signal"] = radar->movingTargetSignal();
      movingTarget["distance"] = radar->movingTargetDistance();
      JsonArray movingSignals = movingTarget.createNestedArray("signals");
      radar->getMovingSignals().forEach([&movingSignals](int value) {
        movingSignals.add(value);
      });
      JsonArray movingThresholds = movingTarget.createNestedArray("thresholds");
      radar->getMovingThresholds().forEach([&movingThresholds](int value) {
        movingThresholds.add(value);
      });
    }

    // If stationary target detected, add related info
    if (radar->stationaryTargetDetected()) {
      JsonObject stationaryTarget = doc.createNestedObject("stationaryTarget");
      stationaryTarget["signal"] = radar->stationaryTargetSignal();
      stationaryTarget["distance"] = radar->stationaryTargetDistance();
      JsonArray stationarySignals = stationaryTarget.createNestedArray("signals");
      radar->getStationarySignals().forEach([&stationarySignals](int value) {
        stationarySignals.add(value);
      });
      JsonArray stationaryThresholds = stationaryTarget.createNestedArray("thresholds");
      radar->getStationaryThresholds().forEach([&stationaryThresholds](int value) {
        stationaryThresholds.add(value);
      });
    }
  }

  // Serialize JSON to string and return
  String output;
  serializeJson(doc, output);
  return output;
}


void read_sensor_data_2(void *pvParams){
  while(1){
    if(R2_SERIAL.available()){
      String data = getStatusJson(&radar2, 2);
      Serial.println(data);
      client.publish(AWS_IOT_PUB_TOPIC, data.c_str());

    }
    vTaskDelay(SAMPLE_PERIOD / portTICK_PERIOD_MS);
  }
}

void read_sensor_data(void *pvParams){

  while(1){
    if(R1_SERIAL.available()){
      String data = getStatusJson(&radar1, 1);
      Serial.println(data);

      client.publish(AWS_IOT_PUB_TOPIC, data.c_str());
    }
    vTaskDelay(SAMPLE_PERIOD / portTICK_PERIOD_MS);
  }
}

void read_sensor_data_3(void *pvParams){
  while(1){
     if(R3_SERIAL.available()){
        String data = getStatusJson(&radar3, 3);
        client.publish(AWS_IOT_PUB_TOPIC, data.c_str());

      Serial.println(data);
      }
    vTaskDelay(SAMPLE_PERIOD / portTICK_PERIOD_MS);
  }
}

void loop() {}


