#include <Arduino.h>
#include <Servo.h>
#include <LiquidCrystal.h>

/* ================= LCD (20x4) =================
RS=12, E=11, D4=5, D5=4, D6=3, D7=2 :حسب توصيلك [1]
*/
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

/* ================= Modes ================= */
enum Mode { MODE_TRACKING = 0, MODE_BOOST = 1 };
Mode mode = MODE_TRACKING;

/* ================= Pins ================= */
const uint8_t PIN_SERVO   = 10;   // إشارة التحكم بمحرك السيرفو [1]
const uint8_t PIN_PWM     = 9;    // مخرج التحكم برفع الجهد (Timer1 OC1A) [2]
const uint8_t PIN_BTN     = 8;    // زر التبديل بين الأوضاع (بالضغط لمدة 3 ثوانٍ) [2]
const uint8_t PIN_LDR_L   = A0;   // حساس الضوء الأيسر [2]
const uint8_t PIN_LDR_R   = A1;   // حساس الضوء الأيمن [2]
const uint8_t PIN_VOUT    = A4;   // قياس جهد خرج دائرة الرفع (عبر مقسم جهد 10k+10k) [2]

/* ================= Serial Telemetry ================= */
const unsigned long TELEM_MS = 200; // زمن دورة إرسال البيانات (200 ميلي ثانية) [2]
unsigned long lastTelem = 0;

/* ================= LCD Update ================= */
const unsigned long LCD_MS = 250;  // زمن تحديث الشاشة [2]
unsigned long lastLCD = 0;

/* ================= Button Hold (3s) ================= */
const unsigned long HOLD_MS = 3000; // مدة الضغط المطول لتغيير الوضع [3]
bool btnWasPressed = false;
unsigned long btnPressStart = 0;
bool toggledThisPress = false;

/* ================= Tracking (Servo + LDR) ================= */
Servo panelServo;
const int SERVO_MIN_ANGLE  = 30;    // الحد الأدنى لزاوية حركة السيرفو [3]
const int SERVO_MAX_ANGLE  = 150;   // الحد الأقصى لزاوية حركة السيرفو [3]
const int SERVO_HOME_ANGLE = 90;    // زاوية البداية (المنتصف) [3]
int servoAngle = SERVO_HOME_ANGLE;
const unsigned long SERVO_UPDATE_MS = 40; // زمن تحديث حركة المحرك [3]
unsigned long lastServoUpdate = 0;

// سلوك حساسات LDR [3]
const bool LDR_INVERT = true;     // اجعلها true إذا كان الضوء يزيد والقراءة التناظرية تنقص [4]
const uint8_t LDR_SAMPLES = 8;    // عدد العينات لحساب المتوسط [4]
const int DIFF_DEADBAND = 8;      // منطقة السماح لمنع الاهتزاز عند التوازن [4]
const int DIFF_MAX_FOR_SPEED = 250;
const int SERVO_STEP_MIN = 1;
const int SERVO_STEP_MAX = 3;
int sensorOffset = 0;             // فارق المعايرة بين الحساس الأيمن والأيسر [4]
const int OFFSET_TRIM_ZONE = 30;  // ميزة التصحيح التلقائي البطيء حول نقطة التوازن [4]

/* ================= Boost (50kHz) ================= */
const unsigned long PWM_FREQ = 50000UL;   // تردد 50 كيلوهرتز لدائرة الرفع [4]
uint8_t duty = 120;                       // دورة العمل الافتراضية [4]
const uint8_t DUTY_MIN = 20;              // الحد الأدنى لمنع حدوث قصر [4]
const uint8_t DUTY_MAX = 200;             // الحد الأقصى لضمان استقرار الدائرة [5]
const float V_TARGET = 20.0;              // الجهد المستهدف (20 فولت) [5]
const float V_TOL    = 0.15;              // نسبة سماح لتذبذب الجهد [5]

