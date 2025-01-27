# Aestus
Bottle with battery powered level sensor


# Aestus

Projekt **Aestus** je jednoduchý vodní senzor využívající ESP32. Měří hladinu vody pomocí touch pinu, ukládá hodnoty do FIFO bufferu a při připojení k Wi-Fi odesílá data do Firebase Realtime Database.

## Funkce
- Pravidelné měření hladiny (průměr z `maxSamples`).
- Ukládání do FIFO bufferu, když Wi-Fi není dostupná.
- Synchronizace času přes NTP (pro uložení timestampů).
- Light Sleep mód ESP32 pro úsporu energie.

## Hardwarové požadavky
- ESP32 (touch pin: `T11`).
- Tlačítko na pinu 16 (pro probuzení).
- LED dioda na pinu 15 (indikuje měření).

## Použití
1. doplňte:
   ```cpp
   #pragma once
   #define WIFI_SSID "NazevVasiSite"
   #define WIFI_PASS "HesloKSite"
   #define FIREBASE_HOST "https://..."
   #define FIREBASE_AUTH "..."
