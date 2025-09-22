// KY-037 → ESP32 DevKit 38-pin (UI hanya tombol Kalibrasi)
// VCC: 3.3V, GND: GND, AO: GPIO34 (input-only), DO: GPIO27 (opsional)

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>

const char* SSID     = "ganti ssid";
const char* PASSWORD = "ganti password ssid";

const int PIN_AO = 34;    // input-only ADC
const int PIN_DO = 27;    // optional
const int ADC_MAX = 4095;
const float ADC_REF_V = 3.3;  // estimasi pada atten 11dB

WebServer server(80);
Preferences pref;          // NVS

// Kalibrasi dB relatif (persisten)
float dB_ref   = 60.0f;    // diisi dari Decibel X saat kalibrasi
float Vrms_ref = 0.010f;   // diukur saat kalibrasi

// Smoothing
float dB_smooth = 0.0f;
const float ALPHA = 0.15f;

unsigned long lastPrint = 0;

float measureVrms(uint16_t nsamples = 300) {
  analogSetWidth(12);
  analogSetPinAttenuation(PIN_AO, ADC_11db);  // ~0..3.3V

  long sum = 0;
  for (uint16_t i = 0; i < nsamples; i++) sum += analogRead(PIN_AO);
  float mid = sum / (float)nsamples;

  double sumSq = 0;
  for (uint16_t i = 0; i < nsamples; i++) {
    float x = analogRead(PIN_AO) - mid;
    sumSq += x * x;
  }
  float rms_counts = sqrt(sumSq / nsamples);
  float volts_per_count = ADC_REF_V / ADC_MAX;
  float Vrms = rms_counts * volts_per_count;

  if (!isfinite(Vrms) || Vrms < 0) Vrms = 0;
  return Vrms;
}

float estimateDb(float Vrms) {
  if (Vrms <= 1e-6f || Vrms_ref <= 1e-6f) return 0.0f;
  float val = dB_ref + 20.0f * log10f(Vrms / Vrms_ref);
  if (!isfinite(val)) val = 0.0f;
  return val;
}

