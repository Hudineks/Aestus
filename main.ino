#include <esp_sleep.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <time.h> 

//   #define WIFI_SSID "TvojeSit"
//   #define WIFI_PASS "TvojeHeslo"
//   #define FIREBASE_HOST "..."

// #define WIFI_SSID "ZDE_DOPLNI_SSID"
// #define WIFI_PASS "ZDE_DOPLNI_HESLO"
// #define FIREBASE_HOST "https://...."
// #define FIREBASE_AUTH "..."

#define FIREBASE_AUTH  "AIzaSy..............."   

// Nastavení tlačítka a LED
#define BUTTON_PIN 16
#define LED_PIN 15

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
const long gmtOffset_sec = 3600;         // UTC+1
const int daylightOffset_sec = 3600;     // +1 hodina pro letní čas

unsigned long lastEpochTime = 0; // Poslední známý epoch čas
unsigned long lastMillis = 0;    // Čas v millis() při poslední synchronizaci

void setup() {
  // Nastavení pinu tlačítka jako vstup s pull-up rezistorem
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Nastavení pinu LED jako výstup
  pinMode(LED_PIN, OUTPUT);

  // Firebase konfigurace
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Konfigurace EXT0 pro probuzení z light sleep režimu
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 1); // Probuzení při úrovni LOW

  // Light sleep start
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
  WiFi.begin(WIFI_SSID, WIFI_PASS);
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
    timestamp = time(nullptr); // Získání aktuálního epoch času
    if (timestamp > 0) {
      lastEpochTime = timestamp; // Uložení aktuálního epoch času
      lastMillis = millis();     // Uložení aktuálního času v millis()
    }
  } else {
    // Pokud není připojení k Wi-Fi, vypočítej čas pomocí posledního známého času
    if (lastEpochTime > 0) {
      unsigned long elapsedMillis = millis() - lastMillis;
      timestamp = lastEpochTime + (elapsedMillis / 1000); 
    } else {
      timestamp = 0; 
    }
  }

  if (timestamp == 0) {
    Serial.println("Čas není dostupný. Data nebudou odeslána.");
    return;
  }

  Serial.printf("Aktuální timestamp: %lu\n", timestamp);

  // Odeslání do Firebase (nebo zpracování FIFO)
  FirebaseJson json;
  json.set("value", WaterLevel);
  json.set("timestamp", timestamp);

  if (WiFi.status() == WL_CONNECTED) {
    // Odeslání FIFO
    for (int i = 0; i < fifoIndex; i++) {
      FirebaseJson fifoJson;
      fifoJson.set("value", fifoBuffer[i].waterLevel);
      fifoJson.set("timestamp", fifoBuffer[i].timestamp);

      if (Firebase.RTDB.setJSON(&fbdo, "/water_level_logs/" + String(fifoBuffer[i].timestamp), &fifoJson)) {
        Serial.println("FIFO data odeslána do Firebase");
        for (int j = i; j < fifoIndex - 1; j++) {
          fifoBuffer[j] = fifoBuffer[j + 1];
        }
        fifoIndex--;
        i--;
      } else {
        break;
      }
    }

    // Odeslání aktuální hodnoty
    if (Firebase.RTDB.setJSON(&fbdo, "/water_level_logs/" + String(timestamp), &json)) {
      Serial.println("Data odeslána do Firebase");
    } else {
      Serial.println("Chyba při odesílání dat do Firebase.");
    }
  } else {
    // Uložení do FIFO
    if (fifoIndex < 10) {
      fifoBuffer[fifoIndex].waterLevel = WaterLevel;
      fifoBuffer[fifoIndex].timestamp = timestamp;
      fifoIndex++;
    } else {
      Serial.println("FIFO buffer je plný.");
    }
  }

  // Odpojení Wi-Fi
  WiFi.disconnect(true);

  // Light sleep
  esp_light_sleep_start();
}
