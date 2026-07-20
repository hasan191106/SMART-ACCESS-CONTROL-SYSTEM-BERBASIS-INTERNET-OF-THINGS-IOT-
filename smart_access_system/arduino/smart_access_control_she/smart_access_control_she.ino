#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <RTClib.h>        
#include <WiFi.h>          
#include <PubSubClient.h>  

// Konfigurasi Layar LCD 16x2
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Konfigurasi RTC DS1307
RTC_DS1307 rtc;

// Konfigurasi Pin RFID
#define SS_PIN     5
#define RST_PIN    4
MFRC522 rfid(SS_PIN, RST_PIN);

// Konfigurasi Pin Sensor & Aktuator
#define PIN_PIR        13
#define PIN_BUZZER     12
#define PIN_LED_HIJAU  27 
#define PIN_LED_MERAH  26 

// =========================================================================
// SAKLAR SIMULATOR WAKTU
#define PIN_SAKLAR_WAKTU      32  // LOW = Siang (Ikut Jam RTC), HIGH = Paksa Malam (> Jam 18.00)
// =========================================================================

// Konfigurasi Kredensial WiFi Wokwi & Broker HiveMQ
const char* ssid = "Wokwi-GUEST"; 
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "iot/kelompok 7"; 

WiFiClient espClient;
PubSubClient client(espClient);

MFRC522::MIFARE_Key key;
byte targetBlock = 4; 

bool adaOrang = false;
unsigned long waktuMulaiTunggu = 0;
const unsigned long batasWaktuTap = 7000; 

int backupPelanggaranMahasiswa = 0; 

// Fungsi untuk menghubungkan ke jaringan WiFi Wokwi
void setupWiFiDanMQTT() {
  delay(10);
  Serial.println("\nMenghubungkan ke WiFi Wokwi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung!");
  client.setServer(mqtt_server, 1883);
}

// Fungsi untuk menjaga koneksi tetap terhubung ke Broker HiveMQ
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Mencoba koneksi MQTT...");
    String clientId = "ESP32Client-Kelompok7-";
    clientId += String(random(0, 0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("Terhubung ke Broker HiveMQ!");
    } else {
      delay(2000);
    }
  }
}

// Fungsi ganda: Mencetak log ke terminal dan mengirimkan data JSON ke Node-RED via HiveMQ
void kirimDataDanLog(String status, String pesan) {
  // Pastikan koneksi ke broker aman sebelum kirim data
  if (!client.connected()) reconnectMQTT();
  client.loop();
  
  // 1. Tampilkan di Terminal Serial Wokwi
  Serial.println("\n========================================");
  Serial.print("[LOG SISTEM - "); Serial.print(status); Serial.println("]");
  Serial.print("Pesan : "); Serial.println(pesan);
  Serial.print("Uptime: "); Serial.print(millis() / 1000); Serial.println(" detik");
  Serial.println("========================================");

  // 2. Bungkus ke format JSON dan publish ke MQTT Broker untuk ditangkap Node-RED
  JsonDocument doc;
  doc["status"] = status;
  doc["message"] = pesan;
  doc["timestamp_millis"] = millis(); 
  
  String jsonString;
  serializeJson(doc, jsonString);
  client.publish(mqtt_topic, jsonString.c_str());
}

