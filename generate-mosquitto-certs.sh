#!/bin/bash
# ============================================================================
# Certificate Generation Script for Mosquitto MQTT Broker
# 
# This script generates a self-signed CA, server certificate, and configures
# Mosquitto for TLS encryption on port 8883.
# 
# Usage: bash generate_mosquitto_certs.sh
# ============================================================================

set -e  # Exit on error

CERT_DIR="/etc/mosquitto/certs"
CONF_DIR="/etc/mosquitto/conf.d"

echo "========================================"
echo "Mosquitto TLS Certificate Generation"
echo "========================================"
echo ""

# Step 1: Create certificate directory
echo "[1/5] Creating certificate directory..."
sudo mkdir -p $CERT_DIR
sudo chown mosquitto:mosquitto $CERT_DIR
sudo chmod 700 $CERT_DIR

# Step 2: Generate CA private key
echo "[2/5] Generating CA private key (4096-bit RSA)..."
openssl genrsa -out ca.key 4096

# Step 3: Generate self-signed CA certificate
echo "[3/5] Generating self-signed CA certificate..."
openssl req -x509 -new -nodes -key ca.key -sha256 -days 365 -out ca.crt \
  -subj "/C=FR/ST=IleDeFrance/L=LeVidocq/CN=Mosquitto-CA"

# Step 4: Generate server private key
echo "[4/5] Generating server private key (2048-bit RSA)..."
openssl genrsa -out server.key 2048

# Step 5: Generate server certificate signing request (CSR)
echo "[5/5] Generating server certificate signing request..."
openssl req -new -key server.key -out server.csr \
  -subj "/C=FR/ST=IleDeFrance/L=LeVidocq/CN=mosquitto-broker"

# Step 6: Sign server certificate with CA
echo "[6/6] Signing server certificate..."
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out server.crt -days 365 -sha256

# Step 7: Copy certificates to Mosquitto directory
echo "[7/7] Installing certificates..."
sudo cp ca.crt server.crt server.key $CERT_DIR/
sudo chown mosquitto:mosquitto $CERT_DIR/{ca.crt,server.crt,server.key}
sudo chmod 600 $CERT_DIR/server.key

echo ""
echo "========================================"
echo "✓ Certificates generated successfully!"
echo "========================================"
echo ""
echo "Location: $CERT_DIR/"
echo "  - ca.crt (CA certificate for clients)"
echo "  - server.crt (Server certificate)"
echo "  - server.key (Server private key)"
echo ""
echo "Next steps:"
echo "1. Copy ca.crt to your ESP32 project (PEM format)"
echo "2. Configure Mosquitto in $CONF_DIR/mosquitto.conf"
echo "3. Restart Mosquitto: sudo systemctl restart mosquitto"
echo ""
