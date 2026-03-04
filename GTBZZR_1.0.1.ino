#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <HTTPUpdate.h>
#include "esp_wifi.h"
#include <vector>
#include <algorithm>
#include "time.h"

// --- DATOS DE RED ---
const char* ssid = "BZZR-CLIENTE";
const char* password = "e48Ac28R5R";
const char* googleUrl = "https://script.google.com/macros/s/AKfycbx2zGDKX9y9l3eihNwxBS6StSDqFts68G518fLGEOz_Pm1S3ZWnmIptO_rzKkvIO1q05Q/exec";

// --- CONFIGURACIÓN OTA Y HORA ---
const char* otaUrl = "https://tu-servidor.com/firmware.bin"; 
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -21600; 
const int   daylightOffset_sec = 0;
const int   HORA_ACTUALIZACION = 20; 

// --- CONFIGURACIÓN DE ANALÍTICA ---
const int RSSI_LIMITE = -90;    // Muy sensible
const int UMBRAL_ENTRADA = -60; // Punto de corte para "Entró"
const unsigned long TIEMPO_ESCANEO = 60000; 

std::vector<String> macsDetectadas;
int conteoPasantes = 0;
int conteoEntraron = 0;
bool actualizacionRealizadaHoy = false;

// --- SNIFFER CORREGIDO CON IMPRESIÓN SERIAL ---
void sniffer_callback(void* buf, wifi_promiscuous_pkt_type_t type) {
  // Capturamos MGMT (celulares buscando WiFi) y DATA (celulares ya conectados)
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t*)buf;
  int rssi = pkt->rx_ctrl.rssi;
  if (rssi < RSSI_LIMITE) return;

  uint8_t* payload = pkt->payload;
  
  // Extraemos la MAC (está en el byte 10 del paquete 802.11)
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", 
          payload[10], payload[11], payload[12], payload[13], payload[14], payload[15]);
  String sMac = String(macStr);

  // Evitar MACs basura
  if (sMac == "00:00:00:00:00:00" || sMac == "FF:FF:FF:FF:FF:FF") return;

  if (std::find(macsDetectadas.begin(), macsDetectadas.end(), sMac) == macsDetectadas.end()) {
    macsDetectadas.push_back(sMac);
    
    // ESTO ES LO QUE TE FALTABA: Imprimir en el Serial para saber que funciona
    if (rssi >= UMBRAL_ENTRADA) {
      conteoEntraron++;
      Serial.printf(" [ENTRÓ] %s (%d dBm)\n", macStr, rssi);
    } else {
      conteoPasantes++;
      Serial.printf(" [PASÓ]  %s (%d dBm)\n", macStr, rssi);
    }
  }
}

// --- FUNCIÓN OTA ---
void ejecutarOTA() {
  Serial.println("\n[!] BUSCANDO ACTUALIZACIÓN...");
  WiFiClientSecure client;
  client.setInsecure();
  t_httpUpdate_return ret = httpUpdate.update(client, otaUrl);
  // El resto de la lógica de error se mantiene igual...
}

void enviarYRevisarOTA() {
  if (macsDetectadas.empty()) {
    Serial.println("\nCiclo sin detecciones.");
    return;
  }

  Serial.println("\n--- CONECTANDO PARA ENVIAR DATOS ---");
  esp_wifi_set_promiscuous(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 40) {
    delay(500); Serial.print("."); retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[OK] Conectado.");
    
    // Revisar Hora
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)){
      Serial.printf("Hora: %02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min);
      if (timeinfo.tm_hour == HORA_ACTUALIZACION && !actualizacionRealizadaHoy) {
        ejecutarOTA();
        actualizacionRealizadaHoy = true;
      }
      if (timeinfo.tm_hour != HORA_ACTUALIZACION) actualizacionRealizadaHoy = false;
    }

    // Enviar a Google
    WiFiClientSecure googleClient;
    googleClient.setInsecure();
    HTTPClient http;
    http.begin(googleClient, googleUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/json");

    String json = "{\"total\":" + String(macsDetectadas.size()) + 
                  ",\"entraron\":" + String(conteoEntraron) + 
                  ",\"pasaron\":" + String(conteoPasantes) + 
                  ",\"dispositivos\":[";
    for(size_t i = 0; i < macsDetectadas.size(); i++) {
      json += "\"" + macsDetectadas[i] + "\"";
      if (i < macsDetectadas.size() - 1) json += ",";
    }
    json += "]}";

    int httpCode = http.POST(json);
    Serial.printf("Google Sheets: %d\n", httpCode);
    http.end();
  }

  macsDetectadas.clear();
  conteoEntraron = 0;
  conteoPasantes = 0;
  WiFi.disconnect(true);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
}

void loop() {
  Serial.println("\nSISTEMA TRACKER v2.1 - MONITOR ACTIVO");
  WiFi.mode(WIFI_STA);
  delay(100);
  Serial.println("\n>>> ESCANEANDO CANALES...");
  
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&sniffer_callback);
  
  unsigned long inicio = millis();
  int canal = 1;
  while (millis() - inicio < TIEMPO_ESCANEO) {
    esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE);
    canal = (canal % 13) + 1;
    delay(400); 
  }

  esp_wifi_set_promiscuous(false); 
  enviarYRevisarOTA();
}