import threading
import json
import time
import serial
from flask import Flask, jsonify, render_template_string

app = Flask(__name__)

# قفل التزامن (Thread Lock) لضمان تبادل آمن للبيانات بين خيط القراءة التسلسلية وخيط خادم الويب
data_lock = threading.Lock()

# المخزن المشترك لآخر بيانات واصلة من لوحات الأردوينو
latest_telemetry = {
    "solar_tracker": {
        "ms": 0,
        "mode": 0, # 0 = Tracking, 1 = Boost
        "angle": 90,
        "ldrL": 0,
        "ldrR": 0,
        "diff": 0,
        "offset": 0,
        "vout": 0.0,
        "duty": 0,
        "pwmHz": 50000,
        "updated_at": 0
    },
    "bms": {
        "ms": 0,
        "A0_V": 0.0,  # جهد وضع الفحص (محدد البطارية)
        "A1_V": 0.0,  # جهد البطارية المقاس فعلياً
        "mode": "12V", # نوع البطارية المكتشفة 12V أو 3.78V
        "updated_at": 0
    }
}

# إعدادات المنافذ التسلسلية لـ Raspberry Pi (تأكد من تعديلها لتطابق منافذك الفعالة)
# تلميح: يمكنك التحقق من المنافذ المتصلة عبر سطر الأوامر باستخدام: ls /dev/tty*
SERIAL_PORT_TRACKER = '/dev/ttyUSB0'  # منفذ أردوينو التتبع ورافع الجهد
SERIAL_PORT_BMS     = '/dev/ttyUSB1'  # منفذ أردوينو مراقب البطارية (BMS)
BAUD_RATE           = 9600

def parse_telemetry_line(line):
    """فك تشفير الأسطر الواردة من الأردوينو بصيغة JSON ومعالجة أي أخطاء في الإرسال."""
    try:
        data = json.loads(line.strip())
        return data
    except json.JSONDecodeError:
        # تجاهل الأسطر المشوشة أو غير المكتملة أثناء الإقلاع
        return None

def serial_reader_thread(port_path, system_key):
    """تابع يعمل في الخلفية للاتصال التلقائي بالمنفذ التسلسلي وقراءة البيانات بدون توقف."""
    print(f"[INFO] Starting Serial Reader Thread for '{system_key}' on {port_path}...")
    while True:
        try:
            # فتح الاتصال بالمنفذ التسلسلي مع مهلة قراءة ثانية واحدة
            with serial.Serial(port_path, BAUD_RATE, timeout=1.0) as ser:
                ser.reset_input_buffer()
                print(f"[SUCCESS] Connected to {system_key} on {port_path}")
                
                while True:
                    if ser.in_waiting > 0:
                        # قراءة السطر وفك التشفير
                        raw_line = ser.readline().decode('utf-8', errors='ignore')
                        parsed_data = parse_telemetry_line(raw_line)
                        
                        if parsed_data:
                            # تحديث المخزن بأمان باستخدام القفل التزامني لمنع تصادم البيانات
                            with data_lock:
                                latest_telemetry[system_key].update(parsed_data)
                                latest_telemetry[system_key]["updated_at"] = time.time()
                                
                    time.sleep(0.01) # تأخير متناهي الصغر لراحة معالج الـ Raspberry Pi
        except serial.SerialException as e:
            print(f"[WARNING] Connection lost on {port_path} ({system_key}). Retrying in 3s... Error: {e}")
            time.sleep(3)
        except Exception as e:
            print(f"[ERROR] Unexpected error in {system_key} thread: {e}")
            time.sleep(3)

