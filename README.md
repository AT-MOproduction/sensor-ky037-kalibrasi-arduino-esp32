ESP32 + KY-037 — Web dB Monitor (Kalibrasi Decibel X app ios)

Monitor level suara berbasis ESP32 DevKit dan sensor KY-037 dengan web UI sederhana.
Skala dB dikalibrasi satu tombol menggunakan angka dari aplikasi Decibel X di HP.

<p align="center"> <img src="esp32/assets/wiring_esp32.png" width="720" alt="Wiring ESP32 - KY-037"> </p>

Status: Stable • Board: ESP32 DevKit 38-pin • UI: HTTP (tanpa library tambahan)

Fitur Utama

Web UI realtime menampilkan Level (dB), Vrms, dan status DO (opsional).

Kalibrasi 1 tombol: masukkan angka dari Decibel X → simpan ke NVS (persisten).

Endpoint sederhana: / (UI), /data (JSON), /cal?db=50, /status.

Kode bersih, tanpa WebSocket/Async (cukup WebServer.h), mudah dipahami & dimodifikasi.
------------------------------------------------------
Potongan Kode Penting

Sampling & RMS:

float measureVrms(uint16_t nsamples = 300) {
  analogSetWidth(12);
  analogSetPinAttenuation(PIN_AO, ADC_11db);
  long sum = 0;
  for (uint16_t i = 0; i < nsamples; i++) sum += analogRead(PIN_AO);
  float mid = sum / (float)nsamples;
  double sumSq = 0;
  for (uint16_t i = 0; i < nsamples; i++) {
    float x = analogRead(PIN_AO) - mid;
    sumSq += x * x;
  }
  float Vrms = sqrt(sumSq / nsamples) * (ADC_REF_V / ADC_MAX);
  return max(0.0f, Vrms);
}


Estimasi dB:

float estimateDb(float Vrms) {
  if (Vrms <= 1e-6f || Vrms_ref <= 1e-6f) return 0.0f;
  return dB_ref + 20.0f * log10f(Vrms / Vrms_ref);
}
-------------------------------------------------------
Wiring (ESP32 DevKit 38-pin)
KY-037	ESP32	Catatan
VCC	3V3	Gunakan 3.3V (aman untuk ADC ESP32)
GND	GND	Ground harus common
AO	GPIO34	GPIO34 adalah input-only → cocok untuk analog
DO (opsional)	GPIO27	Dipengaruhi potensiometer di modul (trigger ON/OFF)

Jika modul disuplai 5V, pastikan AO tidak melebihi 3.3V ke ESP32 (gunakan divider/penyekat bila perlu).

Cara Kerja Singkat

ESP32 sampling sinyal analog AO → hitung midline (DC offset) → ambil RMS komponen AC → Vrms (Volt).

Endpoint HTTP
Endpoint	Metode	Output	Keterangan
/	GET	HTML	Web UI (Level dB, Vrms, DO, tombol Kalibrasi)
/data	GET	JSON	{"db":…, "vrms":…, "do":…} untuk update UI
/cal?db=50	GET	Text	Simpan dB_ref=50, ukur & simpan Vrms_ref saat ini
/status	GET	JSON	{"dB_ref":…, "Vrms_ref":…} cek hasil kalibrasi
