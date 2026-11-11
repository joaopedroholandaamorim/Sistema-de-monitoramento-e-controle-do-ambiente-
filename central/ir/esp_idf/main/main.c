#include "ir_service.h"
#include "console.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include <stdio.h>
/* Console UART0 para ESP32-WROOM-32 com ponte USB/serial. */
static void console_task(void *unused) {
    (void)unused;
    uint8_t bytes[64];
    for (;;) {
        int n = uart_read_bytes(UART_NUM_0, bytes, sizeof bytes, pdMS_TO_TICKS(20));
        for (int i=0; i<n; ++i) ir_console_char((char)bytes[i]);
        vTaskDelay(1);
    }
}
void app_main(void) {
    if (!ir_service_start(27, 1)) { puts("Falha IR."); return; }
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_set_baudrate(UART_NUM_0, 115200));
    if (xTaskCreatePinnedToCore(console_task, "Console", 4096, NULL, 1, NULL, 0) != pdPASS) {
        puts("Falha console."); return;
    }
    puts("Pronto: ON OFF SWING 18..30, com nova linha.");
}
