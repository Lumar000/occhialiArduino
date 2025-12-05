// =====================================================================
//      SKETCH UNIFICATO ESP32-CAM (v9.0 - Riconfigurazione Live)
//      Pagina singola per foto e video con cambio modalità senza riavvio.
// =====================================================================

#include "esp_camera.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "FS.h"
#include "SD_MMC.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// PINOUT per la scheda AI THINKER ESP32-CAM (invariato)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Credenziali WiFi
const char* ssid = "lumar0000";
const char* password = "ciaociao";

// Server
AsyncWebServer server(80);

// Gestione della modalità
#define MODE_VIDEO 0
#define MODE_PHOTO 1
volatile int current_mode = MODE_VIDEO; // Partiamo in modalità Video di default

// Variabili di stato per video
enum RecordingState { REC_IDLE, REC_START_REQUESTED, REC_RECORDING, REC_STOP_REQUESTED };
volatile RecordingState recording_state = REC_IDLE;
String video_filename;
File videoFile;
int frame_count = 0;
const int TARGET_FPS = 12;
const unsigned long FRAME_INTERVAL_MS = 1000 / TARGET_FPS;
unsigned long last_frame_time = 0;

// [Sezione gestione AVI invariata]
// ... (tutto il codice per la gestione AVI rimane identico) ...
typedef struct { uint32_t riff_id; uint32_t file_size; uint32_t avi_id; } RiffHeader;
typedef struct { uint32_t list_id; uint32_t list_size; uint32_t list_type; } ListHeader;
typedef struct { uint32_t chunk_id; uint32_t chunk_size; uint32_t us_per_frame; uint32_t max_bytes_per_sec; uint32_t padding_granularity; uint32_t flags; uint32_t total_frames; uint32_t initial_frames; uint32_t streams; uint32_t suggested_buffer_size; uint32_t width; uint32_t height; uint32_t reserved[4]; } MainAviHeader;
typedef struct { uint32_t chunk_id; uint32_t chunk_size; uint32_t fcc_type; uint32_t fcc_handler; uint32_t flags; uint16_t priority; uint16_t language; uint32_t initial_frames; uint32_t scale; uint32_t rate; uint32_t start; uint32_t length; uint32_t suggested_buffer_size; uint32_t quality; uint32_t sample_size; struct { int16_t left; int16_t top; int16_t right; int16_t bottom; } rc_frame; } AviStreamHeader;
typedef struct { uint32_t chunk_id; uint32_t chunk_size; uint32_t bi_size; uint32_t bi_width; uint32_t bi_height; uint16_t bi_planes; uint16_t bi_bit_count; uint32_t bi_compression; uint32_t bi_size_image; uint32_t bi_x_pels_per_meter; uint32_t bi_y_pels_per_meter; uint32_t bi_clr_used; uint32_t bi_clr_important; } BitmapInfoHeader;
typedef struct { uint32_t chunk_id; uint32_t chunk_size; } ChunkHeader;
const uint32_t AVIOFFSET = 240;

void get_resolution(framesize_t frame_size, int &width, int &height) {
    switch (frame_size) {
    case FRAMESIZE_VGA:    
        width = 640; 
        height = 480;
        break;
    case FRAMESIZE_UXGA:   
        width = 1600; 
        height = 1200;
        break;
    default: 
        width = 640; 
        height = 480; 
    }
}

