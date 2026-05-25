#include <Arduino.h>
#include "esp_camera.h"

// 1. Edge Impulse library name
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
// NOTE: For Deep Sleep wake-up, the Trigger Pin must be an RTC-capable GPIO.
// GPIO 13 is perfect for this!
#define TRIGGER_PIN GPIO_NUM_13
#define RESPONSE_PIN 14

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

        if (pixel < 127)
        {
            out_ptr[i] = 0.0f;
        }
        else
        {
            out_ptr[i] = 16777215.0f;
        }
    }
    return 0;
}

void setup()
{
    Serial.begin(115200);

    // 1. Configure the RESPONSE output pin immediately
    pinMode(RESPONSE_PIN, OUTPUT);
    digitalWrite(RESPONSE_PIN, LOW); // Default to LOW

    // 2. Tell the ESP32 to wake up NEXT time GPIO 13 gets pulled HIGH
    esp_sleep_enable_ext0_wakeup(TRIGGER_PIN, 1);

    Serial.println("\n--- Waking up! Running AI... ---");

    // 3. Initialize Camera
    setup_camera();

    // CRITICAL FIX FOR COLD BOOT:
    // The camera sensor needs time to adjust auto-exposure and white balance.
    delay(500);

    // Clear a "throwaway" frame out of the buffer to prevent a black/green image
    fb = esp_camera_fb_get();
    if (fb)
        esp_camera_fb_return(fb);
    delay(50);

    // 4. Take the real picture
    fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("ERR: Camera capture failed!");
        // We will just let it go to sleep and try again next time
    }
    else
    {
        // 5. Setup Edge Impulse pipeline
        signal_t features_signal;
        features_signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
        features_signal.get_data = &live_camera_get_data;

        // 6. Run Inference
        ei_impulse_result_t result = {0};
        EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);

        if (res == 0)
        {
            // 7. Evaluate the results
            bool pigeonDetected = false;
            for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
            {
                if (strcmp(result.classification[i].label, "pigeon") == 0)
                {
                    if (result.classification[i].value > 0.6f)
                    {
                        pigeonDetected = true;
                    }
                }
            }

            // 8. Set the output pin HIGH or LOW based on the verdict
            if (pigeonDetected)
            {
                Serial.println("Verdict: PIGEON! Setting Response HIGH.");
                digitalWrite(RESPONSE_PIN, HIGH);
            }
            else
            {
                Serial.println("Verdict: Clear. Response remains LOW.");
                digitalWrite(RESPONSE_PIN, LOW);
            }
        }

        // 9. Free camera memory
        esp_camera_fb_return(fb);
    }

    // 10. HOLD THE SIGNAL SO HELTEC CAN READ IT
    // The Heltec needs time to realize the inference is done and read pin 14.
    // We hold the result for 2 seconds before cutting our own power.
    delay(2000);

    // 11. GO BACK TO SLEEP
    Serial.println("Shutting down...");

    // Ensure the pin goes back LOW so we don't accidentally leave it HIGH
    digitalWrite(RESPONSE_PIN, LOW);

    esp_deep_sleep_start();
}

void loop()
{
    // The board sleeps at the end of setup(), so loop() is completely ignored.
}
