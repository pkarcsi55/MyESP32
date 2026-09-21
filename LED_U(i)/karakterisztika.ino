/*
  https://pilath.wordpress.com/2026/08/22/led-ek-nyitofeszultsegenek-merese-fillerekbol
  ESP32-C3 SuperMini – LED U-I karakterisztika

  Kapcsolás:

  +5 V -> LED anód
  LED katód -> BC547 kollektor
  BC547 emitter -> 150 Ω -> GND

  GPIO0 -> 150 Ω-os sönt felső pontja
  GPIO1 -> LED katód / 2
  GPIO3 -> LED anód / 2
  GPIO4 -> PWM, 1 kΩ + 10 µF

  GPIO0–GND: 100 nF
  GPIO1–GND: 100 nF
  GPIO3–GND: 100 nF ajánlott

  Indítás: s vagy S

  Kimenet:
  pontosan 20 darab:
  I_mA,U_LED_V
*/


// ======================= Lábkiosztás =======================

const byte SHUNT_PIN   = 0;
const byte UK_HALF_PIN = 1;
const byte UA_HALF_PIN = 3;
const byte PWM_PIN     = 4;


// ======================= Kapcsolási adatok =======================

const float SHUNT_R_OHM = 150.0;
const float DIVIDER_FACTOR = 2.0;

// Ha a GPIO3 nincs bekötve, ezt használja:
const float DEFAULT_UA_HALF_V = 2.500;

// A GPIO3 mérése akkor tekinthető hihetőnek,
// ha ebbe a tartományba esik.
const float UA_HALF_MIN_VALID_V = 2.0;
const float UA_HALF_MAX_VALID_V = 3.0;


// ======================= PWM =======================

const uint32_t PWM_FREQUENCY = 5000;
const byte PWM_RESOLUTION = 10;
const int PWM_MAX = 1023;


// ======================= Mérési beállítások =======================

const int OUTPUT_POINTS = 20;

const float MIN_CURRENT_MA = 0.05;
const float MAX_CURRENT_MA = 8.0;

const int PRESCAN_STEP   = 5;
const int PRESCAN_SETTLE = 80;

const int MEASURE_SETTLE = 300;

const int ADC_SAMPLES = 100;


// Dinamikusan meghatározott értékek
float shuntOffsetV = 0.0;
float ledAnodeV    = 5.0;


// ======================= ADC-mérés =======================

float readVoltage(byte pin)
{
  // Csatornaváltás utáni első mérés eldobása
  analogReadMilliVolts(pin);
  delayMicroseconds(200);

  uint32_t sumMilliVolt = 0;

  for (int i = 0; i < ADC_SAMPLES; i++)
  {
    sumMilliVolt += analogReadMilliVolts(pin);
    delayMicroseconds(300);
  }

  return sumMilliVolt / (ADC_SAMPLES * 1000.0);
}


// Gyorsabb ADC-mérés az előzetes sweephez
float readVoltageQuick(byte pin)
{
  const int samples = 20;

  analogReadMilliVolts(pin);
  delayMicroseconds(200);

  uint32_t sumMilliVolt = 0;

  for (int i = 0; i < samples; i++)
  {
    sumMilliVolt += analogReadMilliVolts(pin);
    delayMicroseconds(300);
  }

  return sumMilliVolt / (samples * 1000.0);
}


// ======================= PWM =======================

void setPWM(int value)
{
  value = constrain(value, 0, PWM_MAX);
  ledcWrite(PWM_PIN, value);
}


// ======================= Árammérés =======================

float quickCurrentmA()
{
  float uShunt = readVoltageQuick(SHUNT_PIN);

  float currentmA =
    ((uShunt - shuntOffsetV) / SHUNT_R_OHM) * 1000.0;

  if (currentmA < 0.0)
    currentmA = 0.0;

  return currentmA;
}


// ======================= Sweep =======================

