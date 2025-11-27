#include "WiFi.h"
#include <HTTPClient.h> 
#include <SPI.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

// --- Configurarea Retelei (Preluata din codul dvs. original) ---
const char *ssid = "Ghpy1668";
const char *password = "ghpy1668";

const char* flaskServer = "http://10.199.207.102:5000"; 


MFRC522DriverPinSimple ss_pin(5);
MFRC522DriverSPI driver{ss_pin};
MFRC522 mfrc522{driver};

// --- Configurarea Pini de Feedback ---
#define LED_VERDE 2  // Pinul LED-ului de acces (IN) (Puteți schimba pinul)
#define LED_ROSU 4   // Pinul LED-ului de respingere/eroare (Puteți schimba pinul)

// --- Logica Anti-Spam Hardware ---
// Previne citirea cardului de mai multe ori imediat după o scanare reușită
long lastCardReadTime = 0;
const long scanInterval = 3000; // Așteaptă 3 secunde între citirile fizice

// ------------------------------------
// Functii Utilitare
// ------------------------------------

// Functie pentru a transforma UID-ul in format String (HEX majuscule)
String uidToString(byte *buffer, byte bufferSize) {
    String uidString = "";
    for (byte i = 0; i < bufferSize; i++) {
        if (buffer[i] < 0x10) {
            uidString += '0';
        }
        uidString += String(buffer[i], HEX);
    }
    uidString.toUpperCase(); 
    return uidString;
}

// Functie pentru feedback vizual/sonor simplu
void feedback(int ledPin, int delayMs) {
    digitalWrite(ledPin, HIGH);
    delay(delayMs);
    digitalWrite(ledPin, LOW);
}

// ------------------------------------
// Setup
// ------------------------------------
void setup() {
    Serial.begin(115200);
    SPI.begin(); // Initializarea SPI bus
    mfrc522.PCD_Init(); // Initializarea MFRC522
    
    // Setarea pinilor de feedback ca OUTPUT
    pinMode(LED_VERDE, OUTPUT);
    pinMode(LED_ROSU, OUTPUT);

    // Conectarea la Wi-Fi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected to Wi-Fi!");
        Serial.print("Local IP address: ");
        Serial.println(WiFi.localIP());
        feedback(LED_VERDE, 500); // Semnal ca s-a conectat
    } else {
        Serial.println("\nFailed to connect to Wi-Fi!");
        feedback(LED_ROSU, 1000); // Semnal ca nu s-a putut conecta
    }

    Serial.println(F("Ready to scan PICC."));
}

// ------------------------------------
// Loop Principal
// ------------------------------------
void loop() {
    if (!mfrc522.PICC_IsNewCardPresent()) {
        return;
    }

    if (!mfrc522.PICC_ReadCardSerial()) {
        return;
    }
    
    // 1. Logica Anti-Spam Hardware
    if (millis() - lastCardReadTime < scanInterval) {
        Serial.println("--- Scanați prea repede. Așteptați intervalul hardware.");
        mfrc522.PICC_HaltA(); 
        mfrc522.PCD_StopCrypto1(); 
        return;
    }

    // 2. Preia UID-ul cardului
    String uidString = uidToString(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.print("\nCard UID citit: ");
    Serial.println(uidString);
    
    // 3. Trimite cererea HTTP catre serverul Flask
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String serverPath = flaskServer + String("/scan/") + uidString;
        
        Serial.print("Apelare API: ");
        Serial.println(serverPath);
        
        http.begin(serverPath);
        
        int httpResponseCode = http.GET();
        
        Serial.print("Cod Raspuns HTTP: ");
        Serial.println(httpResponseCode);

        // 4. Interpreteaza raspunsul Flask
        if (httpResponseCode > 0) {
            String payload = http.getString();
            Serial.print("Payload: ");
            Serial.println(payload);

            if (httpResponseCode == 200) {
                // Acces OK (IN sau OUT)
                feedback(LED_VERDE, 200);
                Serial.println("--- SUCCESS ---");
            } 
            else if (httpResponseCode == 429) {
                // Cooldown activ (Anti-spam Flask - 5 minute)
                feedback(LED_ROSU, 50); 
                delay(50);
                feedback(LED_ROSU, 50); 
                Serial.println("--- COOLDOWN (Server) ---");
            } 
            else if (httpResponseCode == 403) {
                // Card neînregistrat
                feedback(LED_ROSU, 500);
                Serial.println("--- FORBIDDEN (Card Necunoscut) ---");
            }
            else {
                // Alte erori (ex: 500 Internal Server Error)
                feedback(LED_ROSU, 1000);
                Serial.println("--- EROARE NECUNOSCUTA ---");
            }
        } else {
            Serial.print("Eroare de conexiune la server.");
            feedback(LED_ROSU, 1000);
        }
        
        http.end(); // Închide conexiunea
        
    } else {
        Serial.println("Eroare: Conexiunea Wi-Fi nu este activa!");
        feedback(LED_ROSU, 1000);
    }

    // Resetarea stării cardului
    lastCardReadTime = millis();
    mfrc522.PICC_HaltA(); 
    mfrc522.PCD_StopCrypto1(); 
}