// مقسم الجهد (10k + 10k) يضاعف القيمة مرتين للجهد الفعلي [5]
const float R1 = 10000.0;
const float R2 = 10000.0;
const float VREF = 5.0;
const float ADC_STEP = VREF / 1023.0;
const float DIV_GAIN = (R1 + R2) / R2;   // يساوي 2.0 في هذا التوصيل [5]

/* ================= Shared runtime values ================= */
int ldrL = 0, ldrR = 0, diffLR = 0;
float vout = 0.0;

/* ================= Helpers ================= */
// دالة لقراءة المتوسط التناظري لتقليل الضوضاء [5]
int readAnalogAvg(uint8_t pin, uint8_t samples) {
  long acc = 0;
  for (uint8_t i = 0; i < samples; i++) acc += analogRead(pin);
  return (int)(acc / (float)samples);
}

// قراءة الحساس وتعديله حسب سلوك الانعكاس [6]
int readLDR(uint8_t pin) {
  int raw = readAnalogAvg(pin, LDR_SAMPLES);
  return LDR_INVERT ? (1023 - raw) : raw;
}

// حساب خطوة حركة السيرفو بناءً على الفارق [6]
int computeServoStep(int absDiff) {
  if (absDiff <= DIFF_DEADBAND) return 0;
  if (absDiff >= DIFF_MAX_FOR_SPEED) return SERVO_STEP_MAX;
  float ratio = (float)(absDiff - DIFF_DEADBAND) / (float)(DIFF_MAX_FOR_SPEED - DIFF_DEADBAND);
  int step = SERVO_STEP_MIN + (int)(ratio * (SERVO_STEP_MAX - SERVO_STEP_MIN)); [22، 23]
  if (step < SERVO_STEP_MIN) step = SERVO_STEP_MIN;
  if (step > SERVO_STEP_MAX) step = SERVO_STEP_MAX;
  return step;
}

// دالة معايرة الحساسات عند بدء التشغيل [7]
void calibrateLDRs() {
  const uint8_t N = 32;
  long L = 0, R = 0;
  for (uint8_t i = 0; i < N; i++) {
    L += readLDR(PIN_LDR_L);
    R += readLDR(PIN_LDR_R);
    delay(5);
  }
  int avgL = (int)(L / (float)N);
  int avgR = (int)(R / (float)N);
  sensorOffset = avgR - avgL; // الفرق لمعايرة الحساسات تلقائياً [7]
}

// قراءة الجهد الفعلي المنظم والمكبر [7]
float readVout() {
  const uint8_t samples = 20;
  int raw = readAnalogAvg(PIN_VOUT, samples);
  float v_adc = raw * ADC_STEP; [8]
  return v_adc * DIV_GAIN;
}

/* ================= Boost PWM (Timer1 OC1A D9) ================= */
// تهيئة وإعداد السجلات (Registers) مباشرة لتوليد تردد 50kHz Fast PWM [8]
void pwm50k_start() {
  pinMode(PIN_PWM, OUTPUT);
  TCCR1A = 0;
  TCCR1B = 0;
  uint16_t top = (uint16_t)((F_CPU / PWM_FREQ) - 1); // 16MHz/50k=320 => TOP = 319 [8]
  ICR1 = top;
  
  // WGM 14: Fast PWM with ICR1 as TOP [8]
  TCCR1A |= (1 << WGM11);
  TCCR1B |= (1 << WGM12) | (1 << WGM13);
  
  // Clear OC1A on compare match, non-inverting [8]
  TCCR1A |= (1 << COM1A1); [9]
  
  // Prescaler = 1 [9]
  TCCR1B |= (1 << CS10);
}

// إيقاف إشارة الرفع [9]
void pwm_stop() {
  TCCR1A &= ~(1 << COM1A1);
  digitalWrite(PIN_PWM, LOW);
}