String htmlPage() {
  return R"HTML(
<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>
<meta http-equiv="Cache-Control" content="no-store" />
<title>KY-037 dB Monitor</title>
<style>
body{font-family:Inter,system-ui,-apple-system,Segoe UI,Roboto,Arial;margin:20px;}
.card{max-width:520px;margin:auto;padding:16px;border:1px solid #ddd;border-radius:12px}
.row{display:flex;justify-content:space-between;align-items:center;margin:8px 0}
h1{font-size:22px;margin:0 0 8px}
.big{font-size:42px;font-weight:700}
.badge{padding:6px 10px;border-radius:999px;background:#eee;display:inline-block}
.btn{padding:8px 12px;border-radius:10px;border:1px solid #ccc;background:#fafafa;cursor:pointer}
small{color:#666}
</style></head>
<body>
<div class='card'>
  <h1>KY-037 dB Monitor</h1>
  <div class='row'><div>Level</div><div id='db' class='big'>--.- dB</div></div>
  <div class='row'><div>Vrms</div><div id='vrms' class='badge'>--.-- V</div></div>
  <div class='row'><div>DO</div><div id='do' class='badge'>-</div></div>
  <div class='row'>
    <button class='btn' onclick='calibrate()'>Kalibrasi (pakai angka dB di HP)</button>
  </div>
  <small>Potensiometer modul hanya memengaruhi DO (ON/OFF), bukan skala analog.</small>
</div>
<script>
const dbEl = document.getElementById('db');
const vrEl = document.getElementById('vrms');
const doEl = document.getElementById('do');

async function pull(){
  try{
    const r = await fetch('/data', {cache:'no-store'});
    if(!r.ok) throw new Error('HTTP '+r.status);
    const j = await r.json();
    if(Number.isFinite(j.db))   dbEl.textContent = j.db.toFixed(1)+' dB';
    if(Number.isFinite(j.vrms)) vrEl.textContent = j.vrms.toFixed(3)+' V';
    doEl.textContent = j.do ? 'TRIGGER':'idle';
  }catch(e){
    console.error('Gagal fetch /data:', e);
  }
}

async function calibrate(){
  const dBref = prompt('Masukkan dB referensi dari Decibel X (mis. 50):','50');
  if(!dBref) return;
  try{
    const r = await fetch('/cal?db='+encodeURIComponent(dBref), {cache:'no-store'});
    alert(await r.text());
  }catch(e){ console.error(e); }
  pull();
}

pull();
setInterval(pull, 700);
</script>
</body></html>
)HTML";
}

// Persistensi
void loadCalib() {
  pref.begin("ky037", true);
  if (pref.isKey("dBref") && pref.isKey("vrRef")) {
    dB_ref   = pref.getFloat("dBref", dB_ref);
    Vrms_ref = pref.getFloat("vrRef", Vrms_ref);
  }
  pref.end();
}

void saveCalib() {
  pref.begin("ky037", false);
  pref.putFloat("dBref", dB_ref);
  pref.putFloat("vrRef", Vrms_ref);
  pref.end();
}

// Serial parser: "cal 50" / angka saja
String line;
void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c=='\n' || c=='\r') {
      line.trim();
      if (line.length()) {
        float val = NAN;
        if (line.startsWith("cal ")) val = line.substring(4).toFloat();
        else if (line.startsWith("c ")) val = line.substring(2).toFloat();
        else {
          bool ok=true; for (auto ch: line) if(!isDigit(ch)&&ch!='.'&&ch!='-'){ok=false;break;}
          if (ok) val = line.toFloat();
        }
        if (isfinite(val) && val>20 && val<120) {
          dB_ref = val;
          Vrms_ref = measureVrms(800);
          saveCalib();
          Serial.print("[CAL OK] dB_ref="); Serial.print(dB_ref,1);
          Serial.print(" | Vrms_ref="); Serial.println(Vrms_ref,5);
        } else {
          Serial.println("Format: cal <dB>  contoh: cal 50");
        }
      }
      line = "";
    } else line += c;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_DO, INPUT_PULLUP);
  analogReadResolution(12);

  loadCalib();

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("WiFi ");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println(); Serial.print("IP: "); Serial.println(WiFi.localIP());

  // Routes
  server.on("/", HTTP_GET, [](){
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html", htmlPage());
  });

  server.on("/data", HTTP_GET, [](){
    float Vrms = measureVrms(300);
    float dBnow = estimateDb(Vrms);
    if (dB_smooth==0.0f) dB_smooth = dBnow;
    dB_smooth = (1.0f-ALPHA)*dB_smooth + ALPHA*dBnow;
    int doState = (digitalRead(PIN_DO)==LOW) ? 1:0;

    server.sendHeader("Cache-Control", "no-store");
    String json = String("{\"db\":")+String(dB_smooth,1)+
                  ",\"vrms\":"+String(Vrms,3)+
                  ",\"do\":"+String(doState)+"}";
    server.send(200, "application/json", json);
  });

  server.on("/cal", HTTP_GET, [](){
    if (server.hasArg("db")) {
      dB_ref = server.arg("db").toFloat();
      Vrms_ref = measureVrms(800);   // ambil referensi pada kondisi saat ini
      saveCalib();
      Serial.print("[CAL OK] dB_ref="); Serial.print(dB_ref,1);
      Serial.print(" | Vrms_ref="); Serial.println(Vrms_ref,5);
      server.sendHeader("Cache-Control", "no-store");
      server.send(200, "text/plain", String("CAL OK: dBref=")+String(dB_ref,1)+
                                      " Vrms_ref="+String(Vrms_ref,5));
    } else {
      server.send(400, "text/plain", "Need ?db=<angka>");
    }
  });

  server.on("/status", HTTP_GET, [](){
    server.sendHeader("Cache-Control", "no-store");
    String json = String("{\"dB_ref\":") + String(dB_ref,1) +
                  ",\"Vrms_ref\":" + String(Vrms_ref,5) + "}";
    server.send(200, "application/json", json);
  });

  server.begin();

  Serial.println("Buka: http://<IP-ESP32>/");
  Serial.println("Kalibrasi: klik tombol Kalibrasi, atau /cal?db=50");
}

void loop() {
  server.handleClient();
  handleSerial();

  // Print periodik ke Serial (monitoring)
  if (millis() - lastPrint > 700) {
    lastPrint = millis();``
    float Vrms = measureVrms(300);
    float dB   = estimateDb(Vrms);
    if (dB_smooth==0.0f) dB_smooth = dB;
    dB_smooth = (1.0f-ALPHA)*dB_smooth + ALPHA*dB;

    int doState = (digitalRead(PIN_DO)==LOW) ? 1:0;
    Serial.print("Vrms="); Serial.print(Vrms,4);
    Serial.print(" V | dB~"); Serial.print(dB_smooth,1);
    Serial.print(" (raw "); Serial.print(dB,1); Serial.print(")");
    Serial.print(" | DO="); Serial.println(doState ? "TRIGGER" : "idle");
  }
}