bool add_avi_frame(File& file, camera_fb_t* fb) {
    if (!file) 
        return false; 
    ChunkHeader frame_header = {0x63643030, fb->len};
    if (file.write((uint8_t*)&frame_header, sizeof(ChunkHeader)) != sizeof(ChunkHeader)) 
        return false;
    if (file.write(fb->buf, fb->len) != fb->len) 
        return false;
    if (fb->len % 2 != 0) { 
        uint8_t padding = 0; 
        if (file.write(&padding, 1) != 1) 
            return false; 
    }
    return true;
}
void end_avi(File& file) {
    if (!file) return; 
    uint32_t file_size = file.size(); 
    sensor_t *s = esp_camera_sensor_get(); 
    int width = 0, height = 0; 
    get_resolution(s->status.framesize, width, height);
    RiffHeader riff_header = {0x46464952, file_size - 8, 0x20495641}; 
    ListHeader hdrl_list = {0x5453494c, 192, 0x6C726468}; 
    MainAviHeader avih = {0x68697661, 56, 1000000 / TARGET_FPS, (uint32_t)(width * height * TARGET_FPS), 0, 0x10, (uint32_t)frame_count, 0, 1, (uint32_t)(width * height * 2), (uint32_t)width, (uint32_t)height, {0,0,0,0}};
    ListHeader strl_list = {0x5453494c, 116, 0x6C727473}; 
    AviStreamHeader strh = {0x68727473, 56, 0x73646976, 0x47504A4D, 0, 0, 0, 0, 1, TARGET_FPS, 0, (uint32_t)frame_count, (uint32_t)(width*height*2), (uint32_t)-1, 0, {0,0,(int16_t)width,(int16_t)height}};
    BitmapInfoHeader strf = {0x66727473, 40, 40, (uint32_t)width, (uint32_t)height, 1, 24, 0x47504A4D, (uint32_t)(width*height*3), 0, 0, 0, 0}; 
    ListHeader movi_list = {0x5453494c, file_size - AVIOFFSET + 4, 0x69766F6D};
    file.seek(0); 
    file.write((uint8_t*)&riff_header, sizeof(RiffHeader)); 
    file.write((uint8_t*)&hdrl_list, sizeof(ListHeader)); 
    file.write((uint8_t*)&avih, sizeof(MainAviHeader)); 
    file.write((uint8_t*)&strl_list, sizeof(ListHeader));
    file.write((uint8_t*)&strh, sizeof(AviStreamHeader)); 
    file.write((uint8_t*)&strf, sizeof(BitmapInfoHeader)); 
    file.seek(AVIOFFSET - sizeof(ListHeader)); 
    file.write((uint8_t*)&movi_list, sizeof(ListHeader));
    file.close(); Serial.println("File AVI finalizzato correttamente.");
}
// FINE SEZIONE AVI

