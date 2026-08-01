#include <Arduino.h>

// ================= PINS =================
#define RA_STEP 12
#define RA_DIR  14   
#define RA_EN   13
#define DEC_STEP 27
#define DEC_DIR  26
#define DEC_EN   25

// ================= USTAWIENIA MONTAŻU (EQ3-2) =================
const float RA_STEPS_PER_DEG = 7680.0;     // 144 zęby na ślimacznicy RA
const float DEC_STEPS_PER_DEG = 3466.67;   // 65 zębów na ślimacznicy DEC
const float GUIDE_STEPS_PER_MS = 1.2;

// Sidereal tracking (śledzenie gwiazd)
bool trackingEnabled = false;
const unsigned long TRACKING_INTERVAL = 31164; // us (mikrosekundy)
unsigned long lastTrackingStep = 0;

// Pozycja startowa (okolice Polarnej na start)
long currentRAsteps = (long)((2.5 * 15.0) * RA_STEPS_PER_DEG);
long currentDECsteps = (long)(89.0 * DEC_STEPS_PER_DEG);

long targetRAsteps = currentRAsteps;
long targetDECsteps = currentDECsteps;

// Zmienne lokalizacji (przechowywane czyste, bez '#')
String currentLat = "+50*50";
String currentLon = "-019*13";

// Kontrola ruchu
bool movingRA = false;
bool movingDEC = false;

// FLAGI GUIDINGU
bool isGuidingRA = false; 
bool isGuidingDEC = false; 

long stepsToGoRA = 0;
long stepsToGoDEC = 0;

int dirRA = 0;
int dirDEC = 0;

// Prędkości ruchu szybkiego (rampa trapezowa)
const unsigned long MANUAL_SPEED_MAX = 1000;   // docelowe 1000 us (szybko)
const unsigned long MANUAL_SPEED_START = 3500; // startowe 3500 us (miękki start)
const unsigned long GUIDE_SPEED_DELAY = 833;   // ~1.2 kroku na ms

unsigned long currentDelayRA = MANUAL_SPEED_START;
unsigned long currentDelayDEC = MANUAL_SPEED_START;

unsigned long lastPulseRA = 0;
unsigned long lastPulseDEC = 0;

String cmdBuf = "";

// ================= DEKLARACJE FUNKCJI =================
void process(String c);
String formatRA();
String formatDEC();
long parseRA(String s);
long parseDEC(String s);

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(RA_STEP, OUTPUT);
  pinMode(RA_DIR, OUTPUT);
  pinMode(RA_EN, OUTPUT);

  pinMode(DEC_STEP, OUTPUT);
  pinMode(DEC_DIR, OUTPUT);
  pinMode(DEC_EN, OUTPUT);

  // Włączenie driverów (stan LOW aktywuje TMC2209)
  digitalWrite(RA_EN, LOW);
  digitalWrite(DEC_EN, LOW);

  delay(1000);
  Serial.println("SYSTEM_READY#");
}

// ================= MAIN LOOP =================
void loop() {
  unsigned long nowMicros = micros();

  // ================= TRACKING (ŚLEDZENIE) =================
  if (trackingEnabled && !movingRA) {
    if (nowMicros - lastTrackingStep >= TRACKING_INTERVAL) {
      digitalWrite(RA_DIR, HIGH); // Kierunek śledzenia nieba
      delayMicroseconds(5);       
      
      digitalWrite(RA_STEP, HIGH);
      delayMicroseconds(10);
      digitalWrite(RA_STEP, LOW);

      currentRAsteps += 1;
      lastTrackingStep += TRACKING_INTERVAL; 
    }
  }

  // ================= RUCH RA (GOTO / STRZAŁKI / GUIDING) =================
  if (movingRA && stepsToGoRA > 0) {
    if (nowMicros - lastPulseRA >= currentDelayRA) {
      digitalWrite(RA_DIR, dirRA > 0 ? HIGH : LOW);
      delayMicroseconds(5);       

      digitalWrite(RA_STEP, HIGH);
      delayMicroseconds(10);
      digitalWrite(RA_STEP, LOW);

      currentRAsteps += dirRA;
      stepsToGoRA--;

      lastPulseRA = nowMicros;

      if (!isGuidingRA) {
        if (stepsToGoRA > 200) {
          if (currentDelayRA > MANUAL_SPEED_MAX) currentDelayRA -= 2; 
        } else {
          if (currentDelayRA < MANUAL_SPEED_START) currentDelayRA += 2; 
        }
      }

      if (stepsToGoRA <= 0) {
        movingRA = false;
        isGuidingRA = false;
        lastTrackingStep = micros(); 
      }
    }
  }

  // ================= RUCH DEC (GOTO / STRZAŁKI / GUIDING) =================
  if (movingDEC && stepsToGoDEC > 0) {
    if (nowMicros - lastPulseDEC >= currentDelayDEC) {
      digitalWrite(DEC_DIR, dirDEC > 0 ? HIGH : LOW);
      delayMicroseconds(5);       

      digitalWrite(DEC_STEP, HIGH);
      delayMicroseconds(10);
      digitalWrite(DEC_STEP, LOW);

      currentDECsteps += dirDEC;
      stepsToGoDEC--;

      lastPulseDEC = nowMicros;

      if (!isGuidingDEC) {
        if (stepsToGoDEC > 200) {
          if (currentDelayDEC > MANUAL_SPEED_MAX) currentDelayDEC -= 2; 
        } else {
          if (currentDelayDEC < MANUAL_SPEED_START) currentDelayDEC += 2; 
        }
      }

      if (stepsToGoDEC <= 0) {
        movingDEC = false;
        isGuidingDEC = false;
      }
    }
  }

  // ================= OBSŁUGA SERIAL USB =================
  while (Serial.available()) {
    char c = Serial.read();

    if (c == 0x06) { 
      // Poprawna odpowiedź ACK dla LX200 (BEZ znaku '#')
      Serial.print("P");
    }
    else if (c == '#') {
      process(cmdBuf);
      cmdBuf = "";
    }
    else if (c != '\r' && c != '\n') {
      cmdBuf += c;
    }
  }
}