# ================= واجهات الـ Web Dashboard (HTML/JS) =================
# واجهة مدمجة مباشرة ومبنية باستخدام Bootstrap 5 و Chart.js لعرض رائع وتفاعلي للمشروع
DASHBOARD_HTML = """
<!DOCTYPE html>
<html lang="ar" dir="rtl">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>بوابة التحكم الذكي بالطاقة الشمسية</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.rtl.min.css" rel="stylesheet">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { background-color: #f4f6f9; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; }
        .card { border: none; border-radius: 12px; box-shadow: 0 4px 12px rgba(0,0,0,0.05); transition: 0.3s; }
        .card:hover { transform: translateY(-3px); box-shadow: 0 6px 16px rgba(0,0,0,0.1); }
        .status-badge { font-size: 0.9rem; padding: 6px 12px; border-radius: 20px; }
        .metric-val { font-size: 2rem; font-weight: bold; color: #2c3e50; }
    </style>
</head>
<body>
    <nav class="navbar navbar-dark bg-dark mb-4">
        <div class="container">
            <span class="navbar-brand mb-0 h1">☀️ نظام الطاقة الشمسية الذكي وإدارة البطارية</span>
            <span class="badge bg-success" id="gateway-status">متصل بالبوابة</span>
        </div>
    </nav>

    <div class="container">
        <!-- صف معلومات النظام الرئيسي -->
        <div class="row mb-4">
            <!-- وحدة التتبع ورافع الجهد -->
            <div class="col-md-8">
                <div class="card p-4 mb-4">
                    <div class="d-flex justify-content-between align-items-center mb-3">
                        <h4 class="m-0 text-primary">⚙️ وحدة التتبع ورافع الجهد (Tracker & Boost)</h4>
                        <span id="tracker-mode" class="badge bg-secondary status-badge">جاري القراءة...</span>
                    </div>
                    <div class="row g-3 text-center">
                        <div class="col-4 border-end">
                            <div class="text-muted">زاوية المحرك (Servo)</div>
                            <div class="metric-val" id="val-angle">90°</div>
                        </div>
                        <div class="col-4 border-end">
                            <div class="text-muted">فرق حساسات LDR</div>
                            <div class="metric-val" id="val-diff">0</div>
                        </div>
                        <div class="col-4">
                            <div class="text-muted">جهد خرج الرفع (Vout)</div>
                            <div class="metric-val text-success" id="val-vout">0.0V</div>
                        </div>
                    </div>
                    <hr>
                    <div class="row text-center">
                        <div class="col-6">
                            <span class="text-muted">مستوى إشارة العرض PWM:</span> <strong id="val-duty">0%</strong>
                        </div>
                        <div class="col-6">
                            <span class="text-muted">تردد التبديل:</span> <strong id="val-pwmHz">50 kHz</strong>
                        </div>
                    </div>
                </div>
            </div>

            <!-- وحدة الـ BMS ومراقبة البطارية -->
            <div class="col-md-4">
                <div class="card p-4 mb-4 bg-white">
                    <h4 class="text-warning mb-3">🔋 نظام حماية البطارية (BMS)</h4>
                    <div class="text-center py-3">
                        <div class="text-muted mb-1">جهد حزمة البطارية الحالي</div>
                        <div class="metric-val text-primary" style="font-size: 2.8rem;" id="bms-voltage">0.00 V</div>
                        <span id="bms-mode" class="badge bg-info mt-2">وضع البطارية: --</span>
                    </div>
                    <hr>
                    <div class="d-flex justify-content-between">
                        <span class="text-muted">إشارة منفذ التشخيص A0:</span>
                        <strong id="bms-a0">0.0V</strong>
                    </div>
                    <div class="d-flex justify-content-between mt-2">
                        <span class="text-muted">حالة تحديث الاتصال:</span>
                        <span class="badge bg-light text-dark" id="bms-time">--</span>
                    </div>
                </div>
            </div>
        </div>

        <!-- الرسم البياني الحي للمراقبة -->
        <div class="card p-4 mb-5">
            <h5 class="mb-3">📈 مراقبة تغير جهد دائرة الرفع وبطارية الـ BMS حياً</h5>
            <div style="height: 300px; width: 100%;">
                <canvas id="liveChart"></canvas>
            </div>
        </div>
    </div>

    <script>
        // إعداد الرسم البياني الحي باستخدام Chart.js
        const ctx = document.getElementById('liveChart').getContext('2d');
        const liveChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    { label: 'جهد خرج رافع الجهد (Vout)', data: [], borderColor: '#2ecc71', tension: 0.2, fill: false },
                    { label: 'جهد البطارية المكتشف (BMS)', data: [], borderColor: '#3498db', tension: 0.2, fill: false }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: { display: true, title: { display: true, text: 'الوقت (ثواني)' } },
                    y: { min: 0, max: 24, title: { display: true, text: 'الجهد (V)' } }
                }
            }
        });

        // دالة لجلب البيانات حياً من خادم Flask كل ثانية واحدة
        async function fetchTelemetry() {
            try {
                const response = await fetch('/api/data');
                const data = await response.json();
                
                // 1. تحديث واجهة التتبع والرفع
                const tr = data.solar_tracker;
                document.getElementById('val-angle').innerText = tr.angle + '°';
                document.getElementById('val-diff').innerText = tr.diff;
                document.getElementById('val-vout').innerText = tr.vout.toFixed(1) + 'V';
                document.getElementById('val-duty').innerText = Math.round((tr.duty/255)*100) + '%';
                document.getElementById('val-pwmHz').innerText = (tr.pwmHz/1000).toFixed(0) + ' kHz';
                
                const trackerBadge = document.getElementById('tracker-mode');
                if (tr.mode === 0) {
                    trackerBadge.innerText = "وضع تتبع الضوء نشط";
                    trackerBadge.className = "badge bg-primary status-badge";
                } else {
                    trackerBadge.innerText = "وضع رافع الجهد 50kHz";
                    trackerBadge.className = "badge bg-danger status-badge";
                }

                // 2. تحديث واجهة الـ BMS
                const bms = data.bms;
                document.getElementById('bms-voltage').innerText = bms.A1_V.toFixed(2) + ' V';
                document.getElementById('bms-mode').innerText = 'وضع حزمة: ' + bms.mode;
                document.getElementById('bms-a0').innerText = bms.A0_V.toFixed(2) + 'V';
                
                if (bms.updated_at > 0) {
                    const elapsed = Math.round(Date.now()/1000 - bms.updated_at);
                    document.getElementById('bms-time').innerText = elapsed + ' ثانية مضت';
                }

                // 3. تحديث خطوط الرسم البياني الحي
                const nowLabel = new Date().toLocaleTimeString();
                liveChart.data.labels.push(nowLabel);
                liveChart.data.datasets[0].data.push(tr.mode === 1 ? tr.vout : null); // عرض جهد التتبع فقط في وضع الرفع
                liveChart.data.datasets[1].data.push(bms.A1_V);

                // إبقاء آخر 15 نقطة قراءة فقط على الشاشة
                if (liveChart.data.labels.length > 15) {
                    liveChart.data.labels.shift();
                    liveChart.data.datasets[0].data.shift();
                    liveChart.data.datasets[1].data.shift();
                }
                liveChart.update();

            } catch (err) {
                console.error("Error fetching telemetry:", err);
                document.getElementById('gateway-status').innerText = "فصل في الشبكة!";
                document.getElementById('gateway-status').className = "badge bg-danger";
            }
        }

        setInterval(fetchTelemetry, 1000); // تحديث دوري ممتد كل ثانية واحدة
    </script>
</body>
</html>
"""

