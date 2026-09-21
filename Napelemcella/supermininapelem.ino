/*
  ESP32-C3 Super Mini – Intelligens 30 pontos automatikus tartománykereső terhelésmérés
  Hardver: MCP602 transzimpedancia-váltó (220 Ohm), +bemenet eltolva 22k/2.2k osztóval.
  Tranzisztoros (BC547) söntöléssel.  
  Kimenet (Soros port, 115200 baud):
    PWM,UBE,U(V),I(mA)
*/

#include <Arduino.h>

// ======================== Lábkiosztás ========================
const uint8_t PWM_PIN   = 4;
const uint8_t U_PIN     = 0;  // GPIO 0: Cella feszültség U(V)
const uint8_t I_PIN     = 1;  // GPIO 1: Áram mérése az MCP602-ről
const uint8_t UBE_PIN   = 3;  // GPIO 3: BC547 bázis-emitter feszültség UBE

const uint8_t LED_PIN              = 8;  // Beépített állapotjelző LED GPIO8
const uint8_t MEGVILAGITO_LED_PIN  = 5;  // Megvilágító LED kapcsolása, aktív HIGH

// ======================== Mérési paraméterek ========================
const byte RESZLETES_MINTAK_SZAMA = 200; // Átlagolás a végleges pontos méréshez
const byte GYORS_MINTAK_SZAMA     = 32;  // Kevesebb minta elegendő a határkereséshez

const unsigned long BEALLASI_IDO_MS      = 500;  // Beállási idő a részletes pontok között
const unsigned long GYORS_BEALLASI_IDO_MS = 80;   // Gyors beállási idő az előméréshez

const byte GYORS_PWM_LEPES = 5;
const byte RESZLETES_PONTOK_MAX = 30; // Pontosan 30 releváns pontra korlátozva

// Határérték arányok a hasznos tartomány levágásához
const float FELSO_HATAR_FESZULTSEG_ARANY = 0.10;
const float ALSO_HATAR_ARAM_ARANY        = 0.03;

// Globális változó a dinamikus nullpont eltolásnak (PWM=0-nál kalibrálva)
float dinamikus_offset_V = 0.340; 
const float IU_ELLENALLAS_OHM = 220.0;//A transzimpedancia-ellenállás 220 Ω
// ======================== PWM beállítások ========================
const uint32_t PWM_FREKVENCIA_HZ = 5000;
const uint8_t PWM_FELBONTAS_BIT  = 8;

struct Meres {
  float u_V;
  float ube_V;
  float aram_mA;
};

// ======================== Függvények ========================

float feszultsegMerese(uint8_t pin, uint16_t mintakSzama) {
  uint32_t osszeg_mV = 0;
  analogReadMilliVolts(pin); // Első mérés eldobása a belső kondenzátor kisütéséhez
  delayMicroseconds(50);
  
  for (uint16_t i = 0; i < mintakSzama; i++) {
    osszeg_mV += analogReadMilliVolts(pin);
    delayMicroseconds(30);
  }
  return (osszeg_mV / (float)mintakSzama) / 1000.0;
}

void terhelesKikapcsolasa() {
  ledcWrite(PWM_PIN, 0);
}

Meres cellaMerese(uint16_t mintakSzama) {
  Meres adat;
  adat.u_V   = feszultsegMerese(U_PIN, mintakSzama);
  adat.ube_V = feszultsegMerese(UBE_PIN, mintakSzama);

  float i_nyers_volt = feszultsegMerese(I_PIN, mintakSzama); 
  float i_korrigalt_volt = i_nyers_volt - dinamikus_offset_V;
  
  if (i_korrigalt_volt < 0.0) i_korrigalt_volt = 0.0;
 adat.aram_mA =(i_korrigalt_volt / IU_ELLENALLAS_OHM) * 1000.0;
   
  return adat;
}

void adatKiirasa(int pwm, float ube_V, float u_V, float aram_mA) {
  // Pontos kért sorrend: PWM, UBE, U(V), I(mA)
  Serial.print(pwm);
  Serial.print(',');
  Serial.print(ube_V, 3);
  Serial.print(',');
  Serial.print(u_V, 3);
  Serial.print(',');
  Serial.println(aram_mA, 3);
}

