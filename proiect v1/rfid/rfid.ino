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

// Dimensiune estimată pentru DynamicJsonDocument (2KB ar trebui să fie de ajuns pentru ~15-20 useri)
// Dacă aveți mai mulți, măriți această valoare!
const size_t JSON_DOC_SIZE = 2048; 


// Funcție pentru salvarea datelor în users.json (Acesta suprascrie întotdeauna cu array-ul complet)
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
            Serial.println("Cerere de inregistrare primita...");
            
            // 1. Citim datele din users.json (folosim DynamicJsonDocument pentru dimensiune flexibilă)
            File file = SPIFFS.open("/users.json", "r");
            
            // ATENȚIE: Am schimbat StaticJsonDocument în DynamicJsonDocument
            DynamicJsonDocument usersDoc(JSON_DOC_SIZE);
            JsonArray users; // Declaram JsonArray

            if (file) {
                // Încercăm să citim fișierul existent
                DeserializationError err = deserializeJson(usersDoc, file);
                file.close();

                if (err) {
                    Serial.print(F("Eroare la parsarea users.json existent: "));
                    Serial.println(err.f_str());
                    // Dacă eșuează, vom trata fișierul ca fiind gol (array gol)
                    users = usersDoc.to<JsonArray>(); 
                } else {
                    // Parsarea a reușit, obținem array-ul existent
                    users = usersDoc.as<JsonArray>();
                }
            } else {
                Serial.println("users.json nu exista sau eroare la deschidere. Se creeaza un array gol.");
                // Fișierul nu există, inițializăm un array JSON gol
                users = usersDoc.to<JsonArray>();
            }

            // 2. Creăm obiectul noului utilizator și îl adăugăm la array-ul existent
            // ATENȚIE: users.createNestedObject() adaugă noul obiect în usersDoc/users
            JsonObject newUser = users.createNestedObject();
            newUser["UID"] = doc["uid"].as<String>();
            newUser["Nume"] = doc["Nume"].as<String>();
            newUser["Prenume"] = doc["Prenume"].as<String>();
            newUser["Rol"] = doc["Rol"].as<String>();
            
            // 3. Salvăm array-ul complet (vechi + nou) în fișier, suprascriind vechiul conținut
            if (saveUsersToFile(users)) {
                // Trimitere mesaj de succes la client (browser)
                StaticJsonDocument<256> responseDoc;
                responseDoc["type"] = "REGISTER_SUCCESS";
                responseDoc["user"] = newUser; 
                
                String response;
                serializeJson(responseDoc, response);
                client->text(response);
            } else {
                // Trimitere mesaj de eșec
                client->text("{\"type\":\"REGISTER_FAIL\",\"message\":\"Eroare la salvarea pe ESP32\"}");
            }
        }
    }
}


void initWebServer() {
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected.");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nFailed to connect to WiFi.");
        return;
    }


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
