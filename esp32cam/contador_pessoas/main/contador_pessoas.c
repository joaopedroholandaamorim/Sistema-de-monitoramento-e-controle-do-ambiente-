/**
 * Contador de pessoas com ESP32-CAM (AI-Thinker)
 *  - uma tarefa le a camera (grayscale QVGA), conta entradas/saidas e, se alguem
 *    estiver vendo a pagina, compartilha o quadro com o servidor web
 *  - pagina em http://IP/ mostra a imagem e quantas pessoas ha na sala
 */

#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_camera.h"

#include "contador.h"
#include "web_stream.h"

static const char *TAG = "contador";

// ---- Pinos da ESP32-CAM AI-Thinker ----
#define CAM_PIN_PWDN   32
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD   26
#define CAM_PIN_SIOC   27
#define CAM_PIN_D7     35
#define CAM_PIN_D6     34
#define CAM_PIN_D5     39
#define CAM_PIN_D4     36
#define CAM_PIN_D3     21
#define CAM_PIN_D2     19
#define CAM_PIN_D1     18
#define CAM_PIN_D0      5
#define CAM_PIN_VSYNC  25
#define CAM_PIN_HREF   23
#define CAM_PIN_PCLK   22

static camera_config_t camera_config = {
    .pin_pwdn     = CAM_PIN_PWDN,
    .pin_reset    = CAM_PIN_RESET,
    .pin_xclk     = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href  = CAM_PIN_HREF,
    .pin_pclk  = CAM_PIN_PCLK,

    .xclk_freq_hz = 20000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_GRAYSCALE,   // 1 byte por pixel
    .frame_size   = FRAMESIZE_QVGA,        // 320x240 (o contador espera exatamente isto)
    .jpeg_quality = 12,                    // ignorado em grayscale
    .fb_count     = 2,
    .fb_location  = CAMERA_FB_IN_PSRAM,
    .grab_mode    = CAMERA_GRAB_LATEST,    // sempre entrega o quadro mais recente
};

static esp_err_t init_camera(void)
{
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar a camera: %s (0x%x)", esp_err_to_name(err), err);
        return err;
    }
    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);
    if (s) {
        ESP_LOGI(TAG, "Sensor PID: 0x%x", s->id.PID);
    }
    return ESP_OK;
}

static const char *motivo_reset(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:  return "energia ligada";
        case ESP_RST_SW:       return "reinicio por software";
        case ESP_RST_PANIC:    return "panic (erro no codigo)";
        case ESP_RST_INT_WDT:  return "watchdog de interrupcao";
        case ESP_RST_TASK_WDT: return "watchdog de tarefa";
        case ESP_RST_WDT:      return "outro watchdog";
        case ESP_RST_BROWNOUT: return "BROWNOUT (tensao baixa)";
        default:               return "outro";
    }
}

// Unica tarefa que le a camera: conta e, se houver alguem vendo, publica o quadro.
static void tarefa_contagem(void *arg)
{
    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Falha na captura do quadro");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (fb->len == CT_SRC_W * CT_SRC_H) {
            contador_processar(fb->buf);
            web_stream_publicar(fb->buf, fb->len, fb->width, fb->height);
        } else {
            ESP_LOGE(TAG, "Quadro inesperado: %dx%d (%u bytes)", (int)fb->width, (int)fb->height,
                     (unsigned)fb->len);
        }

        esp_camera_fb_return(fb);   // sempre devolver o buffer
    }
}

void app_main(void)
{
    ESP_LOGW(TAG, "Motivo do ultimo reset: %s", motivo_reset(esp_reset_reason()));
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    if (init_camera() != ESP_OK) return;

    contador_init();
    web_stream_init();

    // A contagem comeca ja, mesmo sem Wi-Fi. Fica no nucleo 1; o Wi-Fi usa o nucleo 0.
    xTaskCreatePinnedToCore(tarefa_contagem, "contagem", 6144, NULL, 5, NULL, 1);

    wifi_init_sta();     // bloqueia ate obter IP
    start_webserver();
}
