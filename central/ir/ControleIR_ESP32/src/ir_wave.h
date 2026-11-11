#ifndef IR_WAVE_H
#define IR_WAVE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define IR_MAX_SYMBOLS 256
#define IR_MAX_HALVES ((IR_MAX_SYMBOLS - 1) * 2)
typedef struct { uint16_t us; uint8_t level; } ir_pulse_t;
void ir_wave_begin(void);
bool ir_wave_end(const ir_pulse_t **pulses, size_t *count);
#endif