/*
  Gyors előmérés a tranzisztor hasznos vezérlési tartományának feltérképezésére.
*/
void pwmHatarokKeresese(int &pwmFelsoHatar, int &pwmAlsoHatar) {
  terhelesKikapcsolasa();
  delay(300);
  float uresjarasiFeszultsegV = feszultsegMerese(U_PIN, GYORS_MINTAK_SZAMA);

  ledcWrite(PWM_PIN, 255);
  delay(300);
  float nyers_volt = feszultsegMerese(I_PIN, GYORS_MINTAK_SZAMA);
  float rovidzarasiAramMilliAmper = max(0.0f, nyers_volt - dinamikus_offset_V);

  float felsoFeszultsegKuszobV = uresjarasiFeszultsegV * FELSO_HATAR_FESZULTSEG_ARANY;
  float alsoAramKuszobMilliAmper = rovidzarasiAramMilliAmper * ALSO_HATAR_ARAM_ARANY;

  pwmFelsoHatar = 255;
  pwmAlsoHatar = 0;
  bool felsoHatarMegvan = false;

  for (int pwmErtek = 255; pwmErtek >= 0; pwmErtek -= GYORS_PWM_LEPES) {
    ledcWrite(PWM_PIN, pwmErtek);
    delay(GYORS_BEALLASI_IDO_MS);

    float cellaFeszultsegV = feszultsegMerese(U_PIN, GYORS_MINTAK_SZAMA);
    float nyers_i = feszultsegMerese(I_PIN, GYORS_MINTAK_SZAMA);
    float cellaAramMilliAmper = max(0.0f, nyers_i - dinamikus_offset_V);

    if (!felsoHatarMegvan && cellaFeszultsegV >= felsoFeszultsegKuszobV) {
      pwmFelsoHatar = min(255, pwmErtek + GYORS_PWM_LEPES);
      felsoHatarMegvan = true;
    }

    if (felsoHatarMegvan && cellaAramMilliAmper <= alsoAramKuszobMilliAmper) {
      pwmAlsoHatar = max(0, pwmErtek - GYORS_PWM_LEPES);
      break;
    }
  }

  if (pwmFelsoHatar <= pwmAlsoHatar) {
    pwmFelsoHatar = 255;
    pwmAlsoHatar = 0;
  }
}

void meresiSorozatVegrehajtasa() {
  digitalWrite(LED_PIN, HIGH); 
  terhelesKikapcsolasa();
  delay(500); 

  // --- AUTOMATIKUS OFFSET KALIBRÁCIÓ (PWM = 0) ---
  dinamikus_offset_V = feszultsegMerese(I_PIN, RESZLETES_MINTAK_SZAMA);

  // Az ofszetmérés után kapcsoljuk be a megvilágítást.
  digitalWrite(MEGVILAGITO_LED_PIN, HIGH);

  // Előmérés indítása a határok megállapításához
  int pwmFelsoHatar;
  int pwmAlsoHatar;
  pwmHatarokKeresese(pwmFelsoHatar, pwmAlsoHatar);

  int pwmTartomany = pwmFelsoHatar - pwmAlsoHatar;
  byte meresiPontokSzama = min(int(RESZLETES_PONTOK_MAX), pwmTartomany + 1);

  // Fejléc kiírása
  Serial.println("PWM,UBE,U(V),I(mA)");

  // Részletes mérés végrehajtása pontosan a kiszámolt releváns tartományon (max 30 pont)
  for (byte pont = 0; pont < meresiPontokSzama; pont++) {
    
    // Elosztjuk a 30 pontot egyenletesen a hasznos PWM tartományban
    int pwmErtek = pwmFelsoHatar - (long(pont) * pwmTartomany + (meresiPontokSzama - 1) / 2) / (meresiPontokSzama - 1);

    ledcWrite(PWM_PIN, pwmErtek);
    delay(BEALLASI_IDO_MS);

    Meres terhelt = cellaMerese(RESZLETES_MINTAK_SZAMA);
    terhelesKikapcsolasa();

    // Kiírás a kért formátumban: PWM, UBE, U(V), I(mA)
    adatKiirasa(pwmErtek, terhelt.ube_V, terhelt.u_V, terhelt.aram_mA);
  }

  terhelesKikapcsolasa();
  digitalWrite(MEGVILAGITO_LED_PIN, LOW);
  digitalWrite(LED_PIN, LOW); 
}

// ======================== Setup & Loop ========================

void setup() {
  Serial.begin(115200);
  delay(2000); 

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(MEGVILAGITO_LED_PIN, OUTPUT);
  digitalWrite(MEGVILAGITO_LED_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(U_PIN,   ADC_11db);
  analogSetPinAttenuation(I_PIN,   ADC_11db);
  analogSetPinAttenuation(UBE_PIN, ADC_11db);

  bool pwmSikeres = ledcAttach(PWM_PIN, PWM_FREKVENCIA_HZ, PWM_FELBONTAS_BIT);
  if (!pwmSikeres) {
    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(500);
    }
  }

  ledcWrite(PWM_PIN, 0);
}

void loop() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 's' || c == 'S') {
      meresiSorozatVegrehajtasa();
    }
  }
  delay(10);
}
