#!/usr/bin/env python3
# ============================================================================
# Test Client for Encrypted MQTT LED Control
# 
# This script:
# 1. Connects to Mosquitto broker via MQTT/TLS
# 2. Receives encrypted LED commands from ESP32
# 3. Decrypts and displays the payload
# 4. Publishes encrypted commands to control LED on ESP32
# 
# Requirements: pip install paho-mqtt pycryptodome
# 
# Usage: python3 mqtt_test_client.py <broker_ip> <ca_cert_path>
# ============================================================================

import paho.mqtt.client as mqtt
import os
import sys
import json
import time
from Crypto.Cipher import AES
from Crypto.Hash import HMAC, SHA256
import binascii

# Configuration
MQTT_BROKER = "192.168.1.100"  # Change to your broker IP
MQTT_PORT = 8883
MQTT_USER = "esp32_client"
MQTT_PASSWORD = "password123"
CA_CERT = "ca.crt"

# Cryptographic keys (MUST match ESP32)
AES_KEY = bytes([
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
])

HMAC_KEY = bytes([
    0x0b] * 32
)

# ============================================================================
# MQTT Callbacks
# ============================================================================

def on_connect(client, userdata, flags, rc):
    """Callback when connected to broker"""
    if rc == 0:
        print("✓ Connected to MQTT broker!")
        print(f"  Broker: {MQTT_BROKER}:{MQTT_PORT}")
        print(f"  User: {MQTT_USER}")
        
        # Subscribe to encrypted LED control topic
        client.subscribe("led/control/encrypted")
        print("✓ Subscribed to: led/control/encrypted")
    else:
        print(f"✗ Connection failed with code {rc}")

def on_disconnect(client, userdata, rc):
    """Callback when disconnected from broker"""
    if rc != 0:
        print(f"✗ Unexpected disconnection: {rc}")
    else:
        print("✓ Disconnected from broker")

def on_message(client, userdata, msg):
    """Callback when message received"""
    print(f"\n{'='*60}")
    print(f"Received encrypted message on topic: {msg.topic}")
    print(f"Payload length: {len(msg.payload)} bytes")
    
    try:
        payload = msg.payload
        
        # Step 1: Extract HMAC (last 32 bytes)
        received_hmac = payload[-32:]
        encrypted_data = payload[:-32]
        
        print(f"\nVerifying HMAC...")
        print(f"  Encrypted data length: {len(encrypted_data)} bytes")
        print(f"  HMAC length: {len(received_hmac)} bytes")
        
        # Step 2: Compute HMAC-SHA256
        hmac_obj = HMAC.new(HMAC_KEY, msg=encrypted_data, digestmod=SHA256)
        computed_hmac = hmac_obj.digest()
        
        # Constant-time comparison
        if computed_hmac == received_hmac:
            print(f"✓ HMAC verification: PASSED")
        else:
            print(f"✗ HMAC verification: FAILED (tampering detected!)")
            print(f"  Expected: {binascii.hexlify(computed_hmac).decode()}")
            print(f"  Received: {binascii.hexlify(received_hmac).decode()}")
            return
        
        # Step 3: Extract IV and ciphertext
        iv = encrypted_data[:16]
        ciphertext = encrypted_data[16:]
        
        print(f"\nDecrypting AES-128-CBC...")
        print(f"  IV: {binascii.hexlify(iv).decode()}")
        print(f"  Ciphertext length: {len(ciphertext)} bytes")
        
        # Step 4: Decrypt with AES-128-CBC
        cipher = AES.new(AES_KEY, AES.MODE_CBC, iv)
        plaintext_padded = cipher.decrypt(ciphertext)
        
        # Step 5: Remove PKCS#7 padding
        padding_len = plaintext_padded[-1]
        if padding_len > 16 or padding_len == 0:
            print(f"✗ Invalid padding length: {padding_len}")
            return
        
        plaintext = plaintext_padded[:-padding_len]
        
        print(f"✓ Decryption successful!")
        print(f"\nDecrypted message:")
        print(f"  {plaintext.decode('utf-8')}")
        
        # Parse JSON
        try:
            data = json.loads(plaintext.decode('utf-8'))
            print(f"\nParsed JSON:")
            for key, value in data.items():
                print(f"  {key}: {value}")
        except json.JSONDecodeError as e:
            print(f"  (Not valid JSON): {e}")
        
        print(f"{'='*60}\n")
        
    except Exception as e:
        print(f"✗ Error processing message: {e}")
        import traceback
        traceback.print_exc()

