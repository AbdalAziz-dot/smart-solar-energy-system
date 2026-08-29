#include <Arduino.h>

/**
 * ============================================================================
 * Project: 3S Li-ion Battery Management System (BMS) Monitor
 * Target MCU: ATmega328P (Arduino Uno / Nano)
 * Author: Hassan Saleh
 * 
 * Technical Features:
 *  - 25-sample ADC rolling average for noise-free voltage sensing.
 *  - Dual-scale hardware voltage dividers:
 *      * A0 (Mode Select Sense): 10k / 10k Divider (Ratio = 2.0x)
 *      * A1 (Battery Pack Sense): 30k / 10k Divider (Ratio = 4.0x)
 *  - Hysteresis thresholds to eliminate relay chatter near cutoff points.
 *  - Real-time diagnostic LED state signaling (Red/Yellow/Green).
 *  - JSON telemetry streamed over Serial (9600 baud) every 250ms.
 * ============================================================================
 */

// ===== توصيل المنافذ (Pins) =====
const int PIN_AN0     = A0;   // منفذ تحديد الوضع (عبر مقسم جهد 10k/10k) [1]
const int PIN_AN1     = A1;   // منفذ قياس فولتية البطارية (عبر مقسم جهد 30k/10k) [1]
const int LED_RED     = 8;    // مؤشر إنذار هبوط جهد البطارية (أحمر) [1]
const int LED_YEL     = 9;    // مؤشر الوضع الحالي للبطارية (أصفر) [1]
const int LED_GRN     = 10;   // مؤشر اكتمال الشحن والامتلاء (أخضر) [1]
const int RELAY_PIN   = 13;   // منفذ التحكم بالمُرحل (Relay) لحماية الدائرة [1]

// ===== ثوابت القياس ومقسمات الجهد =====
const float VREF      = 5.00; // مرجع جهد الـ ADC في متحكم الأردوينو [2]
const float DIV_A0    = 2.0;  // مقسم جهد 10k/10k يضاعف القراءة مرتين للحصول على الجهد الأصلي [2]
const float DIV_A1    = 4.0;  // مقسم جهد 30k/10k يضاعف القراءة 4 مرات لقياس حتى 20V بأمان [2]

// ===== عتبات تفعيل الهسترة لتغيير الوضع (A0 Mode Switching) =====
const float A0_THRESHOLD = 3.00; // عتبة تبديل الوضع الافتراضي (3 فولت) [2]
const float A0_HYST      = 0.15; // قيمة الهسترة لمنع التذبذب قرب الـ 3 فولت [2]

// ===== عتبات جهد البطاريات المستهدفة للقياس والقطع (A1 Battery Thresholds) =====
const float FULL_3V   = 4.00;  // شحن كامل لوضع الخلية الواحدة (4.00 فولت) [2]
const float EMPTY_3V  = 3.50;  // تفريغ كامل لوضع الخلية الواحدة (3.50 فولت) [2]
const float FULL_12V  = 12.60; // شحن كامل لحزمة 3S (12.60 فولت) [2]
const float EMPTY_12V = 10.00; // تفريغ كامل لحزمة 3S (10.00 فولت) لمنع تلف الخلايا [3]

// تخصيص نوع الريلاي: اجعلها false إذا كان الريلاي يعمل بإشارة منخفضة Active-LOW [3]
const bool RELAY_ACTIVE_HIGH = true;

// ===== متغيرات إدارة الحالة والتوقيت =====
bool modeLow          = false; // false = وضع قياس حزمة 12V، true = وضع قياس خلية واحدة 3.78V [3]
unsigned long lastBlink = 0;   // مؤقت لتثبيت تردد وميض الـ LED
bool blinkState       = false; // حالة الوميض للـ LED الأصفر

/* ================= دالة قياس الجهد وتصفية التشويش ================= */
float readVoltage(int pin, float divider) {
  long sum = 0;
  for (int i = 0; i < 25; i++) { // أخذ 25 عينة متتالية [36، 39]
    sum += analogRead(pin);
    delay(2); // تأخير بسيط بين العينات لضمان استقرار إشارة الـ ADC [3]
  }
  float adcV = (sum / 25.0) * (VREF / 1023.0); // حساب متوسط القراءات وتحويلها لجهد [3]
  return adcV * divider; // الضرب في معامل مقسم الجهد للحصول على الجهد الفعلي المقاس [3]
}

