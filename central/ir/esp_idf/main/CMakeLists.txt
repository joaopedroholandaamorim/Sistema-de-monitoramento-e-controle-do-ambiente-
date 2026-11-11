set(IR_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../ControleIR_ESP32/src")
idf_component_register(
    SRCS "main.c" "${IR_SOURCE}/ir.c" "${IR_SOURCE}/ir_wave.c"
         "${IR_SOURCE}/ir_backend.c" "${IR_SOURCE}/ir_service.c"
         "${IR_SOURCE}/console.c"
    INCLUDE_DIRS "${IR_SOURCE}"
    REQUIRES esp_driver_rmt esp_driver_uart freertos
)
