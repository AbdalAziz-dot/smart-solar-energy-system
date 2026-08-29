#!/usr/bin/env python3
"""
============================================================================
Project: Smart Solar Tracking and Boost Power Management System
Script: serial_reader.py
Description: Standalone telemetry reader and CSV data logger. 
             Reads real-time JSON strings from Arduino over Serial,
             displays them on the terminal, and logs them into a 
             structured CSV file on the Raspberry Pi for further analysis.
============================================================================
"""

import os
import sys
import json
import time
import csv
import serial

# الإعدادات الافتراضية للمنفذ ومعدل البود
DEFAULT_PORT = '/dev/ttyUSB0'
DEFAULT_BAUD = 9600
LOG_FILE_NAME = 'solar_telemetry_log.csv'

def initialize_csv(filename, fieldnames):
    """تهيئة ملف الـ CSV وكتابة العناوين الرئيسية إذا لم يكن الملف موجوداً مسبقاً."""
    file_exists = os.path.isfile(filename)
    try:
        with open(filename, mode='a', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            if not file_exists:
                writer.writeheader()
                print(f"[INFO] Created new log file: '{filename}' with headers.")
    except IOError as e:
        print(f"[ERROR] Failed to initialize CSV file: {e}")

def log_to_csv(filename, data):
    """إضافة حزمة البيانات المستقبلة كصف جديد في ملف الـ CSV مع توقيت النظام."""
    # إضافة طابع زمني دقيق للبيانات المستقبلة
    data['pi_timestamp'] = time.strftime('%Y-%m-%d %H:%M:%S')
    
    # تحديد الحقول بناءً على البيانات المستلمة لجعلها مرنة
    fieldnames = list(data.keys())
    
    # التأكد من تهيئة الملف وكتابة العناوين
    initialize_csv(filename, fieldnames)
    
    try:
        with open(filename, mode='a', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writerow(data)
    except IOError as e:
        print(f"[ERROR] Failed to write data to CSV: {e}")

def main():
    # السماح للمستخدم بتمرير اسم المنفذ عبر سطر الأوامر (مثال: python serial_reader.py /dev/ttyUSB1)
    port = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_BAUD
    
    print("==================================================")
    print("☀️  Smart Solar System - Data Logger & Telemetry")
    print("==================================================")
    print(f"[CONFIG] Reading from: {port} @ {baud} baud")
    print(f"[CONFIG] Logging to: {LOG_FILE_NAME}")
    print("[INFO] Press Ctrl+C to safely exit the program.")
    print("--------------------------------------------------")
    
    try:
        # فتح اتصال المنفذ التسلسلي
        with serial.Serial(port, baud, timeout=1.5) as ser:
            # مسح المخزن المؤقت للبدء بقراءة نظيفة
            ser.reset_input_buffer()
            print("[SUCCESS] Serial port opened. Waiting for data packets...")
            print("--------------------------------------------------")
            
            while True:
                if ser.in_waiting > 0:
                    # قراءة السطر الوارد وفك ترميزه
                    raw_line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if not raw_line:
                        continue
                    
                    try:
                        # فك تشفير حزمة الـ JSON
                        parsed_data = json.loads(raw_line)
                        
                        # طباعة البيانات بشكل منسق وسهل القراءة على الشاشة
                        current_time = time.strftime('%H:%M:%S')
                        print(f"[{current_time}] 📥 Packet Received:")
                        for key, value in parsed_data.items():
                            print(f"   🔹 {key}: {value}")
                        print("-" * 35)
                        
                        # تسجيل البيانات في الملف
                        log_to_csv(LOG_FILE_NAME, parsed_data)
                        
                    except json.JSONDecodeError:
                        # تجاهل الأخطاء الناتجة عن قراءة سطر غير مكتمل عند الإقلاع
                        print(f"[WARNING] Skipping corrupted line: {raw_line}")
                    except Exception as e:
                        print(f"[ERROR] Error processing packet: {e}")
                        
                time.sleep(0.05) # تأخير بسيط لتقليل استهلاك المعالج
                
    except serial.SerialException as e:
        print(f"[FATAL] Connection error on port {port}: {e}")
        print("[SUGGESTION] Check cable connection or permission (sudo usermod -a -G dialout $USER)")
    except KeyboardInterrupt:
        print("\n[INFO] Data logging stopped by user. Exiting gracefully.")

if __name__ == '__main__':
    main()