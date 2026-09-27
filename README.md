# ESP32-CAM Control

Sketch Arduino/ESP-IDF per **ESP32-CAM** (scheda AI-Thinker) che trasforma il modulo in una piccola web-cam controllabile da browser: permette di **scattare foto** e di **registrare video** e scaricarle dalla pagina web, con **cambio modalità Foto/Video a caldo** (senza bisogno di riavviare la scheda) tramite un'unica pagina web servita dal dispositivo stesso.

## Caratteristiche principali

- **Modalità Foto**: cattura a risoluzione massima del sensore (QXGA, 2048×1536) con qualità JPEG.
- **Modalità Video**: registrazione a 12 FPS in risoluzione XGA (1024×768), salvata come file **AVI**.
- **Riconfigurazione live della fotocamera**: passaggio tra modalità Foto e Video senza riavviare l'ESP32.
- **Interfaccia web integrata**: singola pagina HTML/CSS/JS con pulsanti per scattare foto, avviare/interrompere la registrazione e scaricare il video.
- **Storage su SD card** tramite `SD_MMC`.
- **Server asincrono** basato su [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer), con IP statico configurabile.

## Hardware richiesto

- Scheda **ESP32-CAM (AI-Thinker)**
- Modulo camera **OV2640** (integrato nella scheda AI-Thinker)
- Scheda **microSD** (formattata FAT32)
- Programmatore seriale USB-TTL (FTDI o simile) per il flashing, dato che l'ESP32-CAM non ha porta USB integrata
- Alimentazione stabile a 5V (il picco di corrente della camera può causare brownout/riavvii con alimentazioni deboli)

### Pinout utilizzato (AI-Thinker)

| Segnale | GPIO | Segnale | GPIO |
|---|---|---|---|
| PWDN | 32 | Y6 | 36 |
| RESET | -1 (non usato) | Y5 | 21 |
| XCLK | 0 | Y4 | 19 |
| SIOD | 26 | Y3 | 18 |
| SIOC | 27 | Y2 | 5 |
| Y9 | 35 | VSYNC | 25 |
| Y8 | 34 | HREF | 23 |
| Y7 | 39 | PCLK | 22 |

## Requisiti software

- [Arduino IDE](https://www.arduino.cc/en/software) (o PlatformIO) con supporto schede **ESP32** installato
- Libreria **[ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)**
- Libreria **[AsyncTCP](https://github.com/me-no-dev/AsyncTCP)** (dipendenza di ESPAsyncWebServer)
- Librerie incluse nel core ESP32: `esp_camera.h`, `WiFi.h`, `FS.h`, `SD_MMC.h`

## Configurazione

Prima di caricare lo sketch, modifica questi parametri nel codice:

### Credenziali WiFi

- const char* ssid = nome della rete wifi a cui l'esp32 si deve agganciare;
- const char* password = password della rete wifi;

## Utilizzo

1. Collega la ESP32-CAM al programmatore USB-TTL (ricorda di ponticellare **GPIO0 a GND** per entrare in modalità flashing).
2. Carica lo sketch da Arduino IDE.
3. Rimuovi il ponticello su GPIO0 e riavvia la scheda.
4. Apri il Monitor Seriale (115200 baud) per verificare la connessione WiFi e recuperare l'IP assegnato.
5. Apri un browser e vai su `http://<IP-ESP32-CAM>/` (es. `http://10.32.65.150/`).
6. Dall'interfaccia web puoi:
   - Cliccare **"Scatta e Scarica Foto"** per catturare e scaricare una foto in alta risoluzione.
   - Cliccare **"Avvia Registrazione"** per iniziare una registrazione video.
   - Cliccare **"Interrompi Registrazione"** per fermarla e ottenere il link di download del file `.avi`.
