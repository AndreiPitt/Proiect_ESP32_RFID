#include <Arduino.h>
#include <SPIFFS.h>

// ---------------------------------------------------------------------
// PROGRAM MINIMAL PENTRU GOLIREA (FORMATAREA) SPAȚIULUI DE STOCARE SPIFFS
// ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("--------------------------------------------------");
  Serial.println("Pornire program de formatare SPIFFS.");

  // De-inițializăm SPIFFS (dacă a fost montat de un program anterior)
  SPIFFS.end(); 

  // Formatarea SPIFFS: Șterge complet toate fișierele și reinițializează sistemul de fișiere
  Serial.println(">>> INCEPE FORMATAREA SPIFFS. ACEASTA POATE DURA CATEVA SECUNDE.");
  
  if (SPIFFS.format()) {
    Serial.println("!!! FORMATARE SPIFFS REUSITA !!!");
  } else {
    Serial.println("!!! EROARE LA FORMATAREA SPIFFS !!!");
  }

  // Încercăm montarea SPIFFS după formatare pentru a verifica dacă totul este OK
  if (SPIFFS.begin(false)) { // Folosim 'false' ca să nu mai încerce formatarea automată
    Serial.println("SPIFFS montat cu succes dupa formatare. Memoria este goala.");
    
    // Verificare rapidă: ar trebui să fie 0 bytes folosiți
    Serial.printf("Total spatiu: %lu bytes, Spatiu liber: %lu bytes\n", 
      SPIFFS.totalBytes(), SPIFFS.usedBytes());
      
  } else {
    Serial.println("EROARE: Nu se poate monta SPIFFS-ul proaspat formatat.");
  }
  
  Serial.println("Formatare terminata. Modulul nu face altceva in loop.");
  Serial.println("--------------------------------------------------");
}

void loop() {
  // Nu face nimic, deoarece scopul este doar formatarea la pornire
  delay(100); 
}
