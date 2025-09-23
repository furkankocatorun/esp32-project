#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// -------------------- Configuration --------------------
#define DHTPIN 4
#define DHTTYPE DHT22
#define RELAY_PIN 16           // Now used as LED pin
#define READING_INTERVAL 2000  // 2 seconds for faster response

// OLED setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// DHT sensor
DHT dht(DHTPIN, DHTTYPE);

// WiFi credentials
const char* ssid = "Galaxy A22EE12";
const char* password = "cfbz8174";

// MQTT settings
const char* mqtt_server = "broker.kerembilgicer.com";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";
char client_id[30]; // unique client id

// Topics
const char* temp_topic = "esp32/sensor/temperature";
const char* hum_topic = "esp32/sensor/humidity";
const char* command_topic = "esp32/control/commands";
const char* status_topic = "esp32/device/status";
const char* debug_topic = "esp32/device/debug";
const char* will_topic = "esp32/device/availability";
const char* target_temp_topic = "esp32/target/temperature";

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastMsg = 0;
unsigned long lastWifiAttempt = 0;
bool oledState = true;   // OLED ON by default
bool autoMode = true;    // AUTO ON by default

// Track last published values
float lastPublishedTemp = NAN;
float lastPublishedHum  = NAN;

// EEPROM settings for target temperature
#define EEPROM_SIZE 4
#define EEPROM_ADDR 0
float targetTemperature = NAN;

// Track last relay state to avoid duplicate publishes
bool lastRelayOn = false;

// -------------------- OLED Update Function --------------------
void updateOLED(float t, float h) {
    if (!oledState) {
        display.clearDisplay();
        display.display();
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);

    // Row 1 - Target temperature
    display.setCursor(0, 0);
    display.print("Target: ");           
    display.print(targetTemperature, 1);                
    display.println(" C");

    // Row 2 - Current temperature
    display.setCursor(0, 16);
    display.print("Temp: "); display.print(t, 1); display.println(" C");

    // Row 3 - Humidity
    display.setCursor(0, 28);
    display.print("Hum:  "); display.print(h, 1); display.println(" %");

    // Row 4 - Auto mode
    display.setCursor(0, 40);
    display.print("Auto: "); display.println(autoMode ? "ON" : "OFF");

    // Row 5 - Relay state (LED)
    display.setCursor(0, 52);
    display.print("Relay: "); 
    display.println(digitalRead(RELAY_PIN) == HIGH ? "ON" : "OFF");

    display.display();
}

// -------------------- MQTT callback --------------------
void callback(char* topic, byte* payload, unsigned int length) {
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.println(message);

    if (strcmp(message, "STATUS") == 0) {
        char statusMsg[150];
        snprintf(statusMsg, sizeof(statusMsg),
                 "{\"device\":\"online\",\"oled\":\"%s\",\"relay\":\"%s\",\"auto\":\"%s\",\"rssi\":%d,\"freeMemory\":%d}",
                 oledState ? "on" : "off",
                 digitalRead(RELAY_PIN) == HIGH ? "on" : "off",
                 autoMode ? "on" : "off",
                 WiFi.RSSI(), ESP.getFreeHeap());
        client.publish(status_topic, statusMsg);
    } 
    else if (strcmp(message, "RESTART") == 0) {
        client.publish(status_topic, "{\"action\": \"restarting\"}");
        delay(1000);
        ESP.restart();
    } 
    else if (strcmp(message, "OLED_ON") == 0) {
        oledState = true;
        client.publish(status_topic, "{\"oled\": \"on\"}");
    } 
    else if (strcmp(message, "OLED_OFF") == 0) {
        oledState = false;
        client.publish(status_topic, "{\"oled\": \"off\"}");
    } 
    else if (strncmp(message, "SET_TEMP:", 9) == 0) {
        float newTarget = atof(message + 9);
        if (newTarget != targetTemperature) { 
            targetTemperature = newTarget;
            EEPROM.put(EEPROM_ADDR, targetTemperature);
            EEPROM.commit();
        }
        char buf[50];
        snprintf(buf, sizeof(buf), "%.2f", targetTemperature);
        client.publish(target_temp_topic, buf, true);
    }
    else if (strcmp(message, "GET_TEMP") == 0) {
        char buf[50];
        snprintf(buf, sizeof(buf), "%.2f", targetTemperature);
        client.publish(target_temp_topic, buf, true);
    }
    else if (strcmp(message, "AUTO_ON") == 0) {
        autoMode = true;
    }
    else if (strcmp(message, "AUTO_OFF") == 0) {
        autoMode = false;
    }
    else if (strcmp(message, "RELAY_ON") == 0 && !autoMode) {
        digitalWrite(RELAY_PIN, HIGH);  // LED ON
        lastRelayOn = true;
        client.publish(status_topic, "{\"relay\":\"on\"}");
    }
    else if (strcmp(message, "RELAY_OFF") == 0 && !autoMode) {
        digitalWrite(RELAY_PIN, LOW);   // LED OFF
        lastRelayOn = false;
        client.publish(status_topic, "{\"relay\":\"off\"}");
    }

    updateOLED(lastPublishedTemp, lastPublishedHum);
}

