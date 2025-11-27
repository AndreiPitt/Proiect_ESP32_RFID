#include "WiFi.h"
#include <HTTPClient.h> 
#include <SPI.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

// ------------------------------------
// 1. Configurarea Retelei si Serverului
// ------------------------------------
const char *ssid = "Ghpy1668";
const char *password = "ghpy1668";
const char* flaskServer = "http://10.199.207.102:5000"; 


// ------------------------------------
// 2. Configurarea Hardware RFID si Feedback
// ------------------------------------
MFRC522DriverPinSimple ss_pin(5); // Pinul SS (SDA) la pinul 5
MFRC522DriverSPI driver{ss_pin};
MFRC522 mfrc522{driver};

#define LED_VERDE 2    // Pinul LED-ului de acces OK (IN/OUT)
#define LED_ROSU 4     // Pinul LED-ului de respingere/eroare (403, 429, WiFi error)

// ------------------------------------
// 3. Logica Anti-Spam Hardware (3 secunde)
// ------------------------------------
long lastCardReadTime = 0;
const long scanInterval = 3000; 

// ------------------------------------
// Functii Utilitare
// ------------------------------------

/**
 * Convertește un array de bytes (UID) într-un String de HEX majuscule.
 */
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

/**
 * Functie pentru feedback vizual simplu (flash LED).
 */
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
        
        // **********************************************
        // NOU: APEL DE WARM-UP PENTRU A EVITA EROAREA -1 (ARP Cache Miss)
        // **********************************************
        Serial.println("Warm-up request in progress...");
        HTTPClient http_warmup;
        // Apelam pagina de baza pentru a stabili conexiunea TCP/IP inainte de prima scanare
        http_warmup.begin(String(flaskServer) + "/"); 
        http_warmup.GET(); // Ignoram raspunsul
        http_warmup.end(); 
        Serial.println("Warm-up complete. Ready to scan.");
        // **********************************************
        
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
    // Verifică dacă există un card nou
    if (!mfrc522.PICC_IsNewCardPresent()) {
        return;
    }

    // Citeste UID-ul
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
        String serverPath = String(flaskServer) + String("/scan/") + uidString;
        
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
                Serial.println("--- EROARE: Card Neînregistrat ---");
                
                // CONSTRUIESTE LINKUL DE INREGISTRARE PE RETEAUA LOCALA
                String registrationLink = String(flaskServer) + "/student_register?uid=" + uidString;
                
                Serial.println("\n---------------------------------------------------------");
                Serial.println("Cardul Dvs. nu este inregistrat.");
                Serial.println("Va rugam accesati acest link de pe un dispozitiv conectat la aceeasi retea Wi-Fi:");
                Serial.println(registrationLink);
                Serial.println("---------------------------------------------------------\n");

            }
            else {
                // Alte erori (ex: 500 Internal Server Error)
                feedback(LED_ROSU, 1000);
                Serial.println("--- EROARE NECUNOSCUTA ---\n");
            }
        } else {
            Serial.print("Eroare de conexiune la server. Cod: ");
            Serial.println(httpResponseCode);
            feedback(LED_ROSU, 1000);
        }
        
        http.end(); // Închide conexiunea
        
    } else {
        Serial.println("Eroare: Conexiunea Wi-Fi nu este activa!\n");
        feedback(LED_ROSU, 1000);
    }

    // Resetarea stării cardului și timpul de citire
    lastCardReadTime = millis();
    mfrc522.PICC_HaltA(); 
    mfrc522.PCD_StopCrypto1(); 
}