void setup() {
  Serial.begin(115200);
  SPI.begin();           
  rfid.PCD_Init();       

  lcd.init();
  lcd.backlight();       
  
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_SAKLAR_WAKTU, INPUT); 

  // Inisialisasi Modul RTC
  if (!rtc.begin()) {
    Serial.println("RTC Tidak Terdeteksi!");
    while (1);
  }
  
  if (!rtc.isrunning()) {
    Serial.println("RTC belum diatur, menyamakan waktu komputer...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Hubungkan ke WiFi dan Setup konfigurasi Broker MQTT
  setupWiFiDanMQTT(); 

  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  
  Serial.println("Sistem Presensi Online MQTT/RTC Siap Digunakan!");
  resetSistem();
}

void loop() {
  // Selalu pastikan loop MQTT berjalan dengan baik di awal loop utama
  if (!client.connected()) reconnectMQTT();
  client.loop();

  // 1. Deteksi Pergerakan Melalui Sensor PIR
  if (!adaOrang) {
    if (digitalRead(PIN_PIR) == HIGH) {
      adaOrang = true;
      waktuMulaiTunggu = millis(); 
      tampilkanMintaTap();
      
      // Pembacaan jam aktual RTC di log terminal hanya dilakukan jika Saklar Malam dalam posisi OFF (LOW)
      if (digitalRead(PIN_SAKLAR_WAKTU) == LOW) {
        DateTime sekarang = rtc.now();
        Serial.print("\n[PIR] Gerakan terdeteksi pada Jam Aktual -> ");
        if(sekarang.hour() < 10) Serial.print("0");
        Serial.print(sekarang.hour()); Serial.print(":");
        if(sekarang.minute() < 10) Serial.print("0");
        Serial.println(sekarang.minute());
      } else {
        Serial.println("\n[PIR] Gerakan terdeteksi pada [Mode Simulasi Malam Hari]");
      }
    }
  }

  // 2. Cek Batas Waktu Tap (Timeout jika tidak melakukan tapping kartu)
  if (adaOrang && (millis() - waktuMulaiTunggu > batasWaktuTap)) {
    Serial.println("[PIR] Tidak ada aktivitas tap. Sistem kembali standby.");
    resetSistem();
  }

  // 3. Proses Pembacaan Kartu RFID
  if (adaOrang && rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uidString = dapatkanUIDString(rfid.uid.uidByte, rfid.uid.size);
    Serial.print("[RFID] Kartu di-tap! UID: "); Serial.println(uidString);

    cariDanProsesJSON(uidString);

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
}

void cariDanProsesJSON(String uidKartu) {
  String jsonDatabaseDosen = "{\"uid\":\"01020304\",\"nama\":\"Muhammad Amri\",\"role\":\"Dosen\"}";
  String jsonDatabaseMhs   = "{\"uid\":\"C0FFEE99\",\"nama\":\"Novendria Ananda\",\"nim\":\"25/559918/TK/63215\",\"role\":\"Mahasiswa\"}";

  JsonDocument doc;
  DateTime waktuTap = rtc.now();
  String jamMenitStr;

  // Jika mode malam aktif, abaikan waktu RTC asli dan set teks statis simulasi malam
  if (digitalRead(PIN_SAKLAR_WAKTU) == HIGH) {
    jamMenitStr = "19.00"; 
  } else {
    // Jika posisi normal (siang), RTC dibaca sepenuhnya
    jamMenitStr = (waktuTap.hour() < 10 ? "0" : "") + String(waktuTap.hour()) + "." + (waktuTap.minute() < 10 ? "0" : "") + String(waktuTap.minute());
  }

  if (uidKartu == "01 02 03 04") {
    deserializeJson(doc, jsonDatabaseDosen);
    String namaDosen = doc["nama"];
    
    kirimDataDanLog("DOSEN HADIR", namaDosen + " masuk ruangan pukul " + jamMenitStr);
    prosesKartuDosen(namaDosen);
  } 
  else if (uidKartu == "C0 FF EE 99") {
    deserializeJson(doc, jsonDatabaseMhs);
    String namaMhs = doc["nama"];
    String nimMhs  = doc["nim"];
    
    prosesKartuMahasiswa(namaMhs, nimMhs, waktuTap, jamMenitStr);
  } 
  else {
    kirimDataDanLog("KARTU ASING", "UID " + uidKartu + " tidak terdaftar pada jam " + jamMenitStr);
    alarmBahaya("KARTU ASING");
  }
}

void prosesKartuDosen(String nama) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("AKSES DOSEN");
  lcd.setCursor(0, 1); lcd.print(nama.substring(0, 16));
  digitalWrite(PIN_LED_HIJAU, HIGH);
  delay(4000);
  resetSistem();
}

void prosesKartuMahasiswa(String nama, String nim, DateTime waktu, String jamStr) {
  int jumlahPelanggaran = backupPelanggaranMahasiswa; 
  
  bool saklarMalamAktif = (digitalRead(PIN_SAKLAR_WAKTU) == HIGH);
  bool waktuSudahMalam = (waktu.hour() >= 18) || saklarMalamAktif; 
  bool mhsTerlambat    = (waktu.hour() >= 7 && waktu.minute() > 0) || (waktu.hour() > 7); 

  if (jumlahPelanggaran >= 3) {
    kirimDataDanLog("AKSES DITOLAK", "Mahasiswa " + nama + " (" + nim + ") gagal masuk karena status TERBLOKIR.");
    alarmBahaya("TERBLOKIR");
    return;
  }

  if (waktuSudahMalam) {
    jumlahPelanggaran++;
    backupPelanggaranMahasiswa = jumlahPelanggaran; 
    
    // Sembunyikan jam RTC asli agar data paket payload log tidak kontradiktif
    if (saklarMalamAktif) {
      kirimDataDanLog("PELANGGARAN MALAM", "Mahasiswa " + nama + " mencoba masuk di luar jam operasional kuliah. Total pelanggaran: " + String(jumlahPelanggaran));
    } else {
      kirimDataDanLog("PELANGGARAN MALAM", "Mahasiswa " + nama + " melanggar jam malam (" + jamStr + "). Total pelanggaran: " + String(jumlahPelanggaran));
    }
    
    if (jumlahPelanggaran >= 3) {
      alarmBahaya("TERBLOKIR");
    } else {
      alarmBahaya("MALAM HARI");
    }
  } 
  else {
    lcd.clear();
    digitalWrite(PIN_LED_HIJAU, HIGH); 

    if (mhsTerlambat) {
      kirimDataDanLog("PRESENSI MAHASISWA", "Nama: " + nama + " - STATUS: TERLAMBAT PUKUL " + jamStr);
      lcd.setCursor(0, 0); lcd.print("Silahkan masuk, ");
      lcd.setCursor(0, 1); lcd.print("Hadir = " + jamStr); 
    } else {
      kirimDataDanLog("PRESENSI MAHASISWA", "Nama: " + nama + " - STATUS: TEPAT WAKTU PUKUL " + jamStr);
      lcd.setCursor(0, 0); lcd.print("Selamat Datang!");
      lcd.setCursor(0, 1); lcd.print("Hadir = " + jamStr); 
    }
    
    delay(4000);
    resetSistem();
  }
}

String dapatkanUIDString(byte *buffer, byte bufferSize) {
  String uidStr = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) uidStr += "0";
    uidStr += String(buffer[i], HEX);
    if (i < bufferSize - 1) uidStr += " ";
  }
  uidStr.toUpperCase();
  return uidStr;
}

