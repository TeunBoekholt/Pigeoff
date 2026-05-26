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

#include "LoRaWANHandler.hpp"
#include <LoRaWan_APP.h>
#include <Wire.h>
#include <HT_SSD1306Wire.h>
#include <Preferences.h>
#include <alog.h>

#define PREFS_NAMESPACE "appconfig"
#define PREFS_MAGIC "magic"
#define PREFS_MAGIC_VALUE 0x19660309
#define PREFS_APP_EUI "appEui"
#define PREFS_DEV_EUI "devEui"
#define PREFS_APP_KEY "appKey"

// extern SSD1306Wire display;

LoRaWANHandler loRaWANHandler;

// LORAWAN Settings /////////////////////////////////////////////////////////

uint8_t appEui[8] = {0x5A, 0x11, 0x52, 0xC4, 0xEE, 0x0E, 0x69, 0x1D};
uint8_t devEui[8] = {0x3E, 0x34, 0x93, 0xF7, 0x71, 0x70, 0xA4, 0x35};
uint8_t appKey[16] = {0xFC, 0x20, 0x29, 0x4F, 0x8F, 0xB6, 0x18, 0x3A, 0xFA, 0x07, 0x35, 0x13, 0x66, 0xDE, 0x09, 0xA3};
uint8_t nwkSKey[16];
uint8_t appSKey[16];
uint32_t devAddr = 0;
uint32_t appTxDutyCycle = 30000; // send every 30 seconds!
uint32_t sendDelay = 0;
uint16_t userChannelsMask[6] = {0x00FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};

LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;
DeviceClass_t loraWanClass = CLASS_A;

bool overTheAirActivation = false;
bool loraWanAdr = true;
bool isTxConfirmed = false;
uint8_t appPort = 2;
uint8_t confirmedNbTrials = 1;

unsigned long lastTxTime = 0;
const unsigned long txInterval = 30000; // Send data every 15 seconds
bool loraIsActive = false;

// LoRaWANHandler Class implementation //////////////////////////////////////

void LoRaWANHandler::printHex(char *label, uint8_t *buffer, int length)
{
  Serial.print(label);
  Serial.print(": ");
  for (int i = 0; i < length; i++)
  {
    Serial.printf("%02X", buffer[i]);
  }
  Serial.println();
}

void LoRaWANHandler::initConfig(bool showConfig)
{
  Preferences preferences;
  preferences.begin(PREFS_NAMESPACE, false);
  uint32_t magic = preferences.getUInt(PREFS_MAGIC, 0l);

  if (magic != PREFS_MAGIC_VALUE || reconfigure)  
  {
    magic = PREFS_MAGIC_VALUE;
    appTxDutyCycle = 30000;

    preferences.putUInt(PREFS_MAGIC, magic);
    preferences.putBytes(PREFS_APP_EUI, appEui, 8);
    preferences.putBytes(PREFS_DEV_EUI, devEui, 8);
    preferences.putBytes(PREFS_APP_KEY, appKey, 16);
  }
  else
  {
    preferences.getBytes(PREFS_APP_EUI, appEui, 8);
    preferences.getBytes(PREFS_DEV_EUI, devEui, 8);
    preferences.getBytes(PREFS_APP_KEY, appKey, 16);
  }
  preferences.end();

  if (showConfig)
  {
    ALOG_I("AppConfig loaded.");
    ALOG_NL();
    ALOG_D("Magic: %08x", magic);
    ALOG_D("duty cycle: %dms", appTxDutyCycle);
    ALOG_D("send delay: %dms", sendDelay);
    ALOG_NL();
    printHex((char *)"AppEUI/JoinEUI", appEui, 8);
    printHex((char *)"        DevEUI", devEui, 8);
    printHex((char *)"        AppKey", appKey, 16);
    ALOG_NL();
  }
}

void LoRaWANHandler::setup()
{
  pinMode(GPIO_NUM_0, INPUT_PULLUP);
  pinMode(Vext, OUTPUT);

  digitalWrite(Vext, LOW);
  Serial.begin(115200);
  reconfigure = false;
  resetReason = esp_reset_reason();

  if (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_EXT)
  {
    // LoRaWAN.displayMcuInit();
    delay(5000);
    Serial.println("\n\n\nLoRaWAN - Version " APP_VERSION );
    Serial.println("Build time : " __DATE__ " " __TIME__);
    Serial.printf("\nHELTEC board    : %d\n", HELTEC_BOARD);

    Serial.printf("SDK Version     : %s\n", ESP.getSdkVersion());
    Serial.printf("PIO Environment : %s\n", PIOENV);
    Serial.printf("PIO Platform    : %s\n", PIOPLATFORM);
    Serial.printf("PIO Framework   : %s\n", PIOFRAMEWORK);
    Serial.printf("Arduino Board   : %s\n", ARDUINO_BOARD);
    Serial.println();
  
    if ( digitalRead(GPIO_NUM_0) == LOW )
    {
      reconfigure = true;
    }
    initConfig(true);
  }
  else
  {
    initConfig(false);
  }

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
}

void LoRaWANHandler::loop() {
  switch(deviceState)
  {
    case DEVICE_STATE_INIT:
      LoRaWAN.init(loraWanClass, loraWanRegion);
      deviceState = DEVICE_STATE_JOIN;
      break;

    case DEVICE_STATE_JOIN:
      loraIsActive = true;
      LoRaWAN.join();
      break;

    case DEVICE_STATE_SEND:
      loraIsActive = true;
      prepareTxFrame(appPort);
      LoRaWAN.send();
      lastTxTime = millis(); // Record the exact time we sent a packet
      deviceState = DEVICE_STATE_CYCLE;
      break;

    case DEVICE_STATE_CYCLE:
      // Instead of letting Heltec handle the timer, we bypass it
      deviceState = DEVICE_STATE_SLEEP;
      break;

    case DEVICE_STATE_SLEEP:
      Mcu.timerhandler();
	    Radio.IrqProcess();
      if (loraIsActive && (millis() - lastTxTime >= 8000)) {
        loraIsActive = false;
      }
       if (millis() - lastTxTime >= txInterval) {
        deviceState = DEVICE_STATE_SEND;
      }
      break;
    default:
      deviceState = DEVICE_STATE_INIT;
      break;
  }
}

uint32_t LoRaWANHandler::getSleepTime()
{
  return appTxDutyCycle;
}

uint32_t LoRaWANHandler::getSendDelay()
{
  return sendDelay;
}

bool LoRaWANHandler::getLoraIsActive()
{
  return loraIsActive;
}
