#include "ir_wave.h"
#include "PWM_utils.h"
static ir_pulse_t pulses[IR_MAX_HALVES];
static size_t used;
static bool overflow;
void ir_wave_begin(void) { used = 0; overflow = false; }
static void append(uint8_t level, uint32_t us) {
    while (us && !overflow) {
        if (used && pulses[used-1].level == level && pulses[used-1].us < 32767) {
            uint32_t room = 32767 - pulses[used-1].us;
            uint32_t part = us < room ? us : room;
            pulses[used-1].us += part; us -= part;
        } else {
            if (used == IR_MAX_HALVES) { overflow = true; break; }
            uint16_t part = us > 32767 ? 32767 : (uint16_t)us;
            pulses[used++] = (ir_pulse_t){part, level}; us -= part;
        }
    }
}
void ir_mark(uint32_t us) { append(1, us); }
void ir_space(uint32_t us) { append(0, us); }
bool ir_wave_end(const ir_pulse_t **out, size_t *count) {
    *out = pulses; *count = used;
    return !overflow && used > 0;
}
