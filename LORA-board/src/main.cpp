/*
 * Copyright 2025 Thorsten Ludewig (t.ludewig@gmail.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <Arduino.h>
#include <LoRaWANHandler.hpp>
#include <rom/crc.h>
#include <alog.h>

/*

Schedule downlink (FPort 1)
NOTE: if you want dowlink to put device to sleep, use this payload format and command:
Payload Type: Bytes
  Byte 0: 0x5A (magic number) 
   - Why 0x5A? 0x5A is a nice binary number, '0101 1010', 
     and it's easy to recognize.
  Byte 1: Command 
  Byte 2-5: Value

Commands:
  0x01: Set sleep time
  0x02: Set send delay
Example:
0x5A 0x01 0x00 0x01 0xD4 0xC0 -> Set sleep time to 120000ms (2 minutes)
*/

bool sendImage = false;
volatile float latestRadarValue = 0.0;
volatile bool g_event_triggered = false;
volatile uint32_t last_interrupt_time = 0;

#define TRIGGER_PIN 2           // Pin 2 is safe on Heltec V3
#define PIN_INCOMING_TRIGGER 3
#define THRESHOLD 10
#define LIDAR_DELAY 2000
#define DEBOUNCE_TIME 500        // Prevent double-triggers (ms)
#define LIDAR_DEFAULT 100

#define RX_PIN 4 // Connect to TFmini-S TX
#define TX_PIN 5 // Connect to TFmini-S RX (Optional, if only readin


void IRAM_ATTR handleTriggerISR() {
    uint32_t interrupt_time = millis();
    
    // Simple debounce logic to prevent noise from triggering multiple messages
    if (interrupt_time - last_interrupt_time > DEBOUNCE_TIME) {
        g_event_triggered = true;
    }
    last_interrupt_time = interrupt_time;
}

/**
 * @brief Prepares the transmission frame for LoRaWAN.
 *
 * This function sets up the application data to be sent over LoRaWAN.
 * If a send delay is configured, it applies the delay before preparing the frame.
 *
 * @param port The port number on which to send the data.
 */
// uint8_t appData[255]
void prepareTxFrame(uint8_t port)
{
  // e.g. warmup delay for the sensor
  if ( loRaWANHandler.getSendDelay() > 0 )
  {
    ALOG_D("Send delay: %dms", loRaWANHandler.getSendDelay());
    delay(loRaWANHandler.getSendDelay());
  }

  if (!sendImage) {
    uint8_t sentInt = (uint8_t)7; // TODO test value - to be replaced by sensor data
    appDataSize = 1;
    appData[0] = sentInt;
    }
  else {
    // Image sending is not supported.}
  }
}

void radarTask(void * parameter) {
  pinMode(TRIGGER_PIN, OUTPUT);
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.begin(115200);

  uint8_t frame[9]; 
  int packetsProcessed = 0;

  for(;;) {
    // Process serial data, but cap it to prevent starving the CPU
    // If there is continuous data, it will process up to 5 packets then yield
    while (Serial2.available() >= 9 && packetsProcessed < 5) {
      if (Serial2.read() == 0x59 && Serial2.peek() == 0x59) {
        frame[0] = 0x59;
        frame[1] = Serial2.read(); 
        
        for (int i = 2; i < 9; i++) {
          frame[i] = Serial2.read();
        }

        uint8_t checksum = 0;
        for (int i = 0; i < 8; i++) {
          checksum += frame[i];
        }

        if (checksum == frame[8]) {
          int distanceCm = frame[2] + (frame[3] << 8);
          latestRadarValue = (float)distanceCm;

        
          boolean trigger = (distanceCm < LIDAR_DEFAULT - THRESHOLD) || (distanceCm > LIDAR_DEFAULT + THRESHOLD); 
          if (trigger) {
            ALOG_I("Radar trigger! Distance: %d cm", distanceCm);
          }
          digitalWrite(TRIGGER_PIN, trigger ? HIGH : LOW);

          Serial.print("Distance_cm:");
          Serial.print(distanceCm);
          Serial.print(",Threshold:");
          Serial.println(THRESHOLD);
          
          packetsProcessed++; // Keep track of how much work we did this loop
        }
      } else {
        // If the byte wasn't a valid header, we still need to break an infinite loop 
        // in case the buffer is full of garbage data.
        packetsProcessed++; 
      }
    }

    // Reset our packet counter for the next cycle
    packetsProcessed = 0;

    // This vTaskDelay MUST be hit to feed the Watchdog and let FreeRTOS breathe!
    vTaskDelay(pdMS_TO_TICKS(LIDAR_DELAY)); 
  }
}
/**
 * @brief Initializes the LoRaWAN handler.
 *
 * This function sets up the LoRaWAN handler by invoking the setup method
 * of the loRaWANHander object during the initialization phase.
 */
void setup() {
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(PIN_INCOMING_TRIGGER, INPUT_PULLDOWN);
  Serial.println("Initializing LoRaWAN Handler...");
  loRaWANHandler.setup();
  
  attachInterrupt(digitalPinToInterrupt(PIN_INCOMING_TRIGGER), handleTriggerISR, RISING);

  // Create the task
  Serial.println("Starting Radar Task...");
  xTaskCreatePinnedToCore(
    radarTask,        // Function name
    "RadarTask",      // Name for debugging
    2048,             // Stack size (bytes)
    NULL,             // Parameter to pass
    1,                // Priority (1 is low)
    NULL,             // Task handle
    0                 // Core ID (0 or 1)
  );
}
  

/**
 * @brief Continuously handles LoRaWAN events and maintains the connection.
 *
 * This function is repeatedly called in the main loop and delegates
 * processing to the LoRaWAN handler's loop method. It ensures that
 * LoRaWAN events are processed and the connection remains active.
 */
void loop()
{
  // loRaWANHandler.loop();

  if (g_event_triggered) {
        ALOG_I("External hardware trigger detected! Initiating LoRa Uplink...");
        
        // Reset the flag
        g_event_triggered = false;

        // Force a LoRaWAN transmission
        // Note: The specific function name depends on your LoRaWAN library
        // usually loRaWANHandler.send() or similar.
        prepareTxFrame(1); // Prepare the frame on the desired port (e.g., 1)
    }
}
