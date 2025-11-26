/* ============================================================================
   TAB5 ESP32 - Cryptographic LED Control with MQTT Mosquitto
   
   Project: Secure Edge Computing IoT System
   Platform: M5Stack TAB5 ESP32-P4
   Author: Engineering Student
   Date: November 26, 2025
   
   Architecture:
   - WiFi connectivity (SSID: Kiiway)
   - MQTT over TLS 1.2 (Port 8883, Mosquitto broker)
   - AES-128 CBC encryption of LED commands
   - HMAC-SHA256 message authentication
   - Touchscreen UI for local brightness control
   
   ============================================================================ */

#include <Arduino.h>
#include <Wire.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <esp_aes.h>
#include <mbedtls/md.h>
#include <time.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

// WiFi Configuration
const char* ssid = "Kiiway";
const char* password = "endgame010824";

// MQTT Configuration
const char* mqtt_broker = "192.168.1.100";    // Change to your Mosquitto broker IP
const int mqtt_port = 8883;                   // TLS port
const char* mqtt_user = "esp32_client";
const char* mqtt_password = "password123";    // Change to secure password

// LED Configuration
int ledPin = 50;                              // GPIO pin for LED
int luminosite = 0;                           // Current brightness (0-255)

// Display UI Configuration
int Slargeur = 400;
int Shauteur = 40;
int sliderX, sliderY;

// ============================================================================
// CRYPTOGRAPHIC KEYS (DO NOT HARDCODE IN PRODUCTION!)
// ============================================================================

// AES-128 encryption key (128-bit = 16 bytes)
uint8_t aes_key[16] = {
  0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
  0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

// HMAC key (256-bit = 32 bytes)
uint8_t hmac_key[32] = {
  0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
  0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
  0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
  0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
};

// ============================================================================
// CA CERTIFICATE (from Mosquitto server ca.crt)
// Copy and paste your CA certificate here
// ============================================================================

const char* ca_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDXTCCAkWgAwIBAgIUH5q8rD6P7L/5Z7C+Xp1X6H7N1d8wDQYJKoZIhvcNAQEL
BQAwPjELMAkGA1UEBhMCRlIxDTALBgNVBAgMBElsZUYxDzANBgNVBAcMBkxlVmlk
MRMwEQYDVQQDDApNb3NxdWl0dG9DBCB7MHcwDQYJKoZIhvcNAQEBBQADRgAwQ0RC
QgIBLjELMAkGA1UEBhMCRlIxDTALBgNVBAgMBElsZUYxDzANBgNVBAcMBkxlVmlk
MRMwEQYDVQQDDApNb3NxdWl0dG9DAQQ=
-----END CERTIFICATE-----
)EOF";

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void setLedValue(int val);
void drawSlider(int value);
void connectMQTT();
void messageCallback(char* topic, byte* payload, unsigned int length);
void encryptMessage(const char* plaintext, uint8_t* encrypted, int* encrypted_len);
void computeHMAC(const uint8_t* data, int data_len, uint8_t* hmac_out);
void publishEncryptedLED(int brightness);

// ============================================================================
// SETUP FUNCTION
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize M5Stack display
  M5.begin();
  Wire.end();  // Release I2C pins to avoid WiFi interference

  // Initialize LED pin
  pinMode(ledPin, OUTPUT);
  setLedValue(luminosite);

  // Setup display
  sliderX = (M5.Display.width() - Slargeur) / 2;
  sliderY = (M5.Display.height() - Shauteur) / 2;
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 10);
  M5.Display.println("Initializing...");

  // Configure WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("Connecting to WiFi: %s\n", ssid);

  unsigned long startAttempt = millis();
  const unsigned long timeout = 15000;

  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < timeout) {
    delay(500);
    Serial.print(".");
  }

  M5.Display.fillScreen(BLACK);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.println(WiFi.localIP());
    M5.Display.setCursor(10, 10);
    M5.Display.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi connection failed!");
    M5.Display.println("WiFi FAILED!");
  }

  delay(1000);
  M5.Display.fillScreen(BLACK);
  drawSlider(luminosite);

  // Configure TLS with CA certificate
  espClient.setCACert(ca_cert);

  // Configure MQTT
  mqttClient.setServer(mqtt_broker, mqtt_port);
  mqttClient.setCallback(messageCallback);
  mqttClient.setBufferSize(2048);  // Increased for encrypted payloads
  
  // Connect to MQTT broker
  connectMQTT();

  // Synchronize time via NTP (required for TLS certificate validation)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for NTP time sync...");
  while (time(nullptr) < 1609459200) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("\nTime synchronized!");
}

// ============================================================================
// MQTT CONNECTION HANDLER
// ============================================================================

