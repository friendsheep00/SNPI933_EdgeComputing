#!/bin/bash
# ============================================================================
# Setup Script for Mosquitto MQTT Broker with TLS Security
# 
# This script automates:
# 1. Installation of Mosquitto and OpenSSL
# 2. Certificate generation
# 3. User authentication setup
# 4. Service configuration and restart
# 
# Usage: sudo bash setup-mosquitto.sh
# ============================================================================

set -e

echo "========================================"
echo "Mosquitto MQTT Broker TLS Setup"
echo "========================================"
echo ""

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo "ERROR: This script must be run as root (use sudo)"
   exit 1
fi

# Step 1: Install Mosquitto and dependencies
echo "[1/5] Installing Mosquitto and OpenSSL..."
apt-get update -qq
apt-get install -y mosquitto mosquitto-clients openssl

# Step 2: Create certificates directory
echo "[2/5] Creating certificate directory..."
mkdir -p /etc/mosquitto/certs
chmod 700 /etc/mosquitto/certs

# Step 3: Generate CA certificate
echo "[3/5] Generating CA certificate..."
openssl genrsa -out /tmp/ca.key 4096 2>/dev/null
openssl req -x509 -new -nodes -key /tmp/ca.key -sha256 -days 365 \
  -out /tmp/ca.crt \
  -subj "/C=FR/ST=IleDeFrance/L=LeVidocq/CN=Mosquitto-CA" 2>/dev/null

# Step 4: Generate server certificate
echo "[4/5] Generating server certificate..."
openssl genrsa -out /tmp/server.key 2048 2>/dev/null
openssl req -new -key /tmp/server.key -out /tmp/server.csr \
  -subj "/C=FR/ST=IleDeFrance/L=LeVidocq/CN=mosquitto-broker" 2>/dev/null
openssl x509 -req -in /tmp/server.csr -CA /tmp/ca.crt -CAkey /tmp/ca.key \
  -CAcreateserial -out /tmp/server.crt -days 365 -sha256 2>/dev/null

# Step 5: Install certificates with proper permissions
echo "[5/5] Installing certificates..."
cp /tmp/ca.crt /tmp/server.crt /tmp/server.key /etc/mosquitto/certs/
chown mosquitto:mosquitto /etc/mosquitto/certs/*
chmod 600 /etc/mosquitto/certs/server.key
chmod 644 /etc/mosquitto/certs/*.crt

# Clean up temporary files
rm -f /tmp/ca.key /tmp/server.key /tmp/server.csr /tmp/server.crt /tmp/ca.srl

# Step 6: Configure Mosquitto
echo "[6/6] Configuring Mosquitto..."
cat > /etc/mosquitto/conf.d/default.conf << 'EOF'
listener 8883
cafile /etc/mosquitto/certs/ca.crt
certfile /etc/mosquitto/certs/server.crt
keyfile /etc/mosquitto/certs/server.key
tls_version tlsv1.2

allow_anonymous false
password_file /etc/mosquitto/passwd
EOF

# Step 7: Create default user
echo "[7/7] Creating MQTT user..."
mosquitto_passwd -c -b /etc/mosquitto/passwd esp32_client password123

# Step 8: Restart Mosquitto service
echo ""
echo "Restarting Mosquitto service..."
systemctl restart mosquitto
systemctl enable mosquitto

echo ""
echo "========================================"
echo "✓ Setup complete!"
echo "========================================"
echo ""
echo "Broker details:"
echo "  Host: $(hostname -I | awk '{print $1}')"
echo "  Port: 8883 (TLS)"
echo "  Username: esp32_client"
echo "  Password: password123"
echo ""
echo "CA Certificate location:"
echo "  /etc/mosquitto/certs/ca.crt"
echo ""
echo "To test (copy ca.crt first):"
echo "  mosquitto_sub -h <host> -p 8883 --cafile ca.crt \\"
echo "    -u esp32_client -P password123 -t 'led/#'"
echo ""
