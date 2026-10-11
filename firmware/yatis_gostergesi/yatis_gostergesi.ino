#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

#define MPU_ADDR 0x68
#define BTN_PIN  4
#define DHT_PIN  18
#define SIGN     -1     // sağ/sol ters çıkarsa 1 yap

// gösterge geometrisi (128x64 ekran)
const int ARC_CX = 64;      // yayın merkezi (ekranın alt orta noktası)
const int ARC_CY = 63;
const int R_OUT = 50;       // yay dış kenar
const int R_IN  = 42;       // yay iç kenar
const int R_BAR_IN  = 44;   // dolan çubuk iç
const int R_BAR_OUT = 48;   // dolan çubuk dış
const int R_DOT = 46;       // max noktaları
const float D2R = 0.0174533f;

Adafruit_SSD1306 display(128, 64, &Wire, -1, 400000UL, 400000UL);
Preferences prefs;
DHT dht(DHT_PIN, DHT11);

float angle = 0;          // filtrelenmiş yatış açısı (derece, ham)
float offsetDeg = 0;      // montaj sıfırı (kalıcı)
float relF = 0;           // yumuşatılmış açı (max için)
float dispA = 0;          // ekranda kayan çubuğun açısı
float maxL = 0, maxR = 0;
float gyroBias = 0;
unsigned long lastUs, lastDraw = 0;

bool maxDirty = false;
unsigned long lastMaxChange = 0;
bool maxArmed = false;    // açı bir kez ~dik olunca max kaydı başlar

// butona basmadan önceki durumu saklamak için
float hist[8];            // son ~400 ms'nin açı geçmişi (50 ms aralıkla)
int hi = 0;
unsigned long lastHist = 0, lastUnstable = 0;
bool pressStill = false;
float pressAngle = 0;

float tempC = NAN;        // son sıcaklık (°C)
unsigned long lastDht = 0;

// uyarı mesajı (ortadaki rakamın yerine geçer)
const char* msgL1 = nullptr;
const char* msgL2 = nullptr;
unsigned long msgUntil = 0;

void setMsg(const char* a, const char* b, unsigned long ms) {
  msgL1 = a; msgL2 = b; msgUntil = millis() + ms;
}

float wrap180(float a) {
  while (a > 180)  a -= 360;
  while (a < -180) a += 360;
  return a;
}

void saveMax() {
  prefs.putFloat("maxL", maxL);
  prefs.putFloat("maxR", maxR);
  maxDirty = false;
}

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

bool readMPU(float &ay, float &az, float &ax, float &gx) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, 14) != 14) return false;
  int16_t rax = Wire.read() << 8 | Wire.read();
  int16_t ray = Wire.read() << 8 | Wire.read();
  int16_t raz = Wire.read() << 8 | Wire.read();
  Wire.read(); Wire.read();                       // sıcaklık
  int16_t rgx = Wire.read() << 8 | Wire.read();
  Wire.read(); Wire.read(); Wire.read(); Wire.read();
  ax = rax / 8192.0; ay = ray / 8192.0; az = raz / 8192.0;  // g (±4g)
  gx = rgx / 32.8;                                          // °/s (±1000)
  return true;
}

// ---------- ekran çizimi ----------

int polX(float r, float deg) { return ARC_CX + (int)lroundf(r * sinf(deg * D2R)); }
int polY(float r, float deg) { return ARC_CY - (int)lroundf(r * cosf(deg * D2R)); }

// a0..a1 derece arası (dikeyden, sağ +) r0..r1 kalınlığında dolu yay
void fillArc(float a0, float a1, float r0, float r1) {
  float span = a1 - a0;
  int n = (int)ceilf(fabsf(span) / 2.0f);
  if (n < 1) n = 1;
  float da = span / n;
  for (int i = 0; i < n; i++) {
    float t0 = a0 + i * da, t1 = t0 + da;
    int x00 = polX(r0, t0), y00 = polY(r0, t0);
    int x10 = polX(r1, t0), y10 = polY(r1, t0);
    int x11 = polX(r1, t1), y11 = polY(r1, t1);
    int x01 = polX(r0, t1), y01 = polY(r0, t1);
    display.fillTriangle(x00, y00, x10, y10, x11, y11, SSD1306_WHITE);
    display.fillTriangle(x00, y00, x11, y11, x01, y01, SSD1306_WHITE);
  }
}

