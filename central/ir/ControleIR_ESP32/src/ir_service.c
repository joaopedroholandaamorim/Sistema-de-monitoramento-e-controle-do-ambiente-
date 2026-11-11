#include "ir_service.h"
#include "IR_utils.h"
#include "ir_backend.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>
#define READY (1U << 0)
#define FAILED (1U << 1)
typedef struct { uint32_t code; uint8_t length; uint8_t data[28]; } request_t;
static QueueHandle_t queue;
static EventGroupHandle_t state;
static int output_gpio;
static bool started;
static void ir_task(void *unused) {
    (void)unused;
    esp_err_t e = ir_backend_init(output_gpio);
    xEventGroupSetBits(state, e == ESP_OK ? READY : FAILED);
    if (e != ESP_OK) { printf("IR init: %s\n", esp_err_to_name(e)); vTaskDelete(NULL); return; }
    for (;;) {
        request_t req;
        if (xQueueReceive(queue, &req, portMAX_DELAY) != pdTRUE) continue;
        if (!ir_service_ready()) { puts("IR em falha: pedido descartado."); continue; }
        ir_wave_begin();
        if (req.length) ir_send_hitachi_ac(req.data, req.length, 0);
        else ir_send_coolix(req.code, nBitsCoolix, NREPEAT);
        const ir_pulse_t *pulses; size_t count;
        if (!ir_wave_end(&pulses, &count)) { puts("IR: mensagem excede buffer; nada emitido."); continue; }
        e = ir_backend_send(pulses, count);
        if (e != ESP_OK) {
            xEventGroupClearBits(state, READY);
            xEventGroupSetBits(state, FAILED);
            printf("IR falhou: %s. Reinicie.\n", esp_err_to_name(e));
        } else printf("IR emitido no core %d; sem ACK do aparelho.\n", xPortGetCoreID());
        /* Intervalo mínimo entre pedidos (inclusive Hitachi). O gap entre
         * repetições COOLIX continua dentro da sequência de hardware. */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
bool ir_service_start(int gpio, int core) {
    if (started || core < 0 || core > 1) return false;
    started = true; output_gpio = gpio;
    queue = xQueueCreate(8, sizeof(request_t));
    state = xEventGroupCreate();
    if (!queue || !state) return false;
    if (xTaskCreatePinnedToCore(ir_task, "IR", 4096, NULL, 3, NULL, core) != pdPASS) return false;
    EventBits_t bits = xEventGroupWaitBits(state, READY | FAILED, pdFALSE, pdFALSE, portMAX_DELAY);
    return (bits & READY) != 0;
}
bool ir_service_ready(void) { return state && (xEventGroupGetBits(state) & READY); }
static bool submit(const request_t *req) {
    return ir_service_ready() && xQueueSend(queue, req, 0) == pdTRUE;
}
bool ir_request_coolix(uint32_t code) {
    if (code > 0xFFFFFF) return false;
    request_t req = {.code = code}; return submit(&req);
}
bool ir_request_temperature(uint8_t t) {
    static const uint32_t codes[] = {
        TEMPERATURA18_COOLIX, TEMPERATURA19_COOLIX, TEMPERATURA20_COOLIX,
        TEMPERATURA21_COOLIX, TEMPERATURA22_COOLIX, TEMPERATURA23_COOLIX,
        TEMPERATURA24_COOLIX, TEMPERATURA25_COOLIX, TEMPERATURA26_COOLIX,
        TEMPERATURA27_COOLIX, TEMPERATURA28_COOLIX, TEMPERATURA29_COOLIX,
        TEMPERATURA30_COOLIX};
    return t >= 18 && t <= 30 && ir_request_coolix(codes[t-18]);
}
bool ir_request_hitachi(const uint8_t *data, size_t length) {
    if (!data || !length || length > 28) return false;
    request_t req = {.length = (uint8_t)length};
    memcpy(req.data, data, length); return submit(&req);
}
