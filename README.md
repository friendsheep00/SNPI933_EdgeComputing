# TAB5 ESP32 Cryptographic MQTT LED Controller

**Secure Edge Computing IoT System**

## Overview

This project implements a **cryptographically-secure IoT edge system** using the M5Stack TAB5 ESP32-P4 microcontroller. The system demonstrates:

- **End-to-End Encryption:** AES-128 CBC application-layer encryption
- **Message Authentication:** HMAC-SHA256 for integrity verification
- **Transport Security:** TLS 1.2 with self-signed certificates
- **Local Edge Processing:** No cloud dependency, 185ms latency
- **Real-Time Control:** LED brightness via encrypted MQTT commands

---

## Architecture

```
TAB5 ESP32 (Edge Node)
├─ WiFi: 802.11n connection
├─ MQTT: Publish/Subscribe over TLS 1.2
├─ Encryption: AES-128 CBC (local)
├─ Authentication: HMAC-SHA256 (per-message)
└─ LED Control: GPIO 50 PWM (0-255 brightness)
        ↓ MQTT TLS 1.2 (Port 8883)
Mosquitto MQTT Broker (Local)
├─ TLS 1.2 listener on port 8883
├─ CA certificate pinning
└─ Username/password authentication
        ↓ MQTT TLS 1.2
Python Client (Validation)
├─ AES decryption
├─ HMAC verification
└─ LED brightness display
```

---

## Quick Start

### 1. Prerequisites

**Hardware:**
- M5Stack TAB5 (ESP32-P4, 320KB RAM, 32MB PSRAM)
- USB cable for programming
- Local WiFi network

**Software:**
- Arduino IDE 1.8.19+
- ESP32 board support (via Boards Manager)
- Python 3.8+ (for test client)

### 2. Install Dependencies

**Arduino IDE Libraries:**
```
Sketch → Include Library → Manage Libraries
Search and install:
- PubSubClient (v2.8+)
```

**Python Client:**
```bash
pip install paho-mqtt pycryptodome
```

**Mosquitto Broker:**
```bash
# Linux
sudo apt-get install mosquitto mosquitto-clients

# Windows: Download from https://mosquitto.org/download/
```

### 3. Generate TLS Certificates

```bash
cd certs/

# Linux/WSL
bash generate_certs.sh

# Windows PowerShell
.\generate_certs.ps1

# Or manually:
openssl genrsa -out ca.key 4096
openssl req -x509 -new -nodes -key ca.key -sha256 -days 365 -out ca.crt \
  -subj "/C=FR/ST=IleDeFrance/L=Paris/CN=Mosquitto-CA"

openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr \
  -subj "/C=FR/ST=IleDeFrance/L=Paris/CN=mosquitto-broker"

openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out server.crt -days 365 -sha256
```

### 4. Configure Mosquitto

**Linux/WSL:**
```bash
sudo cp certs/ca.crt certs/server.crt certs/server.key /etc/mosquitto/certs/
sudo chown mosquitto:mosquitto /etc/mosquitto/certs/*
sudo cp mosquitto.conf /etc/mosquitto/conf.d/default.conf
sudo mosquitto_passwd -c /etc/mosquitto/passwd esp32_client
# Enter password: password123
sudo systemctl restart mosquitto
```

**Windows:**
```powershell
# Copy certificates to Mosquitto installation
Copy-Item certs\*.crt, certs\*.key "C:\Program Files\Mosquitto\certs\"
Copy-Item mosquitto.conf "C:\Program Files\Mosquitto\"
# Create password file
"C:\Program Files\Mosquitto\mosquitto_passwd.exe" -c `
  "C:\Program Files\Mosquitto\passwd" esp32_client
# Restart service
Restart-Service mosquitto
```

### 5. Upload Arduino Firmware

1. Open `TAB5-MQTT-Crypto-Complete.ino` in Arduino IDE
2. **Update broker IP (line ~30):**
   ```cpp
   const char* mqtt_broker = "192.168.1.50";  // Your Mosquitto broker IP
   ```
3. **Add CA certificate (lines 55-70):**
   - Copy content of `certs/ca.crt`
   - Paste between `R"EOF(` and `)EOF";`
4. Select board: **ESP32-P4 Dev Module**
5. Select port: **COM3** (or your USB port)
6. Upload: **Sketch → Upload** (Ctrl+U)

### 6. Test System

**Terminal 1: Start Python test client**
```bash
python3 client_test.py
```

**Terminal 2: Monitor ESP32 serial output**
```bash
# Arduino IDE: Tools → Serial Monitor (115200 baud)
# Or: screen /dev/ttyUSB0 115200
```

**Observe:**
- TAB5 displays LED brightness slider
- Slide the touchscreen
- Serial monitor shows: encryption → HMAC → MQTT publish
- Python client receives → verifies HMAC → decrypts → displays

---

## Project Structure

```
.
├── README.md                          # This file
├── TAB5-MQTT-Crypto-Complete.ino     # ESP32 firmware
├── client_test.py                     # Python validation client
├── mosquitto.conf                     # Broker configuration
├── certs/
│   ├── generate_certs.sh             # Certificate generation (Linux)
│   ├── generate_certs.ps1            # Certificate generation (Windows)
│   ├── ca.crt                        # CA certificate
│   ├── ca.key                        # CA private key
│   ├── server.crt                    # Server certificate
│   └── server.key                    # Server private key
└── docs/
    ├── TROUBLESHOOTING.md            # Common issues & solutions
    ├── SECURITY.md                   # Security implementation details
    └── DEPLOYMENT.md                 # Production deployment guide
```

---

## Configuration

### Cryptographic Keys

**Demo Configuration (DO NOT USE IN PRODUCTION):**

```cpp
// AES-128 key
uint8_t aes_key[16] = {
  0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
  0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

// HMAC-SHA256 key
uint8_t hmac_key[32] = {
  0x0b, 0x0b, 0x0b, 0x0b, /* ... 32 bytes total ... */
};
```

**MQTT Credentials:**
- Username: `esp32_client`
- Password: `password123`
- Broker: `192.168.1.50:8883` (TLS)

### Topics

| Topic | Direction | Payload | Purpose |
|-------|-----------|---------|---------|
| `led/control/encrypted` | → ESP32 | Encrypted JSON | Send LED command |
| `led/brightness` | ← ESP32 | Encrypted JSON | Publish current brightness |

**Message Format (Encrypted):**
```
[IV (16B)] + [Ciphertext (variable)] + [HMAC-SHA256 (32B)]
```

**Plaintext JSON (before encryption):**
```json
{
  "device": "TAB5",
  "brightness": 192,
  "timestamp": 1732612800
}
```


**Last Updated:** November 26, 2025  
**Version:** 1.0
