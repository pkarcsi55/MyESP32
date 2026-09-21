/*
  ESP32-C3 Super Mini – Optimalizált terhelésmérés (Tol=30, Ig=255, Step=7 10 Ohm, BC547, Rb 1K)
  Kimenet: PWM,Ug_V,Ut_V,Uk_V,I_mA  (CSV, vessző, tizedespont)
  Nagy felbontású átlagolással a lépcsőzetesség ellen.
*/

#include <Arduino.h>

// ======================== Lábkiosztás ========================
const uint8_t PWM_PIN   = 4;
const uint8_t UK_PIN    = 0;
const uint8_t USONT_PIN = 1;
const uint8_t UGATE_PIN = 3;

const uint8_t LED_PIN = 8;   // beépített LED GPIO8

// ======================== Feszültségosztó szorzók ========================
const float UK_SZORZO    = 1.0;
const float USONT_SZORZO = 1.0;
const float UGATE_SZORZO = 1.0;

// ======================== Mérési paraméterek ========================
const float SONT_OHM      = 10.0;
const float MAX_ARAM_mA   = 130.0;
const uint16_t MINTAK_SZAMA = 256;  // Átlagolás megemelve 16-ról 256-ra a sima görbéért

const unsigned long PIHENO_MS           = 300;
const unsigned long MERESI_IMPULZUS_MS  = 300;

// ======================== PWM beállítások ========================
const uint32_t PWM_FREKVENCIA_HZ = 5000;
const uint8_t PWM_FELBONTAS_BIT  = 8;

struct Meres {
  float uk_V;
  float usont_V;
  float ug_V;
  float aram_mA;
};

// ======================== Függvények ========================

float feszultsegMerese(uint8_t pin, float szorzo) {
  uint32_t osszeg_mV = 0;
  analogReadMilliVolts(pin);
  delayMicroseconds(100);
  
  // A megnövelt ciklus kiszűri az ADC bizonytalanságait
  for (uint16_t i = 0; i < MINTAK_SZAMA; i++) {
    osszeg_mV += analogReadMilliVolts(pin);
    delayMicroseconds(50);
  }
  float atlag_V = (osszeg_mV / (float)MINTAK_SZAMA) / 1000.0;
  return atlag_V * szorzo;
}

void terhelesKikapcsolasa() {
  ledcWrite(PWM_PIN, 0);
}

Meres meres() {
  Meres adat;
  adat.uk_V    = feszultsegMerese(UK_PIN, UK_SZORZO);
  adat.usont_V = feszultsegMerese(USONT_PIN, USONT_SZORZO);
  adat.ug_V    = feszultsegMerese(UGATE_PIN, UGATE_SZORZO);

  float usont_esés = adat.uk_V - adat.usont_V;
  adat.aram_mA = usont_esés * 1000.0 / SONT_OHM;
  if (adat.aram_mA < 0.0) adat.aram_mA = 0.0;
  return adat;
}

void adatKiirasa(int pwm, float ug_V, float ut_V, float uk_V, float aram_mA) {
  Serial.print(pwm);
  Serial.print(',');
  Serial.print(ug_V, 3);
  Serial.print(',');
  Serial.print(ut_V, 3);
  Serial.print(',');
  Serial.print(uk_V, 3);
  Serial.print(',');
  Serial.println(aram_mA, 2);
}

void automatikusMeres() {
  digitalWrite(LED_PIN, HIGH); 
  terhelesKikapcsolasa();
  delay(300);

  Serial.println("PWM,Ug_V,Ut_V,Uk_V,I_mA");

  int pwm = 30;        // Lejjebb vettem 30-ra, hogy a kis áramok is látszódjanak
  int lepesköz = 7;    

  while (pwm <= 255) { 
    terhelesKikapcsolasa();
    delay(PIHENO_MS);

    float ut_V = feszultsegMerese(UK_PIN, UK_SZORZO);

    ledcWrite(PWM_PIN, pwm);
    delay(MERESI_IMPULZUS_MS);

    Meres terhelt = meres();
    terhelesKikapcsolasa();

    if (terhelt.aram_mA >= MAX_ARAM_mA) {
      adatKiirasa(pwm, terhelt.ug_V, ut_V, terhelt.uk_V, terhelt.aram_mA);
      break;
    }

    adatKiirasa(pwm, terhelt.ug_V, ut_V, terhelt.uk_V, terhelt.aram_mA);

    pwm += lepesköz;
  }

  terhelesKikapcsolasa();
  digitalWrite(LED_PIN, LOW); 
}

// ======================== Setup & Loop ========================

void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println(); 

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(UK_PIN,    ADC_11db);
  analogSetPinAttenuation(USONT_PIN, ADC_11db);
  analogSetPinAttenuation(UGATE_PIN, ADC_11db);

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
      automatikusMeres();
    }
  }
  delay(10);
}

