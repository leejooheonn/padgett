#include <Wire.h>
#include <Adafruit_APDS9960.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEBeacon.h>

// Create the sensor object
Adafruit_APDS9960 apds;

// Proximity variables (0 = clear, 255 = maximum reflection/blocked)
const int proximityThreshold = 150; // Adjust based on pill size/distance from sensor
const int pillCooldownMs = 300;     // Time window to avoid double-counting a single pill

int pillCount = 0;
bool systemInPeak = false;
unsigned long lastPillTime = 0;

bool sensorOK = false; // Tracks whether the APDS9960 initialized successfully

BLEAdvertising *pAdvertising;

void updateBluetoothBroadcast(const String &statusPayload) {
  BLEAdvertisementData oAdvertisementData;
  oAdvertisementData.setFlags(0x04); // BR_EDR_NOT_SUPPORTED
  oAdvertisementData.setManufacturerData(statusPayload); // This core's API expects Arduino String, not std::string

  // Only swap the payload — advertising keeps running continuously,
  // avoiding the brief stop/start visibility gap of calling start() again.
  pAdvertising->setAdvertisementData(oAdvertisementData);

  Serial.print("Bluetooth updated. Broadcasting: ");
  Serial.println(statusPayload);
}

void updatePillCountBroadcast(int currentCount) {
  String payload = "COUNT:" + String(currentCount);
  updateBluetoothBroadcast(payload);
}

void setup() {
  Serial.begin(115200);

  // Initialize the I2C protocol
  Wire.begin();

  // Give the sensor a moment to power up and stabilize before the first
  // begin() attempt — helps avoid spurious failures right after boot.
  delay(200);

  // Initialize the APDS9960 sensor, with retries instead of a hard halt
  for (int attempt = 0; attempt < 5 && !sensorOK; attempt++) {
    sensorOK = apds.begin();
    if (!sensorOK) {
      Serial.print("APDS9960 init failed (attempt ");
      Serial.print(attempt + 1);
      Serial.println("/5), retrying...");
      delay(500);
    }
  }

  // Ready the Bluetooth broadcasting configuration regardless of sensor state,
  // so the device can still report a fault status over BLE.
  BLEDevice::init("XIAO_PILL_ALERT");
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start(); // Start advertising once; updates happen via payload swaps only

  if (!sensorOK) {
    Serial.println("APDS9960 not found after retries. Running in fault mode.");
    updateBluetoothBroadcast("ERR:NO_SENSOR");
  } else {
    apds.enableProximity(true);
    Serial.println("System initialized! Scanning until cap is replaced...");
    updatePillCountBroadcast(pillCount); // Broadcast initial count of 0
  }
}

void loop() {
  // If the sensor never came up, stay alive in a degraded fault state
  // instead of hanging, so BLE fault status remains visible.
  if (!sensorOK) {
    delay(1000);
    return;
  }

  // Read the digital proximity byte (value from 0 to 255)
  uint8_t proximityData = apds.readProximity();
  unsigned long currentTime = millis();

  // 1. Detect a pill passing closely in front of the sensor face
  if (proximityData > proximityThreshold && !systemInPeak && (currentTime - lastPillTime > pillCooldownMs)) {
    pillCount++;
    systemInPeak = true;
    lastPillTime = currentTime;

    Serial.print("Pill block detected! Proximity Value: ");
    Serial.print(proximityData);
    Serial.print(" | Total Count: ");
    Serial.println(pillCount);

    // 2. Instantly update the phone broadcast with the running count
    updatePillCountBroadcast(pillCount);
  }

  // 3. Reset state once proximity falls back down below the threshold
  else if (proximityData < (proximityThreshold - 30) && systemInPeak) {
    systemInPeak = false;
  }

  delay(10); // Small pause to prevent I2C bus congestion
}
