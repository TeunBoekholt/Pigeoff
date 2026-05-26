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
#include "LoRaWANHandler.hpp"
#include <LoRaWan_APP.h>
#include <Wire.h>
#include <HT_SSD1306Wire.h>
#include <Preferences.h>
#include <alog.h>

bool testMode = false; 
volatile float pidgeonCounter = 0.0;
volatile float latestRadarValue = 0.0;
volatile bool g_event_triggered = false;
volatile uint32_t last_interrupt_time = 0;

TimerHandle_t fakeInterruptTimer;
QueueHandle_t radarQueue = NULL;
TaskHandle_t radarReaderTaskHandle = NULL;
TaskHandle_t radarProcessingTaskHandle = NULL;

extern SSD1306Wire display;

#define LORA_SEND_INTERVAL_MS 600000 // 10 minute
#define TRIGGER_PIN 2
#define PIN_INCOMING_TRIGGER 3
#define THRESHOLD 10 
#define LIDAR_SAMPLING_RATE 1000
#define DEBOUNCE_TIME 1000 // How long to wait between 2 triggers
#define LIDAR_DEFAULT 80
#define SUSTAINED_TIME_MS 5000

#define RX_PIN 4 // Connect to TFmini-S TX
#define TX_PIN 5 // Connect to TFmini-S RX 


/**
 * @brief Artificial interrupt callback for testing without hardware.
 *
 * @param xTimer the timer handle.
 */
void fakeInterruptCallback(TimerHandle_t xTimer) {
  // Serial.println("Software Timer Fired! Executing trigger code...");
  g_event_triggered = true;
  // pidgeonCounter+= 1.0;
}

/**
 * @brief Code to execute when the Esp32-CAM sends an interrupt signal. (e.g. when it detects a pidgeon)
 */
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
 * @param port The port number on which to send the data.
 */
void prepareTxFrame(uint8_t port)
{
  // e.g. warmup delay for the sensor
  if ( loRaWANHandler.getSendDelay() > 0 )
  {
    ALOG_D("Send delay: %dms", loRaWANHandler.getSendDelay());
    delay(loRaWANHandler.getSendDelay());
  }

  uint8_t pidgeonCount = (uint8_t)pidgeonCounter;
  pidgeonCounter = 0.0; // reset counter after reading
  ALOG_D("Preparing LoRa frame with count: %d", pidgeonCount);
  appDataSize = 4;
  appData[0] = 0xA5; // preamble
  appData[1] = 0x01; // status
  appData[2] = pidgeonCount; // pidgeon count code
  appData[3] = crc8_le(0, appData, appDataSize - 1); // crc 8 LE
}

/**
 * @brief The task on Core 0 that continuously reads from the radar sensor and writes it to a queue.
 * @param parameter 
 */
void radarReaderTask(void * parameter) {
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  uint8_t frame[9];

  for(;;) {
    // Clear stale data and avoid processing old packets!
   while (Serial2.available() > 27) { 
      Serial2.read(); 
    }

    boolean packetFound = false;
    while (Serial2.available() >= 9 && !packetFound) {
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

          xQueueSend(radarQueue, &distanceCm, 0);
          
          packetFound = true; // Break the while loop so we don't process more packets right now
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(LIDAR_SAMPLING_RATE)); 
  }
}

/**
 * @brief Artificial radar task for testing without hardware.
 *
 * @param xTimer the timer handle.
 */