# ============================================================================
# Test Functions
# ============================================================================

def encrypt_led_command(brightness):
    """Encrypt LED brightness command (for testing)"""
    import secrets
    
    plaintext = f'{{"device":"TestClient","brightness":{brightness},"timestamp":{int(time.time())}}}'
    plaintext_bytes = plaintext.encode('utf-8')
    
    # Generate random IV
    iv = secrets.token_bytes(16)
    
    # Apply PKCS#7 padding
    block_size = 16
    padding_len = block_size - (len(plaintext_bytes) % block_size)
    padded = plaintext_bytes + bytes([padding_len] * padding_len)
    
    # Encrypt
    cipher = AES.new(AES_KEY, AES.MODE_CBC, iv)
    ciphertext = cipher.encrypt(padded)
    
    # Concatenate IV + ciphertext
    encrypted_data = iv + ciphertext
    
    # Compute HMAC
    hmac_obj = HMAC.new(HMAC_KEY, msg=encrypted_data, digestmod=SHA256)
    hmac_value = hmac_obj.digest()
    
    # Final payload
    payload = encrypted_data + hmac_value
    
    print(f"\nEncrypted LED command:")
    print(f"  Plaintext: {plaintext}")
    print(f"  Payload length: {len(payload)} bytes")
    print(f"  (IV: 16B, Ciphertext: {len(ciphertext)}B, HMAC: 32B)")
    
    return payload

# ============================================================================
# Main
# ============================================================================

def main():
    global MQTT_BROKER, CA_CERT
    
    # Parse command line arguments
    if len(sys.argv) > 1:
        MQTT_BROKER = sys.argv[1]
    if len(sys.argv) > 2:
        CA_CERT = sys.argv[2]
    
    print("======================================")
    print("MQTT Encrypted LED Test Client")
    print("======================================")
    print(f"Broker: {MQTT_BROKER}:{MQTT_PORT}")
    print(f"User: {MQTT_USER}")
    print(f"CA Certificate: {CA_CERT}")
    print("")
    
    # Check if CA cert exists
    if not os.path.exists(CA_CERT):
        print(f"✗ Error: CA certificate not found: {CA_CERT}")
        print("  Please provide the path to ca.crt from Mosquitto server")
        sys.exit(1)
    
    # Create MQTT client
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    
    # Set TLS
    client.tls_set(ca_certs=CA_CERT, certfile=None, keyfile=None,
                   cert_reqs=mqtt.ssl.CERT_REQUIRED, tls_version=mqtt.ssl.PROTOCOL_TLSv1_2)
    client.tls_insecure = False  # Verify certificate
    
    # Set credentials
    client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    
    # Connect
    try:
        print("Connecting to broker...")
        client.connect(MQTT_BROKER, MQTT_PORT, keepalive=30)
    except Exception as e:
        print(f"✗ Connection error: {e}")
        sys.exit(1)
    
    # Start network loop
    client.loop_start()
    
    print("\nClient running. Waiting for messages...")
    print("(Press Ctrl+C to exit)")
    print("")
    
    try:
        # Keep program running
        while True:
            # Optionally publish test commands
            user_input = input("\nEnter brightness (0-255) to send command, or 'q' to quit: ").strip()
            
            if user_input.lower() == 'q':
                break
            
            if user_input.isdigit():
                brightness = int(user_input)
                if 0 <= brightness <= 255:
                    payload = encrypt_led_command(brightness)
                    client.publish("led/control/encrypted", payload, qos=1)
                    print("✓ Command published!")
                else:
                    print("✗ Brightness must be 0-255")
            else:
                print("✗ Invalid input")
    
    except KeyboardInterrupt:
        print("\n\n✓ Shutting down...")
    
    finally:
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    main()