// تعيين قيمة دورة العمل (Duty Cycle) [9]
void setDuty(uint8_t d) {
  if (d < DUTY_MIN) d = DUTY_MIN;
  if (d > DUTY_MAX) d = DUTY_MAX;
  duty = d;
  OCR1A = map(duty, 0, 255, 0, ICR1); // مطابقة النطاق مع سجل المقارنة [9]
}

/* ================= Mode transitions ================= */
// الدخول إلى وضع التتبع [9]
void enterTrackingMode() {
  pwm_stop(); // إيقاف دائرة الرفع أولاً لتحرير المؤقت [9]
  
  // ربط السيرفو وتحديد زاوية البداية [25، 26]
  panelServo.attach(PIN_SERVO);
  servoAngle = SERVO_HOME_ANGLE;
  panelServo.write(servoAngle);
  delay(400);
  calibrateLDRs();
  mode = MODE_TRACKING;
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("MODE: TRACKING");
}

// الدخول إلى وضع رافع الجهد [10]
void enterBoostMode() {
  panelServo.detach(); // فصل السيرفو لتحرير المؤقت المشترك (Timer1) [10]
  
  pwm50k_start(); // تفعيل إشارة الرفع عالي التردد [10]
  setDuty(duty);
  mode = MODE_BOOST;
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("MODE: BOOST 50kHz");
}

/* ================= Loop Steps ================= */
// خطوات تشغيل وضع التتبع الشمسي [10]
void trackingStep(unsigned long now) {
  ldrL = readLDR(PIN_LDR_L);
  ldrR = readLDR(PIN_LDR_R);
  int rCal = ldrR - sensorOffset;
  diffLR = ldrL - rCal;
  int absDiff = abs(diffLR);
  
  // التعديل البسيط والتلقائي للمعايرة في منطقة الاتزان [11]
  if (absDiff < OFFSET_TRIM_ZONE) {
    if (diffLR > 0) sensorOffset -= 1;
    else if (diffLR < 0) sensorOffset += 1;
  }
  
  if (now - lastServoUpdate >= SERVO_UPDATE_MS) {
    lastServoUpdate = now;
    int step = computeServoStep(absDiff);
    if (step != 0) {
      if (diffLR > 0) servoAngle -= step; [12]
      else            servoAngle += step;
      if (servoAngle < SERVO_MIN_ANGLE) servoAngle = SERVO_MIN_ANGLE;
      if (servoAngle > SERVO_MAX_ANGLE) servoAngle = SERVO_MAX_ANGLE;
      panelServo.write(servoAngle);
    }
  }
  vout = 0.0; // غير مستخدم في هذا الوضع [12]
}

// خطوات تنظيم ورفع الجهد [12]
void boostStep(unsigned long now) {
  vout = readVout(); // قراءة الجهد المقاس [12]
  
  // حلقة تحكم تزايدية للتعديل التدريجي (Incremental Regulation Loop) [12]
  if (vout < V_TARGET - V_TOL) {
    if (duty < DUTY_MAX) setDuty(duty + 1); [12]
  } else if (vout > V_TARGET + V_TOL) { [13]
    if (duty > DUTY_MIN) setDuty(duty - 1);
  }
  ldrL = 0; ldrR = 0; diffLR = 0; // غير مستخدمة في هذا الوضع [13]
}

/* ================= Telemetry JSON ================= */
// تجميع وإرسال البيانات بصيغة JSON نظيفة عبر الاتصال التسلسلي [13]
void sendTelemetry(unsigned long now) {
  if (now - lastTelem < TELEM_MS) return;
  lastTelem = now;
  
  Serial.print("{\"ms\":");
  Serial.print(now);
  Serial.print(",\"mode\":");
  Serial.print((int)mode);
  Serial.print(",\"angle\":");
  Serial.print(servoAngle); [13]
  Serial.print(",\"ldrL\":");
  Serial.print(ldrL); [14]
  Serial.print(",\"ldrR\":");
  Serial.print(ldrR);
  Serial.print(",\"diff\":");
  Serial.print(diffLR);
  Serial.print(",\"offset\":");
  Serial.print(sensorOffset);
  Serial.print(",\"vout\":");
  Serial.print(vout, 2);
  Serial.print(",\"duty\":");
  Serial.print(duty);
  Serial.print(",\"pwmHz\":");
  Serial.print((unsigned long)PWM_FREQ);
  Serial.println("}");
}