void artificialRadarTask(void * parameter) {
  for(;;) {
    // Simulate a random distance measurement between 50 and 100 cm
    int simulatedDistance = random(50, 100);
    xQueueSend(radarQueue, &simulatedDistance, 0);
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
          vTaskDelay(pdMS_TO_TICKS(100));
          digitalWrite(TRIGGER_PIN, LOW);
          // ----------------================---------------------------------
          // EXECUTE YOUR EXTERNAL LOOP TRIGGER HERE
          // Example: Wait for Camera task to finish, transmit data, etc.
          // For demonstration, simulating an execution delay:
          vTaskDelay(pdMS_TO_TICKS(6000)); // Simulating a 5-second capture cycle
          // ----------------================---------------------------------

          // Reset status values after the action loop completes
          currentlyTriggered = false;
          disruptionDurationMs = 0;
          Serial.println(">>> TRIGGER CLEARED: Action loop complete. <<<");

          // Clear out stale data that piled up in serial buffers before resuming
          // while(Serial2.available() > 0) { Serial2.read(); } 
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
    if (!loRaWANHandler.getLoraIsActive()) {
      // If LoRaWAN is not active, we can afford to sleep more aggressively.
      // This is a good place to put the device into a light sleep mode to save power.
      // Force the internal RTC timer to wake us up in exactly LIDAR_SAMPLING_RATE MS
      // (Macro requires microseconds: 1 ms = 1000 us)
      esp_sleep_enable_timer_wakeup(LIDAR_SAMPLING_RATE * 950ULL);
      // Serial.flush(); // Cleanly flush logs out of the serial bus before power gates drop

      // // Command the hardware layer into a low power light sleep state immediately.
      // // The CPU freezes here. RAM stays perfectly intact. 
      esp_light_sleep_start();
    }
  }
}

void loraTask(void * parameter) {
  for(;;) {
    loRaWANHandler.loop();
    vTaskDelay(pdMS_TO_TICKS(100)); // Adjust this delay as needed to balance responsiveness and CPU usage
  }
}


/**
 * @brief Initializes the LoRaWAN handler.
 *
 * This function sets up the LoRaWAN handler by invoking the setup method
 * of the loRaWANHander object during the initialization phase.
 */
void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(PIN_INCOMING_TRIGGER, INPUT_PULLDOWN);
  Serial.println("Initializing LoRaWAN Handler...");
  loRaWANHandler.setup();
  
  attachInterrupt(digitalPinToInterrupt(PIN_INCOMING_TRIGGER), handleTriggerISR, RISING);

  radarQueue = xQueueCreate(10, sizeof(int));

  if (testMode) {
      Serial.println("Test mode enabled: Starting artificial radar task...");
      fakeInterruptTimer = xTimerCreate(
      "4s_Timer",              
      pdMS_TO_TICKS(4000),    
      pdTRUE,
      (void *)0,                
      fakeInterruptCallback     
    );

    // Start the timer
    if (fakeInterruptTimer != NULL) {
      xTimerStart(fakeInterruptTimer, 0);
      Serial.println("4-second background timer started successfully.");
  }
  }

  if (radarQueue != NULL) {
    if (testMode) {
      Serial.println("Creating Artificial Radar Task...");
      xTaskCreatePinnedToCore(
        artificialRadarTask,
        "ArtificialRadar",
        3072,
        NULL,
        2, // Slightly higher priority to ensure serial data is captured without drops
        &radarReaderTaskHandle,
        0
      );
    } else {
      Serial.println("Creating Radar Reader Task...");
      xTaskCreatePinnedToCore(
        radarReaderTask,
        "RadarReader",
        3072,
        NULL,
        2, // Slightly higher priority to ensure serial data is captured without drops
        &radarReaderTaskHandle,
        0
      );
    }

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

    xTaskCreatePinnedToCore(
      loraTask,
      "LoRaTask",
      4096,
      NULL,
      1, // Normal priority
      NULL,
      1
    );
    
    Serial.println("Both FreeRTOS Radar Tasks Initialized.");
  } else {
    Serial.println("Error creating the Radar Queue!");
  }
}

  
/**
 * @brief Continuously handles interrupt events. Other tasks are pinned to specific cores. 
 */
void loop()
{
  if (g_event_triggered) {
        ALOG_I("External hardware trigger detected! PIGEON DETECTED!...");
        // Reset the flag
        g_event_triggered = false;
        pidgeonCounter+= 1.0; 
  }
  vTaskDelay(pdMS_TO_TICKS(100));
}
