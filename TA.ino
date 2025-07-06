#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal_I2C.h>
#include <SPIFFS.h>
#include <time.h>
#include <DFRobotDFPlayerMini.h>
#include <HardwareSerial.h>

// WiFi
const char* ssid = "Alat";
const char* password = "123456789";

// Komponen
#define MAX9814_PIN 32
#define LED_HIJAU 4
#define LED_KUNING 2
#define LED_MERAH 17
LiquidCrystal_I2C lcd(0x27, 16, 2);
HardwareSerial dfSerial(1);  // Gunakan UART1
DFRobotDFPlayerMini dfplayer;
// Batas
#define THRESHOLD_HIJAU_MAX 1349
#define THRESHOLD_KUNING_MIN 1350
#define THRESHOLD_KUNING_MAX 1399
#define THRESHOLD_MERAH 1400

// Variabel
int analogValue = 0;
float desibel = 0;
bool wasNoisy = false;
unsigned long lastWarningTime = 0;
unsigned long lastUpdateTime = 0;
unsigned long updateInterval = 1000;
int lastSavedHour = -1;
bool alatAktif = true; // untuk kontrol ON/OFF
String lastStatus = "";

// Web Server
WebServer server(80);

// Sinkronisasi waktu NTP
void setupTime() {
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println("Waktu tersinkron");
  } else {
    Serial.println("Gagal sinkron waktu");
  }
}

// Simpan data ke SPIFFS per jam
void simpanData(float desibel) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("[GAGAL] Tidak bisa ambil waktu");
    return;
  }

  if (timeinfo.tm_hour < 8 || timeinfo.tm_hour > 17) {
    Serial.println("[INFO] Di luar jam 08:00 - 17:00, data tidak disimpan");
    return;
  }

  if (timeinfo.tm_hour == lastSavedHour) {
    return; // Sudah disimpan jam ini
  }

  char filename[20];
  strftime(filename, sizeof(filename), "/%Y-%m-%d.txt", &timeinfo);
  File file = SPIFFS.open(filename, FILE_APPEND);

  if (!file) {
    Serial.printf("[GAGAL] Gagal membuka file %s untuk menulis\n", filename);
    return;
  }

  char waktu[9];
  strftime(waktu, sizeof(waktu), "%H:%M:%S", &timeinfo);
  file.printf("%s,%.2f\n", waktu, desibel);
  file.close();

  Serial.printf("[SUKSES] Data disimpan: %s = %.2f dB\n", waktu, desibel);
  lastSavedHour = timeinfo.tm_hour;
}