// NUOVA PAGINA HTML UNIFICATA (CON SINTASSI JS ALTERNATIVA PER COMPATIBILITÀ)
const char HTML_UNIFIED_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><title>ESP32-CAM Control</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body{font-family:Arial,Helvetica,sans-serif;text-align:center;background-color:#f2f2f2;margin:0;padding:15px;}
  h1{color:#333;}
  .container{max-width:800px;margin:auto;background-color:white;padding:20px;border-radius:10px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}
  .control-group{border:1px solid #ddd;border-radius:8px;padding:15px;margin-top:20px;}
  .btn{padding:15px 25px;font-size:1.1em;cursor:pointer;margin:10px;border:none;border-radius:5px;color:white;min-width:220px;}
  .btn-photo{background-color:#4CAF50;}
  .btn-video{background-color:#f44336;}
  .btn-stop{background-color:#757575;}
  .status{font-size:1.1em;color:#555;margin:20px;min-height:1.2em;}
  #mode-status{font-weight:bold;}
  a{color:#008CBA;text-decoration:none;font-size:1.1em;}
</style>
</head><body>
<div class="container">
  <h1>ESP32-CAM Controllo</h1>
  <p id="mode-status" class="status">Caricamento...</p>

  <div class="control-group">
    <h2>Foto</h2>
    <button id="photo-btn" class="btn btn-photo" onclick="takePhoto()">Scatta e Scarica Foto (UXGA)</button>
  </div>

  <div class="control-group">
    <h2>Video</h2>
    <button id="start-btn" class="btn btn-video" onclick="startRec()">Avvia Registrazione (VGA)</button>
    <button id="stop-btn" class="btn btn-stop" onclick="stopRec()" disabled>Interrompi Registrazione</button>
    <div id="download-link" style="margin-top:15px;"></div>
  </div>
  
  <p id="global-status" class="status"></p>
</div>

<script>
  let currentMode = '';
  const photoBtn = document.getElementById('photo-btn');
  const startBtn = document.getElementById('start-btn');
  const stopBtn = document.getElementById('stop-btn');
  const modeStatus = document.getElementById('mode-status');
  const globalStatus = document.getElementById('global-status');
  const downloadLink = document.getElementById('download-link');

  const setStatus = (msg) => { globalStatus.innerText = msg; }
  const setModeStatus = (mode) => { modeStatus.innerText = "Modalita' attuale: " + (mode === 'photo' ? 'FOTO' : 'VIDEO'); }

  const switchMode = async (mode) => {
    setStatus(`Cambio modalita' in ${mode.toUpperCase()}...`);
    const response = await fetch(`/switch-mode?mode=${mode}`);
    if (!response.ok) {
      setStatus(`Errore: cambio modalita' fallito!`);
      throw new Error('Mode switch failed');
    }
    currentMode = mode;
    setModeStatus(currentMode);
    setStatus('');
  };

  const takePhoto = async () => {
    photoBtn.disabled = true;
    startBtn.disabled = true;
    try {
      if (currentMode !== 'photo') {
        await switchMode('photo');
      }
      setStatus("Scatto in corso...");
      window.location.href = '/capture-photo';
      setTimeout(() => { setStatus(''); }, 4000);
    } catch (e) {
      console.error(e);
    }
    photoBtn.disabled = false;
    startBtn.disabled = false;
  };

  const startRec = async () => {
    photoBtn.disabled = true;
    startBtn.disabled = true;
    try {
      if (currentMode !== 'video') {
        await switchMode('video');
      }
      setStatus("Avvio registrazione...");
      await fetch('/start-rec');
      setStatus('🔴 Registrazione in corso...');
      stopBtn.disabled = false;
      downloadLink.innerHTML = '';
    } catch (e) {
      console.error(e);
      photoBtn.disabled = false;
      startBtn.disabled = false;
    }
  };

  const stopRec = async () => {
    setStatus('Finalizzazione video...');
    const response = await fetch('/stop-rec');
    const filename = await response.text();
    setStatus('Video salvato!');
    downloadLink.innerHTML = `<a href="/download?file=${filename}" target="_blank">Scarica ${filename.substring(1)}</a>`;
    photoBtn.disabled = false;
    startBtn.disabled = false;
    stopBtn.disabled = true;
  };

  document.addEventListener('DOMContentLoaded', async () => {
    try {
      const response = await fetch('/current-mode');
      currentMode = await response.text();
      setModeStatus(currentMode);
    } catch (e) {
      modeStatus.innerText = "Errore di connessione con la CAM.";
    }
  });
</script>
</body></html>
)HTML";

// SOSTITUISCI QUESTA FUNZIONE CON LA NUOVA VERSIONE
// SOSTITUISCI QUESTA FUNZIONE CON LA VERSIONE PER QUALITÀ ESTREMA
esp_err_t configure_camera(int mode) {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.pixel_format = PIXFORMAT_JPEG;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.xclk_freq_hz = 20000000;

    if (mode == MODE_PHOTO) {
        Serial.println("Configurazione per MODALITA' FOTO (Qualita' ESTREMA).");
        config.frame_size = FRAMESIZE_QXGA;  // Massima risoluzione del sensore (2048x1536)
        config.jpeg_quality = 5;             // Qualita' JPEG massima in assoluto
        config.fb_count = 2; // fb_count = 2 è consigliato per risoluzioni alte
    } else { // MODE_VIDEO
        Serial.println("Configurazione per MODALITA' VIDEO (Qualita' ESTREMA).");
        config.frame_size = FRAMESIZE_XGA;   // Risoluzione video massima sperimentale (1024x768)
        config.jpeg_quality = 10;            // Qualita' gia' molto alta per il video
        config.fb_count = 2;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Inizializzazione camera fallita con errore 0x%x\n", err);
        return err;
    }
    
    Serial.println("Riscaldamento sensore...");
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        esp_camera_fb_return(fb);
        Serial.println("Buffer pulito.");
    }

    current_mode = mode;
    return ESP_OK;
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);
    Serial.println("\n\nAvvio ESP32-CAM...");

    if(!SD_MMC.begin("/sdcard", true)){
        Serial.println("Montaggio SD Card fallito!");
        return;
    }
    Serial.println("SD Card inizializzata.");

    if (configure_camera(MODE_VIDEO) != ESP_OK) {
        Serial.println("ERRORE CRITICO: impossibile avviare la fotocamera.");
        return;
    }

// --- NUOVA CONFIGURAZIONE CON IP STATICO ---

    // 1. Definisci le informazioni della tua rete (da personalizzare!)
    IPAddress local_IP(10, 32, 65, 150); // L'IP fisso che hai scelto per l'ESP32
    IPAddress gateway(10, 32, 65, 1);     // L'indirizzo del gateway (il tuo telefono)
    IPAddress subnet(255, 255, 255, 0);   // La subnet mask

    // 2. Applica la configurazione statica
    // Questa funzione DEVE essere chiamata prima di WiFi.begin()
    if (!WiFi.config(local_IP, gateway, subnet)) {
        Serial.println("Errore nella configurazione dell'IP statico!");
        return; // Ferma l'esecuzione se la configurazione fallisce
    }

    // 3. Ora avvia la connessione Wi-Fi come prima
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnesso!");
    Serial.print("Indirizzo IP FISSO: http://"); Serial.println(WiFi.localIP());

    // --- Definizione Endpoint Server ---
    
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", HTML_UNIFIED_PAGE);
    });

    server.on("/current-mode", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", (current_mode == MODE_PHOTO) ? "photo" : "video");
    });

    server.on("/switch-mode", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("mode")) {
            String modeStr = request->getParam("mode")->value();
            int new_mode = (modeStr == "photo") ? MODE_PHOTO : MODE_VIDEO;

            if (new_mode != current_mode) {
                Serial.println("De-inizializzo la fotocamera...");
                esp_camera_deinit();
                Serial.println("Riconfiguro la fotocamera...");
                if (configure_camera(new_mode) == ESP_OK) {
                    request->send(200, "text/plain", "OK");
                } else {
                    request->send(500, "text/plain", "Failed to reconfigure camera");
                }
            } else {
                request->send(200, "text/plain", "Already in this mode");
            }
        } else {
            request->send(400, "text/plain", "Missing mode parameter");
        }
    });

    // Endpoint FOTO
    server.on("/capture-photo", HTTP_GET, [](AsyncWebServerRequest *request){
        if (current_mode != MODE_PHOTO) {
            request->send(400, "text/plain", "Not in photo mode. Please switch mode first.");
            return;
        }
        camera_fb_t * fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Cattura foto fallita");
            request->send(500, "text/plain", "Errore cattura immagine");
            return;
        }
        AsyncWebServerResponse *response = request->beginResponse_P(200, "image/jpeg", (const uint8_t *)fb->buf, fb->len);
        response->addHeader("Content-Disposition", "attachment; filename=capture_uxga.jpg");
        request->onDisconnect([fb](){
            esp_camera_fb_return(fb);
            Serial.println("Buffer foto liberato.");
        });
        request->send(response);
    });

    // Endpoint VIDEO
    server.on("/start-rec", HTTP_GET, [](AsyncWebServerRequest *request){
        if (current_mode != MODE_VIDEO) { request->send(400,"text/plain","Not in video mode"); return; }
        if (recording_state == REC_IDLE) {
            recording_state = REC_START_REQUESTED;
            request->send(200,"text/plain","OK");
        } else {
            request->send(400,"text/plain","Busy");
        }
    });

    server.on("/stop-rec", HTTP_GET, [](AsyncWebServerRequest *request){
        if (recording_state == REC_RECORDING) {
            recording_state = REC_STOP_REQUESTED;
            request->send(200,"text/plain",video_filename);
        } else {
            request->send(400,"text/plain","Not Recording");
        }
    });

    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){ 
        if(request->hasParam("file")){ 
            String f = request->getParam("file")->value(); 
            if(SD_MMC.exists(f)){ request->send(SD_MMC, f, "video/x-msvideo", true); }
            else { request->send(404); }
        } else { request->send(400); }
    });

    // ======================= AGGIUNTA FONDAMENTALE =======================
    // Gestisce tutte le richieste per URL non definiti, evitando che il server si blocchi.
    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not found");
    });
    // =====================================================================

    server.begin();
    Serial.println("Server avviato.");
}