void connectMQTT() {
  int attempts = 0;
  while (!mqttClient.connected() && attempts < 10) {
    Serial.print("Connecting to MQTT broker...");
    M5.Display.setCursor(10, 50);
    M5.Display.printf("MQTT: Connecting (%d)...", attempts + 1);

    if (mqttClient.connect("ESP32_TAB5", mqtt_user, mqtt_password)) {
      Serial.println("Connected!");
      M5.Display.setCursor(10, 50);
      M5.Display.println("MQTT: Connected!    ");

      // Subscribe to encrypted LED control topic
      mqttClient.subscribe("led/control/encrypted");
      Serial.println("Subscribed to: led/control/encrypted");
    } else {
      Serial.print("Failed, rc=");
      Serial.println(mqttClient.state());
      delay(5000);
      attempts++;
    }
  }
}

// ============================================================================
// LED CONTROL FUNCTIONS
// ============================================================================

void setLedValue(int val) {
  val = constrain(val, 0, 255);
  analogWrite(ledPin, val);
  Serial.printf("LED brightness set to: %d\n", val);
}

void drawSlider(int value) {
  M5.Display.fillRect(sliderX, sliderY, Slargeur, Shauteur, DARKGREY);
  int fillWidth = map(value, 0, 255, 0, Slargeur);
  M5.Display.fillRect(sliderX, sliderY, fillWidth, Shauteur, GREEN);
  M5.Display.setTextSize(4);
  M5.Display.setCursor(sliderX, sliderY - 50);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.printf("Brightness: %d%%  ", (value * 100) / 255);
}

// ============================================================================
// CRYPTOGRAPHIC FUNCTIONS
// ============================================================================

/**
 * Encrypt plaintext message using AES-128 CBC
 * 
 * Output structure: [IV (16B)][Ciphertext (variable)]
 * 
 * @param plaintext: Message to encrypt (null-terminated string)
 * @param encrypted: Output buffer for encrypted data
 * @param encrypted_len: Pointer to store output length
 */
void encryptMessage(const char* plaintext, uint8_t* encrypted, int* encrypted_len) {
  Serial.printf("Encrypting message: %s\n", plaintext);

  // Step 1: Generate random IV (Initialization Vector)
  uint8_t iv[16];
  esp_fill_random(iv, 16);  // Hardware RNG on ESP32
  Serial.println("Random IV generated");

  // Step 2: Apply PKCS#7 padding to plaintext
  int plaintext_len = strlen(plaintext);
  int padded_len = ((plaintext_len / 16) + 1) * 16;  // Round up to multiple of 16
  uint8_t padded[padded_len];
  
  memcpy(padded, plaintext, plaintext_len);
  int padding_value = padded_len - plaintext_len;
  for (int i = plaintext_len; i < padded_len; i++) {
    padded[i] = padding_value;  // Pad with padding value itself
  }
  
  Serial.printf("Plaintext length: %d, Padded length: %d\n", plaintext_len, padded_len);

  // Step 3: Encrypt with AES-128-CBC
  esp_aes_context aes_ctx;
  esp_aes_init(&aes_ctx);
  esp_aes_setkey(&aes_ctx, aes_key, 128);  // 128-bit key
  
  uint8_t ciphertext[padded_len];
  esp_aes_crypt_cbc(&aes_ctx, ESP_AES_ENCRYPT, padded_len, iv, padded, ciphertext);
  esp_aes_free(&aes_ctx);
  
  Serial.println("AES-128-CBC encryption complete");

  // Step 4: Concatenate IV + Ciphertext in output buffer
  memcpy(encrypted, iv, 16);
  memcpy(encrypted + 16, ciphertext, padded_len);
  *encrypted_len = 16 + padded_len;
  
  Serial.printf("Encrypted data length: %d bytes\n", *encrypted_len);
}

/**
 * Compute HMAC-SHA256 for message authentication
 * 
 * @param data: Data to authenticate
 * @param data_len: Length of data
 * @param hmac_out: Output buffer for 32-byte HMAC
 */
void computeHMAC(const uint8_t* data, int data_len, uint8_t* hmac_out) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  
  mbedtls_md_hmac_starts(&ctx, hmac_key, 32);
  mbedtls_md_hmac_update(&ctx, data, data_len);
  mbedtls_md_hmac_finish(&ctx, hmac_out);
  mbedtls_md_free(&ctx);
  
  Serial.println("HMAC-SHA256 computed");
}

/**
 * Publish encrypted and authenticated LED brightness command
 * 
 * Payload structure: [IV (16B)][Ciphertext (variable)][HMAC (32B)]
 * 
 * @param brightness: LED brightness value (0-255)
 */
void publishEncryptedLED(int brightness) {
  // Create JSON plaintext message
  char plaintext[128];
  snprintf(plaintext, sizeof(plaintext),
    "{\"device\":\"TAB5\",\"brightness\":%d,\"timestamp\":%ld}",
    brightness, time(nullptr));
  
  Serial.printf("Plaintext message: %s\n", plaintext);

  // Encrypt message
  uint8_t encrypted[256];
  int encrypted_len;
  encryptMessage(plaintext, encrypted, &encrypted_len);

  // Compute HMAC of encrypted data
  uint8_t hmac[32];
  computeHMAC(encrypted, encrypted_len, hmac);

  // Build final payload: encrypted || hmac
  uint8_t payload[encrypted_len + 32];
  memcpy(payload, encrypted, encrypted_len);
  memcpy(payload + encrypted_len, hmac, 32);

  // Publish to MQTT with QoS 1 (At Least Once)
  if (mqttClient.publish_binary("led/control/encrypted", payload, encrypted_len + 32, true)) {
    Serial.printf("Published %d bytes (encrypted + HMAC + metadata)\n", encrypted_len + 32);
  } else {
    Serial.println("ERROR: Failed to publish message!");
  }
}