// TASK 1: Sensor
void TaskSensor(void *pvParameters) {

  Serial.print("[DEBUG] Sisa stack TaskSensor: ");
Serial.println(uxTaskGetStackHighWaterMark(NULL));

  while (true) {
      if (alatAktif) {
      long sum = 0;
      for (int i = 0; i < 10; i++) {
        sum += analogRead(MAX9814_PIN);
        vTaskDelay(pdMS_TO_TICKS(5));
      }
      analogValue = sum / 10;

      float slope = (70.0 - 50.0) / (1400 - 1349);
      float intercept = 50.0 - (slope * 1349);
      desibel = slope * analogValue + intercept;

      if (millis() - lastUpdateTime >= updateInterval) {
        Serial.print("Analog: ");
        Serial.print(analogValue);
        Serial.print(" | Desibel: ");
        Serial.println(desibel);
        lastUpdateTime = millis();
      }

      simpanData(desibel);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// TASK 2: Output
void TaskOutput(void *pvParameters) {
  while (true) {
    if (alatAktif) {
      String currentStatus = "";

      if (desibel <= 50) {
        digitalWrite(LED_HIJAU, HIGH);
        digitalWrite(LED_KUNING, LOW);
        digitalWrite(LED_MERAH, LOW);
        currentStatus = "Tenang";
        wasNoisy = false;
      } else if (desibel >= 51 && desibel <= 69) {
        digitalWrite(LED_HIJAU, LOW);
        digitalWrite(LED_KUNING, HIGH);
        digitalWrite(LED_MERAH, LOW);
        currentStatus = "Berisik";
        if (!wasNoisy) {
      
          dfplayer.play(1);
          wasNoisy = true;
          lastWarningTime = millis();
        }
       if (millis() - lastWarningTime > 5000) {
          wasNoisy = false;
  }
      } else if (desibel >= 70) {
        digitalWrite(LED_HIJAU, LOW);
        digitalWrite(LED_KUNING, LOW);
        digitalWrite(LED_MERAH, HIGH);
        currentStatus = "Sangat Berisik";

        if (!wasNoisy) {
          dfplayer.play(1);;
          wasNoisy = true;
          lastWarningTime = millis();
        }

        if (millis() - lastWarningTime > 5000) {
          wasNoisy = false;
        }
      }

      // Update LCD hanya jika status berubah
      if (currentStatus != lastStatus) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(currentStatus);
        lastStatus = currentStatus;
      }
      lcd.setCursor(0, 1);
      lcd.print(WiFi.localIP().toString().c_str());
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }

}
// TASK 3: Web
void TaskWeb(void *pvParameters) {
  server.on("/", HTTP_GET, []() {
    File file = SPIFFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
  });
  server.on("/style.css", HTTP_GET, []() {
    File file = SPIFFS.open("/style.css", "r");
    server.streamFile(file, "text/css");
    file.close();
  });
  server.on("/script.js", HTTP_GET, []() {
    File file = SPIFFS.open("/script.js", "r");
    server.streamFile(file, "application/javascript");
    file.close();
  });
  server.on("/data", HTTP_GET, []() {
    String json = "{\"analog\":" + String(analogValue) + ",\"desibel\":" + String(desibel) + "}";
    server.send(200, "application/json", json);
  });
  server.on("/list", HTTP_GET, []() {
    String json = "[";
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    bool first = true;
    while (file) {
      String name = file.name();
      if (name.endsWith(".txt")) {
        if (!first) json += ",";
        json += "\"" + name + "\"";
        first = false;
      }
      file = root.openNextFile();
    }
    json += "]";
    server.send(200, "application/json", json);
  });
  server.on("/grafik", HTTP_GET, []() {
    if (!server.hasArg("tanggal")) {
      server.send(400, "text/plain", "Parameter tanggal diperlukan");
      return;
    }

    String tanggal = "/" + server.arg("tanggal");
    File file = SPIFFS.open(tanggal, "r");

    if (!file) {
      server.send(404, "text/plain", "File tidak ditemukan");
      return;
    }

    String json = "[";
    bool first = true;
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      int idx = line.indexOf(',');
      if (idx != -1) {
        String waktu = line.substring(0, idx);
        String db = line.substring(idx + 1);
        if (!first) json += ",";
        json += "{\"waktu\":\"" + waktu + "\",\"desibel\":" + db + "}";
        first = false;
      }
    }
    json += "]";
    file.close();

    server.send(200, "application/json", json);
  });

  // Endpoint untuk realtime grafik
  server.on("/realtime", HTTP_GET, []() {
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    char waktu[6];
    strftime(waktu, sizeof(waktu), "%H:%M", &timeinfo);

    String json = "{\"desibel\": " + String(desibel, 2) + ", \"waktu\": \"" + String(waktu) + "\"}";
    server.send(200, "application/json", json);
  });

  // Endpoint kontrol alat
  server.on("/status", HTTP_GET, []() {
    if (server.hasArg("set")) {
      String set = server.arg("set");
      if (set == "on") alatAktif = true;
      else if (set == "off") alatAktif = false;
    }
    String json = "{\"status\": \"" + String(alatAktif ? "on" : "off") + "\"}";
    server.send(200, "application/json", json);
  });

  server.begin();
  while (true) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Setup
void setup() {
  Serial.begin(115200);
  pinMode(LED_HIJAU, OUTPUT);
  pinMode(LED_KUNING, OUTPUT);
  pinMode(LED_MERAH, OUTPUT);
  lcd.init();
  lcd.backlight();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());
  
  SPIFFS.begin();
  setupTime();

  xTaskCreate(TaskSensor, "TaskSensor", 4096, NULL, 1, NULL);
  xTaskCreate(TaskOutput, "TaskOutput", 2048, NULL, 1, NULL);
  xTaskCreate(TaskWeb, "TaskWeb", 8192, NULL, 1, NULL);

  dfSerial.begin(9600, SERIAL_8N1, 26, 27); // RX=26, TX=27 (ubah sesuai wiring Anda)
  if (!dfplayer.begin(dfSerial)) {
    Serial.println("Gagal menginisialisasi DFPlayer Mini");
  } else {
    dfplayer.volume(18); // Atur volume (0-30)
    Serial.println("DFPlayer Mini siap");
  }
}

// Loop
void loop() {
  // Tidak digunakan
}