void loop() {
    // La logica di registrazione video deve essere eseguita solo se siamo in modalità video
    if (current_mode == MODE_VIDEO) {
        switch (recording_state) {
            case REC_START_REQUESTED:
                Serial.println("Avvio registrazione...");
                video_filename = "/video_" + String(millis()) + ".avi";
                videoFile = SD_MMC.open(video_filename, FILE_WRITE);
                if (!videoFile) { recording_state = REC_IDLE; }
                else { 
                    byte h[AVIOFFSET]={0}; 
                    videoFile.write(h,AVIOFFSET); 
                    frame_count=0; 
                    last_frame_time=millis(); 
                    recording_state=REC_RECORDING; 
                    Serial.println("Registrazione avviata."); 
                }
                break;
            case REC_RECORDING:
                { 
                    unsigned long now = millis(); 
                    if (now - last_frame_time >= FRAME_INTERVAL_MS) { 
                        last_frame_time = now; 
                        camera_fb_t *fb = esp_camera_fb_get(); 
                        if(!fb) break; 
                        if(!add_avi_frame(videoFile,fb)) recording_state = REC_STOP_REQUESTED; 
                        esp_camera_fb_return(fb); 
                        frame_count++; 
                    }
                }
                break;
            case REC_STOP_REQUESTED:
                Serial.println("Registrazione interrotta, finalizzo il file..."); 
                end_avi(videoFile); 
                recording_state = REC_IDLE;
                break;
            case REC_IDLE: 
                break;
        }
    }
}