/* ================= دالة التحكم بالريلاي للقطع والتوصيل ================= */
void relaySet(bool lowMode) {
  bool on = lowMode; // تفعيل الريلاي في وضع المنخفض لحماية الحزمة
  if (RELAY_ACTIVE_HIGH) {
    digitalWrite(RELAY_PIN, on ? HIGH : LOW); [4]
  } else {
    digitalWrite(RELAY_PIN, on ? LOW : HIGH); [4]
  }
}

/* ================= دالة تبديل أوضاع الحماية ================= */
void setMode(bool lowMode) {
  modeLow = lowMode;
  relaySet(modeLow); // تحديث حالة الريلاي تبعا للوضع الجديد [4]
}

void setup() {
  // تهيئة مخارج ومداخل المتحكم
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YEL, OUTPUT);
  pinMode(LED_GRN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  
  setMode(false); // البدء بالوضع الافتراضي لحزمة 12V [4]
  
  Serial.begin(9600); // تهيئة الاتصال التسلسلي بسرعة 9600 [4]
  delay(1500);
  Serial.println("READY"); // إشارة جاهزية النظام للبدء [4]
}

void loop() {
  // 1. قراءة الفولتية المصححة والمصفاة للحساسين A0 و A1 [4]
  float vA0 = readVoltage(PIN_AN0, DIV_A0); [4]
  float vA1 = readVoltage(PIN_AN1, DIV_A1); [5]

  // 2. اتخاذ قرار الوضع (Mode Selector) مع استخدام عتبات الهسترة لمنع التذبذب [36، 41]
  if (!modeLow && vA0 > (A0_THRESHOLD + A0_HYST)) {
    setMode(true); // التحول لوضع الخلية المنخفضة (3.78V) [5]
  } 
  else if (modeLow && vA0 < (A0_THRESHOLD - A0_HYST)) {
    setMode(false); // العودة لوضع الحزمة الكاملة (12V) [5]
  }

  // 3. إشارة الـ LED الأصفر للدلالة على الوضع الحالي [5]
  if (modeLow) {
    // وميض مستمر بمعدل 500 ميلي ثانية في وضع الخلية الواحدة
    if (millis() - lastBlink >= 500) { [5]
      lastBlink = millis();
      blinkState = !blinkState;
    }
    digitalWrite(LED_YEL, blinkState ? HIGH : LOW); [5]
  } else {
    // إضاءة ثابتة في وضع حزمة الـ 12V [5]
    digitalWrite(LED_YEL, HIGH); [5]
  }

  // 4. تقييم حالة البطارية لتشغيل الـ LED المناسب (الأحمر للإنذار والأخضر للاكتمال) [5]
  float fullV  = modeLow ? FULL_3V  : FULL_12V; [5]
  float emptyV = modeLow ? EMPTY_3V : EMPTY_12V; [6]

  if (vA1 >= fullV) {
    digitalWrite(LED_GRN, HIGH); // شحن كامل [6]
    digitalWrite(LED_RED, LOW);
  } else if (vA1 <= emptyV) {
    digitalWrite(LED_GRN, LOW);
    digitalWrite(LED_RED, HIGH); // انخفاض حاد للجهد - تفعيل الإنذار [6]
  } else {
    // الجهد في النطاق الطبيعي المتوسط
    digitalWrite(LED_GRN, LOW); [6]
    digitalWrite(LED_RED, LOW); [6]
  }

  // 5. تجميع القياسات وإرسالها دورياً بصيغة JSON نظيفة للتسجيل على الـ Raspberry Pi [42، 44]
  Serial.print("{\"ms\":"); Serial.print(millis()); [6]
  Serial.print(",\"A0_V\":"); Serial.print(vA0, 2); [6]
  Serial.print(",\"A1_V\":"); Serial.print(vA1, 2); [6]
  Serial.print(",\"mode\":"); Serial.print(modeLow ? "\"3.78V\"" : "\"12V\""); [6]
  Serial.println("}"); [7]

  delay(250); // تكرار القراءة كل ربع ثانية لراحة المعالج وإبقاء الاتصال مستقراً [7]
}