// yay içinde ortalanmış uyarı yazısı (1-2 satır, sığan en büyük boyut)
void drawMsg(const char* l1, const char* l2) {
  int n = l2 ? 2 : 1;
  int len1 = strlen(l1), len2 = l2 ? strlen(l2) : 0;
  int sz = 1;
  for (int s = 3; s >= 1; s--) {
    int h = n * 8 * s + (n - 1) * 2 * s;
    int top = 48 - h / 2;
    bool ok = (top >= 16);
    for (int i = 0; i < n && ok; i++) {
      int len = (i == 0) ? len1 : len2;
      float hw = len * 6.0f * s / 2.0f;
      float dy = ARC_CY - (top + i * 10 * s);
      if (hw * hw + dy * dy > 40.0f * 40.0f) ok = false;
    }
    if (ok) { sz = s; break; }
  }
  int h = n * 8 * sz + (n - 1) * 2 * sz;
  int top = 48 - h / 2;
  display.setTextSize(sz);
  for (int i = 0; i < n; i++) {
    const char* t = (i == 0) ? l1 : l2;
    int w = strlen(t) * 6 * sz - sz;
    int x = ARC_CX - w / 2;
    int y = top + i * 10 * sz;
    display.setCursor(x, y);     display.print(t);
    display.setCursor(x + 1, y); display.print(t);   // kalın görünüm
  }
}

// relNow: gerçek açı (rakam), a: kayan çubuk açısı, l1/l2: uyarı (yoksa nullptr)
void drawScreen(float relNow, float a, float mL, float mR, float temp,
                const char* l1, const char* l2) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextWrap(false);

  // üst köşeler: max değerler
  char buf[16];
  snprintf(buf, sizeof(buf), "SOL %d", (int)lroundf(mL));
  display.setCursor(0, 1);
  display.print(buf);
  snprintf(buf, sizeof(buf), "SAG %d", (int)lroundf(mR));
  display.setCursor(128 - (int)strlen(buf) * 6, 1);
  display.print(buf);

  // üst orta: termometre simgesi + sıcaklık
  char bufT[8];
  if (isnan(temp)) snprintf(bufT, sizeof(bufT), "--C");
  else             snprintf(bufT, sizeof(bufT), "%dC", (int)lroundf(temp));
  int tw = (int)strlen(bufT) * 6 - 1;
  int ix = ARC_CX - (5 + 3 + tw) / 2;
  display.fillRoundRect(ix + 1, 0, 3, 6, 1, SSD1306_WHITE);
  display.fillCircle(ix + 2, 6, 2, SSD1306_WHITE);
  display.setCursor(ix + 8, 1);
  display.print(bufT);

  // yarım daire çerçeve (uçları ekranın en altında)
  for (float d = -90; d <= 90; d += 0.5f) {
    display.drawPixel(polX(R_OUT, d), polY(R_OUT, d), SSD1306_WHITE);
    display.drawPixel(polX(R_IN, d),  polY(R_IN, d),  SSD1306_WHITE);
  }
  display.drawFastHLine(ARC_CX - R_OUT, ARC_CY, R_OUT - R_IN + 1, SSD1306_WHITE);
  display.drawFastHLine(ARC_CX + R_IN,  ARC_CY, R_OUT - R_IN + 1, SSD1306_WHITE);

  // dolan çubuk: ortadan açıya kadar (dikken küçük bir işaret kalır)
  float ac = a;
  if (ac > 90) ac = 90;
  if (ac < -90) ac = -90;
  if (fabsf(ac) < 2.0f) fillArc(-1.0f, 1.0f, R_BAR_IN, R_BAR_OUT);
  else                  fillArc(0, ac, R_BAR_IN, R_BAR_OUT);

  // max noktaları (çubuk üstünden geçince halka gibi görünür)
  if (mR >= 0.5f) {
    int x = polX(R_DOT, mR), y = polY(R_DOT, mR);
    display.fillCircle(x, y, 2, SSD1306_WHITE);
    if (a > 0 && a >= mR - 1) display.fillCircle(x, y, 1, SSD1306_BLACK);
  }
  if (mL >= 0.5f) {
    int x = polX(R_DOT, -mL), y = polY(R_DOT, -mL);
    display.fillCircle(x, y, 2, SSD1306_WHITE);
    if (a < 0 && -a >= mL - 1) display.fillCircle(x, y, 1, SSD1306_BLACK);
  }

  // orta: büyük açı rakamı ya da uyarı yazısı
  if (l1) {
    drawMsg(l1, l2);
  } else {
    int v = (int)lroundf(fabsf(relNow));
    if (v > 99) v = 99;
    int digits = (v >= 10) ? 2 : 1;
    int w = digits * 24 - 3;
    int x = ARC_CX - w / 2;
    display.setTextSize(4);
    display.setCursor(x, 35);     display.print(v);
    display.setCursor(x + 1, 35); display.print(v);   // kalın görünüm
  }

  display.display();
}