void alarmBahaya(String status) {
  lcd.clear();
  digitalWrite(PIN_LED_MERAH, HIGH);
  digitalWrite(PIN_LED_HIJAU, LOW);

  if (status == "TERBLOKIR") {
    lcd.setCursor(0, 0); lcd.print(" AKSES DITOLAK! ");
    lcd.setCursor(0, 1); lcd.print("KARTU TERBLOKIR ");
  } else if (status == "MALAM HARI") {
    lcd.setCursor(0, 0); lcd.print(" ACCESS DENIED! ");
    lcd.setCursor(0, 1); lcd.print("MHS DILARANG MASUK");
  } else {
    lcd.setCursor(0, 0); lcd.print(" KARTU ASING!   ");
    lcd.setCursor(0, 1); lcd.print(" AKSES DITOLAK  ");
  }

  for (int i = 0; i < 10; i++) {
    digitalWrite(PIN_BUZZER, HIGH); delay(150);
    digitalWrite(PIN_BUZZER, LOW);  delay(150);
  }
  resetSistem();
}

void resetSistem() {
  digitalWrite(PIN_LED_MERAH, LOW);
  digitalWrite(PIN_LED_HIJAU, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  adaOrang = false;
  tampilkanStandby();
}

void tampilkanStandby() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(" STANDBY SYSTEM ");
  lcd.setCursor(0, 1); lcd.print(" Ruang Kelas ID ");
}

void tampilkanMintaTap() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(" SILAKAN TAP... ");
}