// KY-037 → Arduino UNO
// VCC: 5V, GND: GND, AO: A0, DO: D2 (opsional)
// Kalibrasi via Serial: ketik "cal 50" atau "c 50" (sesuaikan angka dari Decibel X)

#include <Arduino.h>
#include <EEPROM.h>
#include <math.h>

const int PIN_AO = A0;
const int PIN_DO = 2;            // optional
const long BAUD = 115200;

const float ADC_REF_DEFAULT = 5.0;  // Vref kira-kira sama dengan Vcc UNO
const int   ADC_MAX = 1023;

// ----- Kalibrasi (akan diisi dari EEPROM saat startup jika ada) -----
float dB_ref   = 60.0;    // acuan dB dari app (mis. Decibel X)
float Vrms_ref = 0.02;    // Vrms yang diukur saat dB_ref
float ADC_REF_V = ADC_REF_DEFAULT;  // bisa dibuat adaptif (lihat catatan)

// ----- Smoothing (opsional, biar angka tidak loncat) -----
float dB_smoothed = 0.0;
const float ALPHA = 0.15;  // 0..1 (semakin besar semakin responsif)

// ----- EEPROM -----
struct CalibBlob {
  uint8_t magic;   // 0xA5 valid
  float dB_ref;
  float Vrms_ref;
};
const int EEPROM_ADDR = 0; // cukup 1 blob kecil

// ----- Utilitas -----
float measureVrms(uint16_t nsamples = 600) {
  // Estimasi midline (DC offset) cepat
  long sum = 0;
  for (uint16_t i = 0; i < nsamples; i++) sum += analogRead(PIN_AO);
  float mid = sum / (float)nsamples;

  // Hitung RMS komponen AC
  double sumSq = 0;
  for (uint16_t i = 0; i < nsamples; i++) {
    float x = analogRead(PIN_AO) - mid;
    sumSq += x * x;
  }
  float rms_counts = sqrt(sumSq / nsamples);
  float volts_per_count = ADC_REF_V / ADC_MAX;
  float Vrms = rms_counts * volts_per_count;
  return Vrms;
}

float estimateDb(float Vrms) {
  if (Vrms <= 1e-6 || Vrms_ref <= 1e-6) return 0;
  return dB_ref + 20.0 * log10(Vrms / Vrms_ref);
}

void saveCalib() {
  CalibBlob b;
  b.magic = 0xA5;
  b.dB_ref = dB_ref;
  b.Vrms_ref = Vrms_ref;
  EEPROM.put(EEPROM_ADDR, b);
}

bool loadCalib() {
  CalibBlob b;
  EEPROM.get(EEPROM_ADDR, b);
  if (b.magic == 0xA5 && isfinite(b.dB_ref) && isfinite(b.Vrms_ref) && b.Vrms_ref > 0) {
    dB_ref = b.dB_ref;
    Vrms_ref = b.Vrms_ref;
    return true;
  }
  return false;
}

// Parser per-baris sederhana
String line;

void doCalibration(float dbFromApp) {
  dB_ref = dbFromApp;
  delay(100);
  // ambil Vrms referensi dengan lebih banyak sampel agar stabil
  Vrms_ref = measureVrms(1000);
  saveCalib();
  Serial.print(F("[CAL OK] dB_ref=")); Serial.print(dB_ref, 1);
  Serial.print(F(" | Vrms_ref=")); Serial.println(Vrms_ref, 5);
}

unsigned long lastPrint = 0;

void setup() {
  Serial.begin(BAUD);
  pinMode(PIN_DO, INPUT_PULLUP); // bila dipakai
  delay(300);

  bool ok = loadCalib();
  Serial.println(F("KY-037 → Arduino UNO"));
  Serial.println(ok ? F("EEPROM: kalibrasi ditemukan & dipakai.")
                    : F("EEPROM: belum ada kalibrasi, gunakan 'cal <angka_dB>'"));
  Serial.println(F("Contoh: ketik 'cal 50' saat Decibel X menunjukkan 50 dB."));

  // (Opsional) kompensasi Vcc: kamu bisa ukur Vcc dan set ADC_REF_V dinamis.
  ADC_REF_V = ADC_REF_DEFAULT;
}

void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        line.trim();
        // Terima format: "cal 50", "c 50", atau angka saja "50"
        float val = NAN;
        if (line.startsWith("cal ")) val = line.substring(4).toFloat();
        else if (line.startsWith("c ")) val = line.substring(2).toFloat();
        else {
          // jika user hanya kirim angka
          bool onlyDigitsOrDot = true;
          for (unsigned i = 0; i < line.length(); i++) {
            char ch = line[i];
            if (!isDigit(ch) && ch != '.' && ch != '-') { onlyDigitsOrDot = false; break; }
          }
          if (onlyDigitsOrDot) val = line.toFloat();
        }

        if (isfinite(val) && val > 20 && val < 120) {
          doCalibration(val);
        } else {
          Serial.println(F("Format salah. Gunakan: cal <dB>  (contoh: cal 50)"));
        }
        line = "";
      }
    } else {
      line += c;
    }
  }
}

void loop() {
  handleSerial();

  float Vrms = measureVrms(400);
  float dB_now = estimateDb(Vrms);

  // smoothing
  if (dB_smoothed == 0.0) dB_smoothed = dB_now;
  dB_smoothed = (1.0 - ALPHA) * dB_smoothed + ALPHA * dB_now;

  int doState = digitalRead(PIN_DO); // optional info

  if (millis() - lastPrint > 300) {
    lastPrint = millis();
    Serial.print(F("Vrms=")); Serial.print(Vrms, 5);
    Serial.print(F(" V | dB~")); Serial.print(dB_smoothed, 1);
    Serial.print(F(" (raw ")); Serial.print(dB_now, 1); Serial.print(F(")"));
    Serial.print(F(" | ref ")); Serial.print(dB_ref, 1);
    Serial.print(F("dB/")); Serial.print(Vrms_ref, 5); Serial.print(F("V"));
    Serial.print(F(" | DO=")); Serial.println(doState == LOW ? "TRIGGER" : "idle");
  }
}