// açılış animasyonu: çubuk sağa, sola, ortaya süpürür
void introSweep() {
  for (int i = 0; i <= 50; i++) {
    float a = 60.0f * sinf(i / 50.0f * 6.2832f);
    drawScreen(a, a, maxL, maxR, tempC, nullptr, nullptr);
  }
}

void setup() {
  pinMode(BTN_PIN, INPUT_PULLUP);
  Wire.begin(21, 22);
  Wire.setClock(400000);
  dht.begin();

  // kayıtlı değerleri oku
  prefs.begin("lean", false);
  maxL = prefs.getFloat("maxL", 0);
  maxR = prefs.getFloat("maxR", 0);
  if (!(maxL >= 0 && maxL <= 80)) maxL = 0;
  if (!(maxR >= 0 && maxR <= 80)) maxR = 0;
  bool hasOffset = prefs.isKey("offs");
  offsetDeg = prefs.getFloat("offs", 0);
  if (!(offsetDeg >= -180 && offsetDeg <= 180)) offsetDeg = 0;

  mpuWrite(0x6B, 0x00);  // uyandır
  mpuWrite(0x1B, 0x10);  // gyro ±1000 °/s
  mpuWrite(0x1C, 0x08);  // ivme ±4 g
  mpuWrite(0x1A, 0x03);  // dijital filtre ~44 Hz

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("Aci olculuyor...");
  display.println("Cihazi sabit tut");
  display.display();
  delay(1000);

  // sabitken: gyro sapması + başlangıç açısı (ivmeölçerden)
  float sumG = 0, sumAy = 0, sumAz = 0, ax, ay, az, gx;
  int n = 0;
  for (int i = 0; i < 500; i++) {
    if (readMPU(ay, az, ax, gx)) { sumG += gx; sumAy += ay; sumAz += az; n++; }
    delay(2);
  }
  if (n > 0) {
    gyroBias = sumG / n;
    angle = atan2(sumAy / n, sumAz / n) * 57.2958;
  }

  // hiç kalibrasyon kaydedilmediyse: şu anki konumu geçici sıfır say
  if (!hasOffset) offsetDeg = angle;

  // ilk sıcaklık okuması (cihaz sabitken)
  float t0 = dht.readTemperature();
  if (!isnan(t0)) tempC = t0;
  lastDht = millis();

  for (int i = 0; i < 8; i++) hist[i] = angle;
  lastUnstable = millis();

  relF = wrap180(angle - offsetDeg) * SIGN;
  dispA = relF;
  maxArmed = (fabs(relF) < 5);

  introSweep();
  lastUs = micros();
}