void runSweep()
{
  // LED kikapcsolása
  setPWM(0);
  delay(500);


  // -------------------------------------------------------
  // 1. Sönt ADC-offset mérése
  // -------------------------------------------------------

  shuntOffsetV = readVoltage(SHUNT_PIN);


  // -------------------------------------------------------
  // 2. LED anódfeszültségének mérése GPIO3-on
  // -------------------------------------------------------

  float uaHalfMeasured = readVoltage(UA_HALF_PIN);

  if (uaHalfMeasured >= UA_HALF_MIN_VALID_V &&
      uaHalfMeasured <= UA_HALF_MAX_VALID_V)
  {
    // GPIO3 bekötve, a mért értéket használjuk
    ledAnodeV = uaHalfMeasured * DIVIDER_FACTOR;
  }
  else
  {
    // GPIO3 nincs bekötve vagy az érték nem hihető
    ledAnodeV = DEFAULT_UA_HALF_V * DIVIDER_FACTOR;
  }


  // -------------------------------------------------------
  // 3. Gyors előmérés
  // -------------------------------------------------------

  int pwmFrom = 0;
  int pwmTo   = PWM_MAX;

  bool lowerFound = false;
  bool upperFound = false;

  for (int pwm = 0; pwm <= PWM_MAX; pwm += PRESCAN_STEP)
  {
    setPWM(pwm);
    delay(PRESCAN_SETTLE);

    float currentmA = quickCurrentmA();

    if (!lowerFound && currentmA >= MIN_CURRENT_MA)
    {
      pwmFrom = pwm;
      lowerFound = true;
    }

    if (currentmA >= MAX_CURRENT_MA)
    {
      pwmTo = pwm;
      upperFound = true;
      break;
    }
  }

  if (!lowerFound)
  {
    pwmFrom = 0;
    pwmTo   = PWM_MAX;
  }
  else if (!upperFound)
  {
    pwmTo = PWM_MAX;
  }


  // -------------------------------------------------------
  // 4. Pontosan 20 adatpár kiírása
  // -------------------------------------------------------

  // Első pont: origó
  Serial.println("0.0000,0.0000");

  // További 19 valódi mérési pont
  const int measuredPoints = OUTPUT_POINTS - 1;

  for (int point = 0; point < measuredPoints; point++)
  {
    // A pwmFrom...pwmTo tartomány felosztása
    // pontosan 19 mérési pontra
    long numerator =
      (long)(pwmTo - pwmFrom) * point;

    int pwm =
      pwmFrom +
      (numerator + (measuredPoints - 2) / 2) /
      (measuredPoints - 1);

    setPWM(pwm);
    delay(MEASURE_SETTLE);


    // Söntfeszültség
    float uShunt = readVoltage(SHUNT_PIN);

    // LED katódfeszültségének fele
    float ukHalf = readVoltage(UK_HALF_PIN);


    // LED-áram
    float currentmA =
      ((uShunt - shuntOffsetV) / SHUNT_R_OHM) * 1000.0;

    if (currentmA < 0.0)
      currentmA = 0.0;


    // LED katódfeszültsége
    float uCathode =
      ukHalf * DIVIDER_FACTOR;


    // LED-en eső feszültség
    float uLed =
      ledAnodeV - uCathode;

    if (uLed < 0.0)
      uLed = 0.0;


    // Data Streamer: kizárólag két szám
    Serial.print(currentmA, 4);
    Serial.print(',');
    Serial.println(uLed, 4);
  }


  // Mérési folyamat vége
  setPWM(0);
}


// ======================= Inicializálás =======================

void setup()
{
  Serial.begin(115200);
  delay(1000);

  pinMode(SHUNT_PIN, INPUT);
  pinMode(UK_HALF_PIN, INPUT);
  pinMode(UA_HALF_PIN, INPUT);

  analogReadResolution(12);

  // 8 mA × 150 Ω = 1,2 V
  analogSetPinAttenuation(SHUNT_PIN, ADC_6db);

  // Katód/2: legfeljebb kb. 2,5 V
  analogSetPinAttenuation(UK_HALF_PIN, ADC_11db);

  // Anód/2: kb. 2,5 V
  analogSetPinAttenuation(UA_HALF_PIN, ADC_11db);

  bool pwmOk = ledcAttach(
    PWM_PIN,
    PWM_FREQUENCY,
    PWM_RESOLUTION
  );

  if (!pwmOk)
  {
    while (true)
      delay(1000);
  }

  setPWM(0);
}


// ======================= Főprogram =======================

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();

    if (command == 's' || command == 'S')
    {
      // A sorvége és más bent maradt karakterek eldobása
      while (Serial.available() > 0)
        Serial.read();

      runSweep();
    }
  }

  delay(10);
}