# ================= مسارات الخادم (Flask Routing) =================

@app.route('/')
def home():
    """عرض صفحة لوحة التحكم الرئيسية."""
    return render_template_string(DASHBOARD_HTML)

@app.route('/api/data')
def get_data():
    """توفير آخر قراءة مجمعة حية كمخرجات JSON للتطبيق البرمجي."""
    with data_lock:
        return jsonify(latest_telemetry)

# ================= بدء التشغيل الأساسي =================

if __name__ == '__main__':
    # 1. تفعيل خيط القراءة لوحدة التتبع الشمسي ورافع الجهد (Timer1)
    tracker_thread = threading.Thread(
        target=serial_reader_thread, 
        args=(SERIAL_PORT_TRACKER, "solar_tracker"), 
        daemon=True
    )
    tracker_thread.start()

    # 2. تفعيل خيط القراءة لوحدة مراقبة وحماية البطارية (BMS)
    bms_thread = threading.Thread(
        target=serial_reader_thread, 
        args=(SERIAL_PORT_BMS, "bms"), 
        daemon=True
    )
    bms_thread.start()

    # 3. تشغيل خادم الويب على المنفذ 5000 ومتاح لجميع الأجهزة في الشبكة المحلية
    # يمكنك تصفحه عبر: http://<Raspberry_Pi_IP_Address>:5000
    app.run(host='0.0.0.0', port=5000, debug=False, use_reloader=False)