// -------------------- Reconnect to MQTT --------------------
void reconnect() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        boolean willRetain = true;
        int willQoS = 1;
        const char* willMessage = "offline";

        if (client.connect(client_id, mqtt_user, mqtt_password,
                           will_topic, willQoS, willRetain, willMessage)) {
            Serial.println("connected");
            client.publish(will_topic, "online", true);
            client.subscribe(command_topic);
            client.publish(status_topic, "{\"status\": \"connected\"}");
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

// -------------------- Setup --------------------
void setup() {
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); // LED OFF by default

    Serial.begin(115200);
    delay(1000);
    Serial.println("Booting ESP32 DHT Sensor...");

    dht.begin();

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");

    lastWifiAttempt = millis();

    snprintf(client_id, sizeof(client_id), "ESP32_%X", ESP.getEfuseMac());
    Serial.print("Client ID: ");
    Serial.println(client_id);

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    client.setBufferSize(512);
    client.setKeepAlive(60);

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(EEPROM_ADDR, targetTemperature);
    if (isnan(targetTemperature)) targetTemperature = 22.0;
    Serial.print("Target Temperature loaded: ");
    Serial.println(targetTemperature);

    char buf[50];
    snprintf(buf, sizeof(buf), "%.2f", targetTemperature);
    client.publish(target_temp_topic, buf, true);
}

// -------------------- Main loop --------------------
void loop() {
    // Handle WiFi reconnect every 5s
    if (WiFi.status() != WL_CONNECTED && millis() - lastWifiAttempt >= 5000) {
        lastWifiAttempt = millis();
        Serial.println("WiFi not connected. Attempting reconnect...");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
    }

    // Keep MQTT alive
    if (!client.connected() && WiFi.status() == WL_CONNECTED) reconnect();
    client.loop();

    // Read DHT sensor and publish every READING_INTERVAL
    unsigned long now = millis();
    if (now - lastMsg > READING_INTERVAL) {
        lastMsg = now;

        float h = dht.readHumidity();
        float t = dht.readTemperature();

        if (!isnan(h) && !isnan(t)) {
            bool shouldPublish = false;

            if (isnan(lastPublishedTemp) || fabs(t - lastPublishedTemp) >= 0.5) {
                lastPublishedTemp = t;
                shouldPublish = true;
            }

            if (isnan(lastPublishedHum) || fabs(h - lastPublishedHum) >= 0.5) {
                lastPublishedHum = h;
                shouldPublish = true;
            }

            if (shouldPublish) {
                char tempMsg[50], humMsg[50];
                snprintf(tempMsg, sizeof(tempMsg), "{\"temperature\": %.2f}", t);
                snprintf(humMsg, sizeof(humMsg), "{\"humidity\": %.2f}", h);

                client.publish(temp_topic, tempMsg, true);
                client.publish(hum_topic, humMsg, true);

                char debugMsg[100];
                snprintf(debugMsg, sizeof(debugMsg),
                         "{\"temperature\": %.2f, \"humidity\": %.2f}", t, h);
                client.publish(debug_topic, debugMsg);

                Serial.printf("Published -> Temp: %.2f °C | Hum: %.2f %%\n", t, h);
            } else {
                Serial.println("No significant change, skipping publish");
            }

            // Auto mode LED control
            if (autoMode) {
                bool relayOn = t < targetTemperature; // true = ON
                digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);

                // Publish relay state if changed
                if (relayOn != lastRelayOn) {
                    lastRelayOn = relayOn;
                    client.publish(status_topic,
                        relayOn ? "{\"relay\":\"on\"}" : "{\"relay\":\"off\"}"
                    );
                }
            }

            updateOLED(lastPublishedTemp, lastPublishedHum);
        } else {
            Serial.println("Failed to read from DHT sensor!");
            client.publish(debug_topic, "{\"error\": \"dht_sensor_read_failed\"}");
        }
    }
}
