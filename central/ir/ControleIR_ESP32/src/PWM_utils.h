#ifndef PWM_UTILS_H_
#define PWM_UTILS_H_
#include <stdint.h>
/* Contrato preservado: marca = portadora ligada; espaço = saída desligada.
 * Agora apenas registra pulsos; uso exclusivo da tarefa IR entre begin/end. */
void ir_mark(uint32_t duration_us);
void ir_space(uint32_t duration_us);
#endif
