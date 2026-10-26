jadi gini

running code nya di Wokwi
buat diagramnya juga di Wokwi
disini cuma buat naro file nya aja okeehhhh

---

# 🔥 Sistem Peringatan Dini Kebakaran Hutan (Karhutla)
**Tim**: Testarossa's Vanguard — Universitas Ahmad Dahlan

## 📁 Struktur Berkas
- `diagram.json`: Konfigurasi skematik sirkuit Wokwi (ESP32 DevKit V4, DHT22, MQ-2, Potensiometer Tanah, 3 LED, Buzzer).
- `main.cpp`: Firmware mikrokontroler ESP32 (membaca sensor, kendali aktuator, publish/subscribe MQTT).
- `libraries.txt`: Daftar pustaka Arduino untuk Wokwi (`DHT sensor library`, `PubSubClient`, `ArduinoJson`).
- `frontend/dashboard.html`: Antarmuka dashboard real-time Bento UI (Tailwind CSS, Chart.js, REST API).
- `Karhutla_Colab_Dashboard.ipynb`: Notebook Jupyter siap jalan di Google Colab.
- `backend_server.py`: Skrip server Python backend lokal (alternatif tanpa Colab).

## 🚀 Cara Menjalankan

### Cara 1: Menjalankan di Google Colab (Direkomendasikan)
1. Unggah file `Karhutla_Colab_Dashboard.ipynb` ke [Google Colab](https://colab.research.google.com).
2. Jalankan seluruh Cell secara berurutan:
   - Cell 1: Install dependensi (`paho-mqtt`, `fastapi`, `uvicorn`, dll).
   - Cell 2: Menyiapkan file dashboard dari repositori GitHub.
   - Cell 3: Menghubungkan sesi Colab ke HiveMQ MQTT (`broker.hivemq.com:1883`).
   - Cell 4: Menjalankan FastAPI REST API & Decision Engine.
   - Cell 5: Membuka dashboard interaktif langsung di dalam iframe Colab.
3. Jalankan simulasi di [Wokwi](https://wokwi.com).
   - ESP32 akan otomatis mengirim telemetri ke Colab.
   - Colab akan mengevaluasi risiko dan mengirim perintah balik ke LED & Buzzer di Wokwi.

### Cara 2: Menjalankan Secara Lokal di Komputer
```bash
pip install paho-mqtt
python backend_server.py
```
Buka browser di `http://localhost:8000` dan jalankan simulasi Wokwi.