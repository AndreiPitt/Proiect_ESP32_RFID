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
MFRC522DriverSPI driver{ss_pin};
MFRC522 mfrc522{driver};         

// NOU: Funcție pentru salvarea datelor în users.json
bool saveUsersToFile(JsonArray users) {
    File file = SPIFFS.open("/users.json", "w");
    if (!file) {
        Serial.println("Eroare la deschiderea fisierului pentru scriere!");
        return false;
    }
    
    // Serializarea documentului JSON în fișier
    if (serializeJson(users, file) == 0) {
        Serial.println("Eroare la scrierea in fisier!");
        file.close();
        return false;
    }
    file.close();
    Serial.println("Users.json salvat cu succes!");
    return true;
}

// Funcție de gestionare a evenimentelor WebSocket
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        // Procesează mesajele primite de la client (browser)
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return;
        }

        const char* messageType = doc["type"];
        
        if (strcmp(messageType, "REGISTER") == 0) {
            // Se primește o cerere de înregistrare
            Serial.println("Cerere de inregistrare primita...");
            
            // Citim datele din users.json
            File file = SPIFFS.open("/users.json", "r");
            if (!file) {
                Serial.println("Eroare la deschiderea users.json pentru citire!");
                return;
            }

            StaticJsonDocument<1024> usersDoc;
            DeserializationError err = deserializeJson(usersDoc, file);
            file.close();

            if (err) {
                Serial.print(F("Eroare la parsarea users.json: "));
                Serial.println(err.f_str());
                return;
            }

            JsonArray users = usersDoc.to<JsonArray>();
            
            // Creăm obiectul noului utilizator
            JsonObject newUser = users.createNestedObject();
            newUser["UID"] = doc["uid"].as<String>();
            newUser["Nume"] = doc["Nume"].as<String>();
            newUser["Prenume"] = doc["Prenume"].as<String>();
            newUser["Rol"] = doc["Rol"].as<String>();
            
            // Salvăm noul array în fișier
            if (saveUsersToFile(users)) {
                // Trimitere mesaj de succes la client (browser)
                StaticJsonDocument<256> responseDoc;
                responseDoc["type"] = "REGISTER_SUCCESS";
                responseDoc["user"] = newUser; 
                
                String response;
                serializeJson(responseDoc, response);
                client->text(response);
            }
        }
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
    // Permite browser-ului sa citeasca users.json de pe SPIFFS
    server.on("/users.json", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/users.json", "application/json");
    });
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.println("Server started.");
}


void setup() {
  Serial.begin(115200);
  while (!Serial);

  initWebServer();
  mfrc522.PCD_Init();
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522, Serial);
  Serial.println(F("Scan PICC to see UID"));
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  String uidString = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      uidString += "0";
    }
    uidString += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidString.toUpperCase();

  Serial.print("UID scanat: ");
  Serial.println(uidString);
  
  // Trimitere UID catre clientul WebSocket
  StaticJsonDocument<128> doc;
  doc["type"] = "UID_SCAN";
  doc["uid"] = uidString;
  
  String jsonOutput;
  serializeJson(doc, jsonOutput);
  ws.textAll(jsonOutput);

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  ws.cleanupClients();
}
