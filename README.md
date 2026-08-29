# Smart Solar Tracker, 50kHz Boost Converter & IoT BMS Gateway

[![Embedded C++](https://img.shields.io/badge/Firmware-Embedded%20C%2B%2B-blue.svg)](https://www.arduino.cc/)
[![Target MCU](https://img.shields.io/badge/MCU-ATmega328P-green.svg)](https://www.microchip.com/)
[![Gateway](https://img.shields.io/badge/Edge%20Gateway-Raspberry%20Pi-red.svg)](https://www.raspberrypi.org/)
[![Dashboard](https://img.shields.io/badge/Telemetry-Flask%20%7C%20JSON-orange.svg)](https://flask.palletsprojects.com/)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

An end-to-end, resource-optimized Solar Energy Management System. This project integrates a single-axis active solar tracker, an ATmega register-driven **50kHz DC-DC Boost Converter**, and a **3S Li-ion Battery Management System (BMS)** with real-time JSON telemetry streamed to a Raspberry Pi Flask dashboard.

---

## 📐 System Architecture

```plaintext
                           +-------------------------------+
                           |        SOLAR RADIATION        |
                           +---------------+---------------+
                                           |
                                           v
                   +-----------------------------------------------+
                   |           SINGLE-AXIS PHOTO HARVEST           |
                   |   * 2x LDR Sensor Array                       |
                   |   * 25° Fixed Elevation Solar Mount           |
                   +-----------------------+-----------------------+
                                           |
                                           v
+----------------------------------------------------------------------------------+
| ATmega328P Microcontroller Firmware (Subsystem 1)                                |
|                                                                                  |
|   [Mode 0: Solar Tracking]               [Mode 1: 50kHz DC-DC Boost Regulation]  |
|   * Servo Positional Sweeping (30°-150°) * Register Fast PWM (ICR1=319, WGM14)   |
|   * Auto-Trim Drift Canceller            * Dynamic Incremental Feedback Loop     |
|   * Multi-Sample Noise Averaging         * 20V Target Regulation Output          |
|                                                                                  |
|                 \------- 3-Second Hold State Machine Switch -------/             |
+------------------------------------------+---------------------------------------+
                                           |
                                           | JSON Telemetry Stream (USB Serial)
                                           v
+----------------------------------------------------------------------------------+
| Raspberry Pi Linux Edge Gateway                                                  |
|                                                                                  |
|   * Persistent Serial Device Ingestion (/dev/serial/by-id/...)                   |
|   * Multi-threaded Data Normalizer (Python serial & json)                        |
|   * Lightweight Flask REST API (/status) & Real-time Web Dashboard (Chart.js)    |
+------------------------------------------+---------------------------------------+
                                           ^
                                           | JSON Telemetry Stream (USB Serial)
+------------------------------------------+---------------------------------------+
| ATmega328P BMS Battery Monitor (Subsystem 2)                                     |
|                                                                                  |
|   * 3S Li-ion Battery Voltage Sensing via 30k/10k Divider (A1)                   |
|   * Dynamic Mode / Threshold Input via 10k/10k Divider (A0)                      |
|   * Anti-Chatter Hysteresis ±0.15V & 25-sample Averaging                         |
|   * Over/Under Protection Relay Actuator & Diagnostic Tri-LED State Logic        |
+----------------------------------------------------------------------------------+
```

---

## ⚡ Key Technical Highlights

### 1. Register-Level High-Frequency PWM (50kHz)
* **Direct Register Manipulation:** Configured ATmega328P Timer1 registers directly (`TCCR1A`, `TCCR1B`, `ICR1 = 319`) under **WGM14 (Fast PWM)** to achieve a stable, low-noise **50kHz switching frequency** for the custom discrete DC-DC boost topology.
* **Bypassing Abstractions:** Avoided standard Arduino `analogWrite()` jitter and hardware constraints, allowing tight closed-loop power conversion regulation toward a **20.0V target** using a dynamic software incremental feedback mechanism.

### 2. Hardware Timer Contention Resolution
* **Resource Conflict:** The default Arduino Servo library relies heavily on **Timer1**, which clapped directly with our 50kHz PWM generator on pin D9 (OC1A).
* **Dual-Mode Finite State Machine (FSM):** Designed an asynchronous, non-blocking state machine that cleanly detaches and frees Timer1 servo routines before re-initializing the high-frequency registers for the Boost converter. Toggling between modes is handled smoothly via a debounce-safe **3-second long press** on digital pin D8.

### 3. Noise Immunity & Signal Conditioning
* **LDR Auto-Trim:** Integrated an active dynamic calibration offset algorithm with deadband clamping to eliminate steady-state mismatch errors between asymmetric photoresistors.
* **Averaging & Hysteresis:** Raw ADC readings for both the LDRs and battery voltage dividers are filtered using a **20-to-25 sample rolling average** to isolate high-frequency switching noise. Additionally, strict **$\pm0.15\text{V}$ software hysteresis bands** are enforced on battery relay triggers to entirely eradicate relay contact chatter and bounce near cutoff points.

### 4. Edge Telemetry Pipeline
* **Rigid JSON Serialization:** Multi-node telemetry streams are serialized into rigid, lightweight JSON packets emitted over USB Serial every 200ms.
* **Persistent Linux Binding:** Configured robust serial device mapping on the Raspberry Pi Gateway via `/dev/serial/by-id/` symlinks, guaranteeing fault-tolerant communications and preventing port name swapping (ttyUSB0 vs ttyUSB1) across system reboots.

---

## 🛠️ Hardware Bill of Materials (BOM)

| Subsystem | Components | Function |
| :--- | :--- | :--- |
| **Tracking & Actuation** | Micro Servo (SG90/MG90S), 2x LDRs, 10k Resistors | Single-axis East-West active tracking at a fixed 25° elevation tilt. |
| **Power Electronics** | Power Inductor, Fast-Recovery Diode, N-Channel Switching MOSFET, Filter Capacitors | Discrete DC-DC Boost Stage (5V input to ~20V target output). |
| **BMS Protection** | 3S Li-ion Cells, Relay Module, Voltage Dividers (10k/10k, 30k/10k), Status LEDs | Over/Under voltage protection, state signaling, and battery status. |
| **HMI & Edge Compute** | 20x4 Parallel LCD, Raspberry Pi, 2x ATmega328P MCUs | Local physical diagnostics, JSON telemetry serialization, and Flask IoT gateway. |

---

## 📊 Telemetry Payloads

### ☀️ Solar Tracker Node Payload
```json
{
  "ms": 2319,
  "mode": 0,
  "angle": 83,
  "ldrL": 1017,
  "ldrR": 1006,
  "diff": 4,
  "offset": 0,
  "vout": 0.00,
  "duty": 120,
  "pwmHz": 50000
}
```

### 🔋 BMS Node Payload
```json
{
  "ms": 11022,
  "mode": "12V",
  "A0_V": 4.21,
  "A1_V": 11.93
}
```

---

## 📁 Repository Structure

```plaintext
├── firmware/
│   ├── solar_tracker_boost/
│   │   └── solar_tracker_boost.ino   # Main FSM, Timer1 registers & tracking firmware
│   └── bms_module/
│       └── bms_module.ino            # Battery sensing, hysteresis & relay control
├── gateway/
│   ├── app.py                        # Flask server streaming telemetry to web dashboard
│   ├── telemetry_reader.py           # Python serial parser using persistent IDs
│   ├── requirements.txt              # Gateway dependencies
│   ├── templates/
│   │   └── index.html                # Web dashboard template
│   └── static/
│       ├── css/
│       │   └── style.css             # Dashboard responsive styling
│       └── js/
│           └── dashboard.js           # Live Chart.js telemetry handler
├── docs/
│   ├── hardware_wiring.md            # Circuit diagrams and pinout mapping
│   ├── architecture.md               # Block diagram and specs
│   ├── upwork_case_study.md          # Upwork portfolio overview
│   └── images/                       # Hardware assembly and prototype captures
├── scripts/
│   └── solar-gateway.service         # Systemd daemon config for background execution
├── .gitignore
├── LICENSE
└── README.md
```

---

## ⚙️ Quick Start & Deployment

### 1. Flash Subsystem Firmware
* Open `firmware/solar_tracker_boost/solar_tracker_boost.ino` in the Arduino IDE or PlatformIO and upload it to the **Tracker ATmega328P MCU**.
* Open `firmware/bms_module/bms_module.ino` and flash it to the **BMS ATmega328P MCU**.

### 2. Configure Edge Gateway (Raspberry Pi)
```bash
# Clone the repository
git clone https://github.com/<your-username>/smart-solar-tracking-boost-bms.git
cd smart-solar-tracking-boost-bms/gateway

# Install required dependencies
pip install -r requirements.txt

# Start the Flask telemetry gateway
python3 app.py
```
Open a browser and navigate to `http://<raspberrypi-ip>:5000` to view the live dashboard.

---

## 🪵 Engineering Resilience: Overcoming Constraints

This system is built as a functional **Proof-of-Concept (PoC)** under intense real-world logistic and environmental challenges, transforming failures into valuable engineering experience:
* **Structural Ingenuity (Reinforced Cardboard):** Due to frequent electricity outages and a lack of proper woodworking tools, the mechanical frame was engineered out of multi-layered, wood-glued **reinforced cardboard**. This yielded an exceptionally lightweight, rigid, and rapidly modifiable mount holding a fixed 25° tilt.
* **Designing Under Limitations:** Laptop battery issues restricted complex 3D CAD modeling. Instead, precise hand measurements and visual-spatial mapping were leveraged to align linkages and minimize lateral servo friction.
* **Electrical Safety & Hardware Failures:** Earlier testing with an ESP32 resulted in hardware failure due to 3.3V/5V logic conversion and high transient currents on the power lines. This valuable lesson prompted a redesign that prioritized robust power regulation, switching back to ATmega328P MCUs, and moving the telemetry aggregation to a dedicated Raspberry Pi, laying down a highly scalable and resilient IoT network architecture.

---

## 🗺️ Future Roadmap
* [ ] Implement a full **closed-loop PID controller** for the Boost converter output regulation to minimize steady-state error.
* [ ] Integrate passive cell balancing and temperature monitoring ($NTC$) into the BMS module.
* [ ] Migrate to a unified SQLite database on the Raspberry Pi for long-term historical logging and analytics.

---

## 👨‍💻 Author
**AbdalAziz Saleh**  
*Embedded Systems & IoT Firmware Developer*  
* Specialized in low-level register optimization, signal processing, and robust telemetry gateway design.

[Upwork Profile](https://upwork.com) • [LinkedIn](https://linkedin.com) • [GitHub](https://github.com)
