#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPUpdateServer.h>
#include <WebServer.h>

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <izar_wmbus.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

extern const char* host;
extern const char* ssid;
extern const char* password;
extern const char* mqttServer;
extern const int mqttPort;

BLEScan* pBLEScan;

WiFiClient espClient;
PubSubClient client(espClient);

IzarWmbus reader;

WebServer server(80);
HTTPUpdateServer httpUpdater;

bool ledState = false;

void switchLed() {
    digitalWrite(2, ledState);
    ledState = !ledState;
}


void reconnect() {
    // Loop until we're reconnected
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // Create a random client ID
        String clientId = "ESP8266Client-";
        clientId += String(random(0xffff), HEX);
        // Attempt to connect
        if (client.connect(clientId.c_str())) {
            Serial.println("connected");
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        BLEAddress addr = advertisedDevice.getAddress();
        esp_bd_addr_t* nativeAddr = addr.getNative();

        if ((*nativeAddr)[0] == 0xA4 && (*nativeAddr)[1] == 0xC1) {
            // C1 38
            /*
                Byte 5-10 MAC in correct order
                Byte 11-12 Temperature in int16
                Byte 13 Humidity in percent
                Byte 14 Battery in percent
                Byte 15-16 Battery in mV uint16_t
                Byte 17 frame packet counter
            */
            uint8_t* payload = advertisedDevice.getPayload();
            int16_t temp = ((int16_t)payload[10] << 8) + payload[11];
            uint8_t humidity = payload[12];
            uint8_t batteryP = payload[13];
            uint16_t batteryV = ((uint16_t)payload[14] << 8) + payload[15];

            StaticJsonDocument<150> doc;
            doc["t"]=temp;
            doc["h"] = humidity;
            doc["v"] = batteryV;
            doc["b"] = batteryP;
            doc["d"] = addr.toString();
            doc["n"] = advertisedDevice.getName();

        
            char buffer[100];
            size_t n = serializeJson(doc, buffer);
            Serial.println(buffer);
            client.publish("esp/thermo", buffer, n);
            switchLed();
        }
    }
};


void setup() {
    Serial.begin(115200);

    reader.init(0);

    //original value: 0x43
    //reduce sensitivity to set MAX_LNA_GAIN to 011
    ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL2, 0x6B);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(host);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin(host)) {
        Serial.println("mDNS responder started");
    }

    httpUpdater.setup(&server);
    server.begin();

    client.setServer(mqttServer, mqttPort);
    client.setSocketTimeout(5);
    reconnect();


    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();  // create new scan
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);  // less or equal setInterval value

    pinMode(2, OUTPUT);

}

IzarResultData data;

unsigned long previousMillis = 0;
const unsigned long interval = 20000;


void loop() {
    client.loop();
        
    if (!client.connected()) {
        reconnect();
    }


    BLEScanResults foundDevices = pBLEScan->start(5, false);
    pBLEScan->clearResults();

    
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
    
        FetchResult result = reader.fetchPacket(&data);
        if (result == FETCH_SUCCESSFUL) {
            StaticJsonDocument<100> doc;
            doc["meter"] = data.meterId;
            doc["usg"] = data.waterUsage;
            char buffer[100];
            size_t n = serializeJson(doc, buffer);
            client.publish("water/consumption", buffer, n);
            switchLed();
            
            Serial.print("WatermeterId: ");
            Serial.println(data.meterId, HEX);

            Serial.print("Water consumption: ");
            Serial.println(data.waterUsage);
        }
         else {
            reader.ensureRx(); 
        }
    }

    server.handleClient();
}