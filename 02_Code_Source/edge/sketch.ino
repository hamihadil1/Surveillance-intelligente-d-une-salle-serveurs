#include <Arduino.h>
#include "DHT.h"
#include <WiFi.h>
#include <PubSubClient.h>

// ==========================================
// 1. Pin Definitions & Global Variables
// ==========================================
#define DHTPIN 15
#define DHTTYPE DHT22
#define POT_PIN 34
#define LED_PIN 2

// Wokwi Wi-Fi configuration
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// MQTT Broker configuration (Public server)
const char* mqtt_server = "broker.emqx.io";
const char* topic = "m2i2a/equipe1/edge/data";

// Object Initialization
DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);

// Queue to pass data between FreeRTOS tasks
QueueHandle_t dataQueue;

// Structure to hold sensor data
struct SensorData {
  float temp;
  float hum;
  int airFlow;
};

// ==========================================
// 2. FreeRTOS Tasks
// ==========================================

// Task 1: Acquisition (Reads sensors and sends to Queue)
void TaskAcquisition(void *pvParameters) {
  SensorData data;
  for (;;) {
    // Read DHT22 and Potentiometer (simulating airflow)
    data.temp = dht.readTemperature();
    data.hum = dht.readHumidity();
    int potValue = analogRead(POT_PIN);
    data.airFlow = map(potValue, 0, 4095, 0, 100); // Convert to percentage
    
    // Send data to the queue
    xQueueSend(dataQueue, &data, portMAX_DELAY);
    
    // Wait 2 seconds before next reading
    vTaskDelay(2000 / portTICK_PERIOD_MS); 
  }
}

// Task 2: Inference (Processes data and takes action)
void TaskInference(void *pvParameters) {
  SensorData receivedData;
  for (;;) {
    // Wait for data from the queue
    if (xQueueReceive(dataQueue, &receivedData, portMAX_DELAY) == pdPASS) {
      
      // Simple logic: If temp > 30°C, turn on the LED (Emergency Cooling)
      if (receivedData.temp > 30.0) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }
    }
  }
}

// Task 3: Communication (Sends data via MQTT)
void TaskCommunication(void *pvParameters) {
  SensorData dataToSend;
  for (;;) {
    // Peek data from queue without removing it (to allow Inference to also read)
    if (xQueuePeek(dataQueue, &dataToSend, portMAX_DELAY) == pdPASS) {
      if (client.connected()) {
        // Format data as JSON
        char msg[100];
        snprintf(msg, 100, "{\"temp\":%.2f, \"hum\":%.2f, \"air\":%d}", dataToSend.temp, dataToSend.hum, dataToSend.airFlow);
        
        // Publish to MQTT broker
        client.publish(topic, msg);
      }
    }
    // Send data every 3 seconds
    vTaskDelay(3000 / portTICK_PERIOD_MS); 
  }
}

// ==========================================
// 3. Setup and Network Connection
// ==========================================
void connectToNetwork() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password, 6); // Channel 6 for faster connection in Wokwi
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");
  
  client.setServer(mqtt_server, 1883);
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    // Unique Client ID
    if (client.connect("ESP32-Equipe1-Edge")) {
      Serial.println("Connected!");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Initialize sensors and actuators
  dht.begin();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Connect to Wi-Fi and MQTT
  connectToNetwork(); 

  // Create the queue (size 5, each item is of type SensorData)
  dataQueue = xQueueCreate(5, sizeof(SensorData));

  // Create the 3 tasks with specific priorities
  xTaskCreate(TaskAcquisition, "Acquisition", 4096, NULL, 2, NULL);
  xTaskCreate(TaskInference, "Inference", 4096, NULL, 3, NULL); // Highest priority
  xTaskCreate(TaskCommunication, "Communication", 4096, NULL, 1, NULL);
}

void loop() {
  // Keep MQTT connection alive
  client.loop(); 
  vTaskDelay(10 / portTICK_PERIOD_MS); 
}