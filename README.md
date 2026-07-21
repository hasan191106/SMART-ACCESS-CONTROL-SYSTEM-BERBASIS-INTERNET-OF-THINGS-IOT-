[README.md](https://github.com/user-attachments/files/30211942/README.md)
# SIMAK+SHE — Smart Access Control System (IoT) — Teknik UGM

Paket ini berisi versi lanjutan dari kode Wokwi/ESP32 yang kamu kirim, ditambah
website dashboard yang membaca data secara real-time dari broker MQTT yang sama.

```
smart_access_system/
├── arduino/
│   └── smart_access_control_she.ino   <- sketch ESP32 (upload ke Wokwi/board asli)
├── website/
│   └── dashboard.html                 <- dashboard monitoring (buka langsung di browser)
└── README.md
```

## 1. Apa yang berubah dari kode asli

### a. Sinkronisasi RTC ke NTP
RTC DS1307 sekarang disinkronkan ke waktu internet asli (bukan waktu compile
sketch), memakai `configTime()` + `getLocalTime()` dari library `<time.h>`
bawaan ESP32:

- Saat WiFi tersambung di `setup()`, `syncRTCdenganNTP()` dipanggil sekali
  supaya RTC langsung akurat (WIB, UTC+7).
- Di `loop()`, resync otomatis diulang setiap 6 jam (`INTERVAL_RESYNC_NTP`)
  supaya RTC tidak drift dalam pemakaian jangka panjang.
- Kalau NTP gagal diakses (WiFi putus dsb), RTC tetap jalan dengan waktu
  terakhir yang tersimpan — sistem tidak berhenti total.
- Field `ntp_sync` dikirim di setiap payload MQTT supaya dashboard tahu apakah
  waktu yang ditampilkan berasal dari NTP asli atau belum sempat sinkron.

### b. Modul SHE (Safety, Health, Environment) — deteksi helm
Ditambahkan satu pin sensor/saklar baru:

| Pin | Nama | Fungsi |
|---|---|---|
| GPIO 33 | `PIN_HELM` | HIGH = terdeteksi **tidak** pakai helm, LOW = pakai helm |
| GPIO 25 | `PIN_LED_KUNING` | LED indikator "peringatan SHE menunggu konfirmasi" |

Alur kerjanya meniru proses tap masuk/keluar sungguhan:

1. **Tap pertama (dianggap TAP MASUK):** sistem membaca `PIN_HELM`. Kalau
   terdeteksi tidak pakai helm, mahasiswa **tetap diizinkan masuk** (supaya
   tidak mengganggu operasional), tapi statusnya dicatat sebagai
   `SHE WARNING (PENDING)` dan LED kuning menyala terus sebagai reminder.
2. **Tap kedua orang yang sama (dianggap TAP PULANG):** sistem mengecek flag
   pending tadi. Kalau masih pending, pelanggaran dikonfirmasi permanen
   (`PELANGGARAN SHE (KONFIRMASI)`), counter `totalPelanggaranSHE` naik, buzzer
   + LED merah menyala sebentar, LED kuning mati.

Catatan penting: versi ini masih **demo single-user** (status masuk/keluar
disimpan di variabel global `mhsSedangDiDalam` dan `pelanggaranHelmPending`).
Untuk banyak mahasiswa sekaligus, field ini perlu diubah jadi `struct`/array
per-UID (misalnya disimpan di map `uid -> status`), atau — lebih baik lagi —
statusnya disimpan di backend/Node-RED/database, bukan di RAM ESP32.

### c. Deteksi presensi terlambat
Logika lama (`mhsTerlambat`) dirapikan jadi konfigurasi eksplisit di bagian
atas kode:

```cpp
const int JAM_MASUK_KULIAH          = 7;   // kuliah mulai jam 07.00
const int TOLERANSI_TERLAMBAT_MENIT = 15;  // toleransi 15 menit
const int JAM_MULAI_MALAM           = 18;  // >= jam 18.00 = di luar jam operasional
```

Sekarang sistem menghitung **berapa menit keterlambatan** (bukan cuma
ya/tidak), dan mengirim pesan seperti:

```
Nama: Novendria Ananda - STATUS: TERLAMBAT 23 menit (Pukul 07.23)
```

Silakan ganti ketiga angka itu sesuai jadwal kelas kalian.

### d. Payload MQTT lebih terstruktur
Semua event sekarang dikirim lewat satu fungsi `kirimDataJSON(...)` dengan
field tambahan supaya bisa dibaca dashboard:

```json
{
  "status": "PRESENSI MASUK",
  "message": "Nama: Novendria Ananda - STATUS: TERLAMBAT 23 menit (Pukul 07.23)",
  "uid": "C0FFEE99",
  "nama": "Novendria Ananda",
  "role": "Mahasiswa",
  "jenis_tap": "MASUK",
  "jam": "07.23",
  "pelanggaran_she": false,
  "telat": true,
  "terblokir": false,
  "ntp_sync": true,
  "timestamp_millis": 123456
}
```

## 2. Wiring tambahan (Wokwi / hardware asli)

Selain wiring lama (RFID, LCD I2C, PIR, buzzer, LED hijau/merah, saklar waktu),
tambahkan:

- **PIN_HELM (GPIO 33)** → sambungkan ke switch/tombol simulasi di Wokwi
  (posisi HIGH = "tidak pakai helm"). Di hardware asli, ini bisa diganti sensor
  image/IR/berat pada rak helm, atau tombol manual petugas.
- **PIN_LED_KUNING (GPIO 25)** → LED kuning + resistor ke GND, sebagai indikator
  peringatan SHE.

## 3. Website dashboard (`website/dashboard.html`)

File ini **berdiri sendiri** (HTML+CSS+JS, tanpa build tools) dan bisa langsung
dibuka di browser mana saja yang punya akses internet. Cara kerjanya:

1. Terhubung ke broker publik `broker.hivemq.com` lewat WebSocket
   (`wss://broker.hivemq.com:8884/mqtt`) menggunakan library `mqtt.js`.
2. Subscribe ke topik yang sama persis dengan kode ESP32: `iot/kelompok 7`.
3. Setiap payload JSON yang masuk dipakai untuk:
   - memperbarui status "gerbang" (hijau = diterima, kuning = peringatan
     SHE/jam malam, merah = ditolak/diblokir),
   - menambah baris ke log kejadian langsung,
   - menambah baris ke tabel riwayat tap,
   - memperbarui 4 kartu statistik (presensi hari ini, terlambat, pelanggaran
     SHE, kartu diblokir).

Dashboard ini **read-only** — dia hanya mendengarkan, tidak mengirim apa pun ke
perangkat. Jadi aman dibuka di banyak device sekaligus (misalnya layar di pos
satpam + laptop dosen piket).

### Menjalankan dashboard
Cukup dobel klik `dashboard.html`, atau upload ke hosting statis apa pun
(GitHub Pages, Netlify, dsb) supaya bisa diakses dari HP/laptop lain.

### Catatan penting soal broker publik
`broker.hivemq.com` adalah broker **publik dan gratis** — siapa pun yang tahu
nama topik `iot/kelompok 7` juga bisa membaca (atau bahkan memalsukan) data di
topik itu. Ini cukup untuk demo/tugas kuliah, tapi **untuk pemakaian produksi
sungguhan** (data mahasiswa asli, kontrol akses gedung beneran), sebaiknya:
- pindah ke broker privat (self-hosted Mosquitto/EMQX, atau HiveMQ Cloud
  berbayar dengan autentikasi username/password + TLS client cert),
- pakai nama topik yang tidak mudah ditebak, dan
- tambahkan autentikasi di sisi dashboard sebelum menampilkan data pribadi.

## 4. Ide pengembangan lanjutan (opsional)
- Simpan histori presensi & pelanggaran ke database (Firebase/Supabase/Node-RED
  + InfluxDB) supaya tidak hilang saat ESP32 restart.
- Ganti variabel status single-user (`mhsSedangDiDalam`, dst.) jadi map per-UID
  supaya sistem mendukung banyak kartu sekaligus dengan status independen.
- Tambahkan halaman rekap mingguan/bulanan di dashboard (grafik tren
  keterlambatan & pelanggaran SHE per mahasiswa).
- Integrasi notifikasi WhatsApp/Telegram otomatis ke dosen wali saat mahasiswa
  kena pelanggaran SHE ke-3 (status TERBLOKIR).
