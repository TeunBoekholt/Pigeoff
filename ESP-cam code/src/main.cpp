#include <Arduino.h>
#include "esp_camera.h"

#include <PigeOff_0.3_inferencing.h>

// --- AI Thinker ESP32-CAM Pinout ---
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// --- DIGITAL I/O PINS FOR HELTEC ---
#define TRIGGER_PIN GPIO_NUM_13  
#define RESPONSE_PIN 14 

// Global variable to hold camera frame
camera_fb_t *fb = NULL;

/**
 * @brief Initialize the camera with our specific ML requirements
 */
void setup_camera()
{
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;

    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = FRAMESIZE_96X96;
    config.jpeg_quality = 12;
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("ERR: Camera init failed (0x%x)\n", err);
    }
}

/**
 * @brief Preprocessing: Live Camera Frame to Binary Black & White
 */
int live_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    uint8_t *cam_pixels = fb->buf;

    for (size_t i = 0; i < length; i++)
    {
        uint8_t pixel = cam_pixels[offset + i];


        if (pixel < 160)
        {
            out_ptr[i] = 0.0f; // Black
        }
        else
        {
            out_ptr[i] = 16777215.0f; // White
        }
    }
    return 0;
}

void setup()
{
    Serial.begin(115200);

    // Configure Response Pin
    pinMode(RESPONSE_PIN, OUTPUT);
    digitalWrite(RESPONSE_PIN, LOW);

    // Arm the hardware trap for the NEXT wake cycle
    esp_sleep_enable_ext0_wakeup(TRIGGER_PIN, HIGH);

    Serial.println("\n--- Waking Up! Running Pigeon AI... ---");

    setup_camera();

    // Give the sensor power, then flush 3 frames to let Auto-Exposure stabilize
    delay(500); 
    
    for (int i = 0; i < 3; i++) {
        fb = esp_camera_fb_get();
        if(fb) esp_camera_fb_return(fb);
        delay(200); // Give the sensor a moment between garbage frames
    }

    // Take the REAL picture
    fb = esp_camera_fb_get();

    if (!fb)
    {
        Serial.println("ERR: Camera capture failed!");
        // We skip inference if capture fails, but still go back to sleep
    }
    else
    {
        // Setup Edge Impulse pipeline
        signal_t features_signal;
        features_signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
        features_signal.get_data = &live_camera_get_data;

        // Run Inference
        ei_impulse_result_t result = {0};
        EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);

        if (res == 0)
        {
                // Evaluate results
            bool pigeonDetected = false;
            for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
            {
                // we print the confidence score to the serial monitor for debugging
                Serial.printf("  %s score: %.5f\n", result.classification[i].label, result.classification[i].value);

                if (strcmp(result.classification[i].label, "pigeon") == 0)
                {
                    if (result.classification[i].value > 0.6f)
                    {
                        pigeonDetected = true;
                    }
                }
            }

            // Send the signal to the Heltec
            if (pigeonDetected)
            {
                Serial.println("Verdict: PIGEON! Setting Response HIGH.");
                digitalWrite(RESPONSE_PIN, HIGH);
                delay(1000); // Hold HIGH for 1 second so the Heltec has time to read it
                digitalWrite(RESPONSE_PIN, LOW); // Reset it
            }
            else
            {
                Serial.println("Verdict: Clear. Response remains LOW.");
            }
        }
        else
        {
            Serial.printf("ERR: Classifier failed (%d)\n", res);
        }

        //Free camera memory
        esp_camera_fb_return(fb);
    }

    Serial.println("Shutting down...");
    
    digitalWrite(RESPONSE_PIN, LOW); 
    
    esp_deep_sleep_start(); 
}

void loop()
{
    // Deep sleep acts like a reboot. This loop will literally never be reache!
}
