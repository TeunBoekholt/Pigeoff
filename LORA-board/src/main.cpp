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

#define TRIGGER_PIN 2
#define PIN_INCOMING_TRIGGER 3
#define THRESHOLD 10
#define LIDAR_SAMPLING_RATE 1000
#define DEBOUNCE_TIME 5000 // How long to wait between 2 triggers
#define LIDAR_DEFAULT 100
#define SUSTAINED_TIME_MS 10000

#define RX_PIN 4 // Connect to TFmini-S TX
#define TX_PIN 5 // Connect to TFmini-S RX 


void IRAM_ATTR handleTriggerISR() {
    Serial.print("Interrupt detected!");
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
    // Image sending is not supported.
  }
}

QueueHandle_t radarQueue = NULL;
TaskHandle_t radarReaderTaskHandle = NULL;
TaskHandle_t radarProcessingTaskHandle = NULL;

void radarReaderTask(void * parameter) {
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  uint8_t frame[9];

  for(;;) {
    // Pull packets out of the serial buffer
    while (Serial2.available() >= 9) {
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

          // Push the measurement onto the queue. 
          // Wait up to 0 ticks if the queue is full (overwrite/drop old data)
          xQueueSend(radarQueue, &distanceCm, 0);
        }
      }
    }
    // Quick pause to prevent starving the CPU core
    vTaskDelay(pdMS_TO_TICKS(LIDAR_SAMPLING_RATE)); 
  }
}

void radarProcessingTask(void * parameter) {
  int receivedDistanceCm = LIDAR_DEFAULT;
  unsigned long disruptionDurationMs = 0;
  boolean currentlyTriggered = false;

  for(;;) {
    // xQueueReceive blocks automatically until data arrives in the queue.
    // Checking every LOOP_DELAY_MS match our original step timing window.
    if (xQueueReceive(radarQueue, &receivedDistanceCm, pdMS_TO_TICKS(LIDAR_SAMPLING_RATE)) == pdTRUE) {
      
      boolean isDisrupted = (receivedDistanceCm < (LIDAR_DEFAULT - THRESHOLD));

      if (isDisrupted) {
        disruptionDurationMs += LIDAR_SAMPLING_RATE;
        
        if (disruptionDurationMs >= SUSTAINED_TIME_MS && !currentlyTriggered) {
          currentlyTriggered = true;
          
          // CRITICAL STEP: Suspend the hardware reader task instantly
          vTaskSuspend(radarReaderTaskHandle);
          Serial.println(">>> RADAR TASK SUSPENDED <<<");
          
          // Fire physical trigger
          digitalWrite(TRIGGER_PIN, HIGH);
          Serial.println(">>> TRIGGER ACTIVATED: Sustained disruption for 10s! <<<");
          
          // ----------------================---------------------------------
          // EXECUTE YOUR EXTERNAL LOOP TRIGGER HERE
          // Example: Wait for Camera task to finish, transmit data, etc.
          // For demonstration, simulating an execution delay:
          vTaskDelay(pdMS_TO_TICKS(5000)); // Simulating a 5-second capture cycle
          // ----------------================---------------------------------

          // Reset status values after the action loop completes
          digitalWrite(TRIGGER_PIN, LOW);
          currentlyTriggered = false;
          disruptionDurationMs = 0;
          Serial.println(">>> TRIGGER CLEARED: Action loop complete. <<<");

          // Clear out stale data that piled up in serial buffers before resuming
          while(Serial2.available() > 0) { Serial2.read(); } 
          xQueueReset(radarQueue); 

          // Resume the radar reader task safely
          vTaskResume(radarReaderTaskHandle);
          Serial.println(">>> RADAR TASK RESUMED <<<");
        }
      } else {
        // Clear the timer if reading returns to normal before hitting 10s
        if (disruptionDurationMs > 0) {
          disruptionDurationMs = 0;
        }
      }

      // Output to Plotter
      Serial.print("Distance_cm:");
      Serial.print(receivedDistanceCm);
      Serial.print(",Disruption_Timer_ms:");
      Serial.println(disruptionDurationMs);
    }
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

  radarQueue = xQueueCreate(10, sizeof(int));

  if (radarQueue != NULL) {
    // 1. Create the Hardware Reading Task (Core 0)
    xTaskCreatePinnedToCore(
      radarReaderTask,
      "RadarReader",
      3072,
      NULL,
      2, // Slightly higher priority to ensure serial data is captured without drops
      &radarReaderTaskHandle,
      0
    );

    // 2. Create the Data Processing Task (Core 1)
    xTaskCreatePinnedToCore(
      radarProcessingTask,
      "RadarProcessing",
      3072,
      NULL,
      1, // Normal priority
      &radarProcessingTaskHandle,
      1
    );
    
    Serial.println("Both FreeRTOS Radar Tasks Initialized.");
  } else {
    Serial.println("Error creating the Radar Queue!");
  }
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
  loRaWANHandler.loop();

  if (g_event_triggered) {
        ALOG_I("External hardware trigger detected! Initiating LoRa Uplink...");
        
        // Reset the flag
        g_event_triggered = false;

        // Force a LoRaWAN transmission
        prepareTxFrame(1); // Prepare the frame on the desired port (e.g., 1)
    }
}