// ================= PARSER KOMEND LX200 / ESP32Go =================
void process(String c) {
  c.trim();
  
  if (c.startsWith(":")) {
    c = c.substring(1);
  }
  
  if (c.length() == 0) return;

  // ===== Status ruchu / Odczyty pozycji =====
  if (c == "D") {
    Serial.print((movingRA || movingDEC) ? "|#" : "#");
    return;
  }

  if (c == "GR") {
    Serial.print(formatRA());
    return;
  }

  if (c == "GD") {
    Serial.print(formatDEC());
    return;
  }

  // ===== Zapytania o pozycję geograficzną =====
  if (c == "Gt") { 
    Serial.print(currentLat + "#"); 
    return; 
  }
  if (c == "Gg") { 
    Serial.print(currentLon + "#"); 
    return; 
  }

  // ===== Zapis pozycji geograficznej z Astroberry =====
  if (c.startsWith("St")) {
    currentLat = c.substring(2);
    Serial.print("1#");
    return;
  }
  if (c.startsWith("Sg") || c.startsWith("SG")) {
    currentLon = c.substring(2);
    Serial.print("1#");
    return;
  }

  // ===== Handshake pod Driver INDI lx200_esp32go / LX200 Generic =====
  if (c.startsWith("GV") || c == "GVP" || c == "GVN") {
    Serial.print("ESP32Go v1.0#"); 
    return;
  }

  // Status teleskopu dla INDI
  if (c == "Gstat" || c == "GSTAT") {
    if (movingRA || movingDEC) {
      Serial.print("2#"); // 2 = Slewing
    } else if (trackingEnabled) {
      Serial.print("1#"); // 1 = Tracking
    } else {
      Serial.print("0#"); // 0 = Idle
    }
    return;
  }

  // Stan zaparkowania
  if (c == "h?" || c == "hP") {
    Serial.print("0#");
    return;
  }

  // Komendy konfiguracyjne LX200 / ESP32Go
  if (c == "RS" || c == "Ginfo" || c == "GW" || c == "GWI" || c == "GX" || c == "GT" || c == "U") {
    Serial.print("OK#"); 
    return;
  }

  // ===== Tracking ON / OFF =====
  if (c == "AP") {
    trackingEnabled = true;
    lastTrackingStep = micros();
    Serial.print("1#");
    return;
  }

  if (c == "AL") {
    trackingEnabled = false;
    Serial.print("1#");
    return;
  }

  // ===== Pulse Guiding (PHD2) =====
  if (c.startsWith("Mg")) {
    int duration = c.substring(3).toInt();
    long steps = (long)(duration * GUIDE_STEPS_PER_MS);

    if (c.startsWith("Mgn")) {
      stepsToGoDEC = steps;
      dirDEC = 1;
      currentDelayDEC = GUIDE_SPEED_DELAY; 
      isGuidingDEC = true;
      movingDEC = true;
    }
    else if (c.startsWith("Mgs")) {
      stepsToGoDEC = steps;
      dirDEC = -1;
      currentDelayDEC = GUIDE_SPEED_DELAY;
      isGuidingDEC = true;
      movingDEC = true;
    }
    else if (c.startsWith("Mge")) {
      stepsToGoRA = steps;
      dirRA = 1;
      currentDelayRA = GUIDE_SPEED_DELAY;
      isGuidingRA = true;
      movingRA = true;
    }
    else if (c.startsWith("Mgw")) {
      stepsToGoRA = steps;
      dirRA = -1;
      currentDelayRA = GUIDE_SPEED_DELAY;
      isGuidingRA = true;
      movingRA = true;
    }
    return;
  }

  // ===== GOTO Coordinates =====
  if (c.startsWith("Sr")) {
    targetRAsteps = parseRA(c.substring(2));
    Serial.print("1#");
    return;
  }

  if (c.startsWith("Sd")) {
    targetDECsteps = parseDEC(c.substring(2));
    Serial.print("1#");
    return;
  }

  // ===== Wykonaj GOTO =====
  if (c == "MS") {
    long diffRA = targetRAsteps - currentRAsteps;
    long diffDEC = targetDECsteps - currentDECsteps;

    stepsToGoRA = abs(diffRA);
    dirRA = (diffRA > 0) ? 1 : -1;
    currentDelayRA = MANUAL_SPEED_START; 
    isGuidingRA = false;
    movingRA = (stepsToGoRA > 0);

    stepsToGoDEC = abs(diffDEC);
    dirDEC = (diffDEC > 0) ? 1 : -1;
    currentDelayDEC = MANUAL_SPEED_START; 
    isGuidingDEC = false;
    movingDEC = (stepsToGoDEC > 0);

    Serial.print("0#");
    return;
  }

  // ===== Sync =====
  if (c.startsWith("CM")) {
    currentRAsteps = targetRAsteps;
    currentDECsteps = targetDECsteps;
    Serial.print("OK#");
    return;
  }

  // ===== Ręczne sterowanie (Strzałki) =====
  if (c == "Me") { stepsToGoRA = 500000; dirRA = 1;  currentDelayRA = MANUAL_SPEED_START; isGuidingRA = false; movingRA = true; return; }
  if (c == "Mw") { stepsToGoRA = 500000; dirRA = -1; currentDelayRA = MANUAL_SPEED_START; isGuidingRA = false; movingRA = true; return; }
  if (c == "Mn") { stepsToGoDEC = 500000; dirDEC = 1;  currentDelayDEC = MANUAL_SPEED_START; isGuidingDEC = false; movingDEC = true; return; }
  if (c == "Ms") { stepsToGoDEC = 500000; dirDEC = -1; currentDelayDEC = MANUAL_SPEED_START; isGuidingDEC = false; movingDEC = true; return; }

  // ===== Stop RUCHU =====
  if (c.startsWith("Q")) {
    movingRA = false;
    movingDEC = false;
    isGuidingRA = false;
    isGuidingDEC = false;
    stepsToGoRA = 0;
    stepsToGoDEC = 0;
    lastTrackingStep = micros();
    Serial.print("1#");
    return;
  }

  // ===== Inne komendy czasu/ustawień =====
  if (c.startsWith("SL")) {
    Serial.print("1#");
    return;
  }

  // Nierozpoznane komendy po prostu ignorujemy bez odsyłania pustego '#'
}

