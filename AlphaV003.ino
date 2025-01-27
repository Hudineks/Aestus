#include <esp_sleep.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <time.h> // Pro práci s časem

// Firebase konfigurace
#define FIREBASE_HOST "https://aestus-8eaf8-default-rtdb.europe-west1.firebasedatabase.app/"
#define FIREBASE_AUTH "AIzaSyDWWIJv10gA7lXgbtWQ90Y_2rVG_-2RlOY"

// Nastavení tlačítka a LED
#define BUTTON_PIN 16
#define LED_PIN 15

const char* ssid = "simoon";
const char* password = "12345678";

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

struct Measurement {
    int waterLevel;
    unsigned long timestamp;
};

Measurement fifoBuffer[10]; // FIFO buffer pro uložené hodnoty
int fifoIndex = 0;

const int touchPin = T11; // Touch pin
const int maxSamples = 10;
int WaterLevel = 0;

// Nastavení časové zóny (ČR: UTC+1, letní čas: +1 hodina)
const long gmtOffset_sec = 3600; // UTC+1
const int daylightOffset_sec = 3600; // Letní čas

unsigned long lastEpochTime = 0; // Poslední známý epoch čas
unsigned long lastMillis = 0;   // Čas v millis() při poslední synchronizaci

void setup() {
 
  // Nastavení pinu tlačítka jako vstup s pull-up rezistorem
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Nastavení pinu LED jako výstup
  pinMode(LED_PIN, OUTPUT);

  // Nastavení Firebase
    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  
  // Konfigurace EXT0 pro probuzení z light sleep režimu
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 1); // Probuzení při úrovni LOW

  esp_light_sleep_start();
}
 

void loop() {
  // Získání důvodu probuzení
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  int sum = 0;
  // Blikání LED a měření
  for (int i = 0; i < maxSamples; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
    sum += touchRead(touchPin);
  }

  WaterLevel = sum / maxSamples;

  // Připojení k Wi-Fi
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  const unsigned long wifiTimeout = 30000;

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
      delay(1000);
  }

  // Synchronizace času přes NTP
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
    delay(1000);

  unsigned long timestamp;
  if (WiFi.status() == WL_CONNECTED) {
      // Synchronizace času při připojení k Wi-Fi
      timestamp = time(nullptr); // Získání aktuálního epoch času
      if (timestamp > 0) {
          lastEpochTime = timestamp;      // Uložení aktuálního epoch času
          lastMillis = millis();          // Uložení aktuálního času v millis()
      }
  } else {
      // Pokud není připojení k Wi-Fi, vypočítej čas pomocí posledního známého času
      if (lastEpochTime > 0) {
          unsigned long elapsedMillis = millis() - lastMillis;
          timestamp = lastEpochTime + (elapsedMillis / 1000); // Přepočet na sekundy
      } else {
          timestamp = 0; // Pokud nemáme žádný známý čas, nastavíme 0
      }
  }

  if (timestamp == 0) {
      Serial.println("Čas není dostupný. Data nebudou odeslána.");
      return;
  }

  Serial.printf("Aktuální timestamp: %lu\n", timestamp);

  // Zde zpracuj měření, odeslání do Firebase, nebo uložení do FIFO bufferu
  FirebaseJson json;
  json.set("value", WaterLevel);
  json.set("timestamp", timestamp);

  if (WiFi.status() == WL_CONNECTED) {
      // Odeslání uložených hodnot z FIFO při připojení k Wi-Fi
      for (int i = 0; i < fifoIndex; i++) {
          FirebaseJson fifoJson;
          fifoJson.set("value", fifoBuffer[i].waterLevel);
          fifoJson.set("timestamp", fifoBuffer[i].timestamp);

          if (Firebase.RTDB.setJSON(&fbdo, "/water_level_logs/" + String(fifoBuffer[i].timestamp), &fifoJson)) {
              Serial.println("FIFO data odeslána do Firebase");
              // Posuň buffer
              for (int j = i; j < fifoIndex - 1; j++) {
                  fifoBuffer[j] = fifoBuffer[j + 1];
              }
              fifoIndex--;
              i--; // Znovu zpracuj tento index
          } else {
              break; // Pokud odeslání selže, zastav další pokusy
          }
      }

      // Odeslání aktuální hodnoty
      if (Firebase.RTDB.setJSON(&fbdo, "/water_level_logs/" + String(timestamp), &json)) {
          Serial.println("Data odeslána do Firebase");
      } else {
          Serial.println("Chyba při odesílání dat do Firebase.");
      }
  } else {
      if (fifoIndex < 10) {
          fifoBuffer[fifoIndex].waterLevel = WaterLevel;
          fifoBuffer[fifoIndex].timestamp = timestamp;
          fifoIndex++;
      } else {
          Serial.println("FIFO buffer je plný.");
      }
  }

  // Odpojení Wi-Fi před spánkem
  WiFi.disconnect(true);

  // Přechod do light sleep režimu
  esp_light_sleep_start();
}