void loop() {
  float ax, ay, az, gx;
  if (!readMPU(ay, az, ax, gx)) return;

  unsigned long now = micros();
  float dt = (now - lastUs) / 1000000.0;
  lastUs = now;
  if (dt > 0.1) dt = 0.01;

  // gyro ile ilerle
  angle += (gx - gyroBias) * dt;

  // ivmeölçerle yavaşça düzelt (sadece ~1g iken)
  float mag = sqrt(ax*ax + ay*ay + az*az);
  if (mag > 0.9 && mag < 1.1) {
    float accAngle = atan2(ay, az) * 57.2958;
    angle += 0.01 * wrap180(accAngle - angle);
  }
  angle = wrap180(angle);

  // cihaz sabit mi? ve son ~400 ms'nin açı geçmişi
  bool stable = (mag > 0.85 && mag < 1.15 && fabs(gx - gyroBias) < 5.0);
  if (!stable) lastUnstable = millis();
  if (millis() - lastHist >= 50) {
    lastHist = millis();
    hist[hi] = angle;
    hi = (hi + 1) % 8;
  }

  // montaj sıfırına göre açı
  float rel = wrap180(angle - offsetDeg) * SIGN;

  // max için yumuşatılmış açı (~50 ms)
  float k = dt / (0.05 + dt);
  relF += k * (rel - relF);

  if (fabs(relF) < 5) maxArmed = true;

  if (maxArmed && fabs(relF) <= 80) {
    if (relF > 0 && relF > maxR)  { maxR = relF;  maxDirty = true; lastMaxChange = millis(); }
    if (relF < 0 && -relF > maxL) { maxL = -relF; maxDirty = true; lastMaxChange = millis(); }
  }

  // max 2 sn boyunca artmadıysa kaydet
  if (maxDirty && millis() - lastMaxChange > 2000) saveMax();

  // buton: kısa basış = dik konumu kaydet, 2 sn basılı = max'ları sil
  static unsigned long pressStart = 0;
  static bool wasDown = false, longDone = false;
  bool down = (digitalRead(BTN_PIN) == LOW);
  if (down && !wasDown) {
    pressStart = millis();
    longDone = false;
    pressStill = (millis() - lastUnstable > 400);  // basmadan önce 0,4 sn sabit miydi
    pressAngle = hist[hi];                         // ~400 ms önceki açı
  }
  if (down && !longDone && millis() - pressStart >= 2000) {
    maxL = 0; maxR = 0;
    saveMax();
    longDone = true;
    setMsg("MAX", "SILINDI", 1500);
  }
  if (!down && wasDown && !longDone && millis() - pressStart > 30) {
    if (pressStill) {
      offsetDeg = pressAngle;
      angle = pressAngle;
      relF = 0;
      dispA = 0;
      prefs.putFloat("offs", offsetDeg);
      setMsg("DIK", "KAYDEDILDI", 1500);
    } else {
      setMsg("HAREKET", "VAR", 1500);
    }
  }
  wasDown = down;

  // sıcaklık: 3 sn'de bir, cihaz hızlı dönmüyorken
  if (millis() - lastDht > 3000 && fabs(gx - gyroBias) < 20) {
    lastDht = millis();
    float t = dht.readTemperature();
    if (!isnan(t)) tempC = t;
  }

  // ekran ~20 Hz
  if (millis() - lastDraw > 50) {
    lastDraw = millis();
    dispA += 0.45f * (rel - dispA);        // çubuk yumuşak kayar
    bool warn = (millis() < msgUntil);
    drawScreen(rel, dispA, maxL, maxR, tempC,
               warn ? msgL1 : nullptr, warn ? msgL2 : nullptr);
  }
}