// ================= PARSOWANIE I FORMATOWANIE POZYCJI =================
String formatRA() {
  float hours = (currentRAsteps / RA_STEPS_PER_DEG) / 15.0;
  while (hours < 0) hours += 24;
  while (hours >= 24) hours -= 24;

  int h = (int)hours;
  int m = (int)((hours - h) * 60);
  int s = (int)((((hours - h) * 60) - m) * 60);

  char buf[20];
  sprintf(buf, "%02d:%02d:%02d#", h, m, s);
  return String(buf);
}

String formatDEC() {
  float dec = (float)currentDECsteps / DEC_STEPS_PER_DEG;
  char sign = (dec >= 0) ? '+' : '-';
  float absDec = abs(dec);

  int d = (int)absDec;
  int m = (int)((absDec - d) * 60);
  int s = (int)((((absDec - d) * 60) - m) * 60);

  char buf[20];
  sprintf(buf, "%c%02d*%02d:%02d#", sign, d, m, s);
  return String(buf);
}

long parseRA(String s) {
  s.trim();
  int h = 0, m = 0;
  float sec = 0.0;

  int tokens = sscanf(s.c_str(), "%d:%d:%f", &h, &m, &sec);
  if (tokens < 2) return targetRAsteps;

  float hours = (float)h + ((float)m / 60.0f) + (sec / 3600.0f);
  return (long)(hours * 15.0f * RA_STEPS_PER_DEG);
}

long parseDEC(String s) {
  s.trim();
  char sign = '+';
  int startIdx = 0;

  if (s.length() > 0 && (s.charAt(0) == '+' || s.charAt(0) == '-')) {
    sign = s.charAt(0);
    startIdx = 1;
  }

  int d = 0, m = 0;
  float sec = 0.0;

  int tokens = sscanf(s.substring(startIdx).c_str(), "%d%*c%d%*c%f", &d, &m, &sec);
  if (tokens < 1) return targetDECsteps;

  float degrees = (float)d + ((float)m / 60.0f) + (sec / 3600.0f);
  if (sign == '-') degrees = -degrees;

  return (long)(degrees * DEC_STEPS_PER_DEG);
}
