#include <Arduino.h>
#include "src/ir_service.h"
#include "src/console.h"
static void consoleTask(void *) {
    for (;;) {
        for (unsigned i=0; i<64 && Serial.available(); ++i)
            ir_console_char(static_cast<char>(Serial.read()));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void setup() {
    Serial.begin(115200);
    if (!ir_service_start(27, 1)) { Serial.println("Falha IR."); return; }
    if (xTaskCreatePinnedToCore(consoleTask, "Console", 4096, nullptr, 1, nullptr, 0) != pdPASS) {
        Serial.println("Falha console."); return;
    }
    Serial.println("Pronto: ON OFF SWING 18..30, com nova linha.");
}
void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
