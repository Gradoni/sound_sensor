#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <Ticker.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>

#define SERIAL_DEBUG 1
//define your default values here, if there are different values in config.json, they are overwritten.
#define SoundSensorPin A0
//define the pin for the sound sensor
#define VREF 3.3
//should be 3.3 for esp8266 wemos d1 mini pro

#define DAQ_INTERVAL 5
char sensorID[34] = "";
//flag for saving data
bool shouldSaveConfig = false;
volatile bool measurementFlag = true;

String host = "iot.research.hamk.fi";

// this is how you declare a secured mqtt connection
WiFiClientSecure net;
PubSubClient client(host.c_str(), 8883, net);

// and this for non secured
// WiFiClient net;
// PubSubClient client(host.c_str(), 1883, net);

Ticker DAQTimer;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

int readSoundLevel() {
  float voltageValue, dbValue;
  voltageValue = analogRead(SoundSensorPin) / 1024.0 * VREF;
  dbValue = voltageValue * 50.0;
#if SERIAL_DEBUG
  Serial.print(dbValue, 1);
  Serial.println(" dBA");
#endif
  return dbValue;
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  twi_setClockStretchLimit(20000);

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(sensorID, json["sensorID"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_sensorID("sensorID", "Location", sensorID, 32);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //wifiManager.resetSettings();

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_sensorID);

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(120);
  wifiManager.setConnectTimeout(30);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  //wifiManager.autoConnect(String(ESP.getChipId()).c_str(), "dankmemes");
  if (!wifiManager.autoConnect(String(ESP.getChipId()).c_str())) {
    digitalWrite(D4, HIGH);
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  //read updated parameters


  //save the custom parameters to FS
  if (shouldSaveConfig) {
    strcpy(sensorID, custom_sensorID.getValue());
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["sensorID"] = sensorID;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  DAQTimer.attach(DAQ_INTERVAL, setMeasurementFlag);
  //connecting to MQTT server
  client.setServer("iot.research.hamk.fi", 8883);

}

void loop() {
  // put your main code here, to run repeatedly:
  client.loop();
  yield();
  if (measurementFlag) {
    pushMqtt();
  }


}


void setMeasurementFlag() {
  measurementFlag = true;
}

void pushMqtt() {
  long rssi = WiFi.RSSI();
  //Creating JSON object to send to the webserver
  static char result_str[128] = "";
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["id"] = sensorID;
  root["value"] = readSoundLevel();
  root["uptime"] = millis();
  root["rssi"] = rssi;
  root.printTo(result_str);

  if (WiFi.status() != WL_CONNECTED) {
    WiFiManager wifiManager;
    wifiManager.setTimeout(1);
    wifiManager.setConnectTimeout(30);
    while (WiFi.status() != WL_CONNECTED) {
      wifiManager.autoConnect();
    }
  }

	uint8_t mqttRetries = 0;
	if (!client.connected()) {
		while ((!client.connected() || espClient.connected()) && mqttRetries <= 10) {
			client.connect(clientID.c_str());
			Serial.println(client.state());
			if (client.connected()) {
				client.publish("hamk/tekme/puukoulu_sound_level", result_str);
				break;
			}
			else {
				mqttRetries++;
			}
			client.loop();
		}
	}
	else {
		Serial.println(client.state());
		client.publish("hamk/tekme/puukoulu_sound_level", result_str);
	}
  measurementFlag = false;
}