/* ================= LCD ================= */
// عرض البيانات بشكل منسق على شاشة LCD [14]
void updateLCD(unsigned long now) {
  if (now - lastLCD < LCD_MS) return; [15]
  lastLCD = now;
  
  if (mode == MODE_TRACKING) {
    lcd.setCursor(0,0);
    lcd.print("MODE: TRACKING     ");
    lcd.setCursor(0,1);
    lcd.print("Ang:");
    lcd.print(servoAngle);
    lcd.print("  Off:");
    lcd.print(sensorOffset);
    lcd.print("   ");
    lcd.setCursor(0,2);
    lcd.print("L:");
    lcd.print(ldrL);
    lcd.print(" R:");
    lcd.print(ldrR);
    lcd.print("      ");
    lcd.setCursor(0,3);
    lcd.print("Diff:");
    lcd.print(diffLR); [15]
    lcd.print(" Hold D8=3s "); [16]
  } else {
    lcd.setCursor(0,0);
    lcd.print("MODE: BOOST 50kHz  ");
    lcd.setCursor(0,1);
    lcd.print("Target:20.0V       ");
    lcd.setCursor(0,2);
    lcd.print("Vout:");
    lcd.print(vout, 2);
    lcd.print("V         ");
    lcd.setCursor(0,3);
    lcd.print("Duty:");
    lcd.print(map(duty, 0, 255, 0, 100)); // تحويل التناسب لعرض النسبة المئوية % [16]
    lcd.print("%  Hold D8=3s ");
  }
}

/* ================= Button (hold 3s) ================= */
// معالجة الضغط المطول للتبديل بين الوضعين [16]
void handleButton(unsigned long now) {
  int reading = digitalRead(PIN_BTN); // إشارة منخفضة LOW تعني الضغط عند تفعيل المقاومة الداخلية PULLUP [16]
  if (reading == LOW) { [17]
    if (!btnWasPressed) {
      btnWasPressed = true;
      toggledThisPress = false;
      btnPressStart = now;
    } else {
      if (!toggledThisPress && (now - btnPressStart >= HOLD_MS)) {
        toggledThisPress = true;
        // التبديل وتعديل آلة الحالات
        if (mode == MODE_TRACKING) enterBoostMode();
        else enterTrackingMode();
      }
    }
  } else {
    btnWasPressed = false;
    toggledThisPress = false;
  }
}

/* ================= setup / loop ================= */
void setup() {
  Serial.begin(9600); // تفعيل الاتصال التسلسلي [18]
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_PWM, OUTPUT);
  pinMode(PIN_VOUT, INPUT);
  
  lcd.begin(20, 4);
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Solar+Boost System");
  lcd.setCursor(0,1); lcd.print("Starting...");
  delay(600);
  
  // البدء الافتراضي في وضع التتبع [18]
  panelServo.attach(PIN_SERVO);
  servoAngle = SERVO_HOME_ANGLE;
  panelServo.write(servoAngle);
  delay(400);
  calibrateLDRs();
  
  pwm_stop(); // إيقاف إشارة الرفع في البداية [18]
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("MODE: TRACKING"); [19]
}

void loop() {
  unsigned long now = millis(); [19]
  handleButton(now);
  
  // تشغيل الخطوة البرمجية المناسبة للوضع الحالي [19]
  if (mode == MODE_TRACKING) trackingStep(now);
  else                      boostStep(now);
  
  updateLCD(now); // تحديث الشاشة [19]
  sendTelemetry(now); // إرسال القياسات للـ Raspberry Pi [19]
}