// ============================================================================
// MQTT MESSAGE CALLBACK
// ============================================================================

/**
 * Handle incoming MQTT messages
 * 
 * Expected message format: [IV (16B)][Ciphertext (variable)][HMAC (32B)]
 */
void messageCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("Received message on topic: %s\n", topic);
  Serial.printf("Message length: %d bytes\n", length);

  if (strcmp(topic, "led/control/encrypted") == 0) {
    // Minimum payload: 16 (IV) + 16 (min ciphertext) + 32 (HMAC) = 64 bytes
    if (length < 64) {
      Serial.println("ERROR: Message too short!");
      return;
    }

    // Step 1: Extract HMAC (last 32 bytes)
    uint8_t received_hmac[32];
    memcpy(received_hmac, payload + length - 32, 32);

    // Step 2: Verify HMAC of encrypted portion
    int encrypted_len = length - 32;
    uint8_t computed_hmac[32];
    computeHMAC(payload, encrypted_len, computed_hmac);

    // Constant-time comparison (prevent timing attacks)
    bool hmac_valid = true;
    for (int i = 0; i < 32; i++) {
      if (received_hmac[i] != computed_hmac[i]) {
        hmac_valid = false;
      }
    }

    if (!hmac_valid) {
      Serial.println("ERROR: HMAC verification FAILED! Message rejected (tampering detected).");
      M5.Display.setCursor(10, 150);
      M5.Display.setTextColor(RED, BLACK);
      M5.Display.println("HMAC FAILED!");
      M5.Display.setTextColor(WHITE, BLACK);
      return;
    }

    Serial.println("✓ HMAC verification successful!");

    // Step 3: Extract IV and ciphertext
    uint8_t iv[16];
    memcpy(iv, payload, 16);

    uint8_t ciphertext[encrypted_len - 16];
    memcpy(ciphertext, payload + 16, encrypted_len - 16);

    // Step 4: Decrypt with AES-128-CBC
    esp_aes_context aes_ctx;
    esp_aes_init(&aes_ctx);
    esp_aes_setkey(&aes_ctx, aes_key, 128);

    uint8_t plaintext[encrypted_len - 16];
    esp_aes_crypt_cbc(&aes_ctx, ESP_AES_DECRYPT, encrypted_len - 16,
                      iv, ciphertext, plaintext);
    esp_aes_free(&aes_ctx);

    Serial.println("✓ AES-128-CBC decryption complete");

    // Step 5: Remove PKCS#7 padding
    int padding = plaintext[(encrypted_len - 16) - 1];
    if (padding > 16 || padding == 0) {
      Serial.println("ERROR: Invalid padding!");
      return;
    }

    char decrypted[256];
    int decrypted_len = (encrypted_len - 16) - padding;
    strncpy(decrypted, (char*)plaintext, decrypted_len);
    decrypted[decrypted_len] = '\0';

    Serial.printf("✓ Decrypted message: %s\n", decrypted);

    // Step 6: Parse JSON and extract brightness
    // Simple parsing: find "brightness":" and extract number
    const char* brightness_start = strstr(decrypted, "\"brightness\":");
    if (brightness_start != nullptr) {
      brightness_start = strchr(brightness_start, ':');
      if (brightness_start != nullptr) {
        int brightness = atoi(brightness_start + 1);
        luminosite = constrain(brightness, 0, 255);
        setLedValue(luminosite);
        drawSlider(luminosite);
        
        Serial.printf("✓ LED brightness set to %d\n", luminosite);
        M5.Display.setCursor(10, 150);
        M5.Display.setTextColor(GREEN, BLACK);
        M5.Display.printf("✓ Message OK  ");
        M5.Display.setTextColor(WHITE, BLACK);
      }
    }
  }
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  // Check MQTT connection and reconnect if needed
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  // Update M5Stack touch input
  M5.update();
  auto t = M5.Touch.getDetail();

  // Handle touchscreen slider
  if (t.isPressed()) {
    int touchX = t.x;
    if (touchX < sliderX) touchX = sliderX;
    if (touchX > sliderX + Slargeur) touchX = sliderX + Slargeur;

    luminosite = map(touchX, sliderX, sliderX + Slargeur, 0, 255);
    setLedValue(luminosite);
    drawSlider(luminosite);

    // Publish encrypted brightness command
    publishEncryptedLED(luminosite);
  }

  delay(20);
}

// ============================================================================
// END OF CODE
// ============================================================================
