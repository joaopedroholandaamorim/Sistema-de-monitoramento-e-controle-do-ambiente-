#pragma once
#include "esp_err.h"
#include "ir_wave.h"
esp_err_t ir_backend_init(int gpio);
esp_err_t ir_backend_send(const ir_pulse_t *pulses, size_t count);
