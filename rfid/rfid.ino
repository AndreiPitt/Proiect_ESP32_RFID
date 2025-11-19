#include "WiFi.h"
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "esp_err.h"
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

// Detalii rețea Wi-Fi 
const char *ssid = "Ghpy1668";
const char *password = "ghpy1668";

// Obiecte pentru serverul web și WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

MFRC522DriverPinSimple ss_pin(5);
MFRC522DriverSPI driver{ss_pin}; // Create SPI driver
MFRC522 mfrc522{driver};         // Create MFRC522 instance

// Funcție de gestionare a evenimentelor WebSocket
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        
    }
}


void initWebServer() {
    WiFi.begin(ssid, password);
    delay(2000);

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed!");
        return;
    } else {
        Serial.println("SPIFFS Mounted Successfully.");
    }

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.serveStatic("/", SPIFFS, "/");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/index.html", "text/html");
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.println("Server started.");
}


void setup() {
  Serial.begin(115200);  // Initialize serial communication
  while (!Serial);       // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4).

  initWebServer();
  mfrc522.PCD_Init();    // Init MFRC522 board.
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522, Serial);  // Show details of PCD - MFRC522 Card Reader details.
  Serial.println(F("Scan PICC to see UID"));
}

void loop() {
  // Reset the loop if no new card present on the sensor/reader. This saves the entire process when idle.
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // Select one of the cards.
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Save the UID on a String variable
  String uidString = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      uidString += "0"; 
    }
    uidString += String(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println(uidString);
  delay(2000);
  
  ws.cleanupClients();
}
