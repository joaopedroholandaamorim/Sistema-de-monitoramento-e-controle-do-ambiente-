#ifndef IR_SERVICE_H
#define IR_SERVICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Inicializar uma única vez, no boot. Não chamar APIs de ISR. */
bool ir_service_start(int gpio, int core);
bool ir_service_ready(void);
bool ir_request_coolix(uint32_t code);
bool ir_request_temperature(uint8_t celsius);
/* Payload copiado para a fila; não guarda o ponteiro do chamador.
 * Até 28 bytes, sem repetição extra. Checksum/estado são do chamador. */
bool ir_request_hitachi(const uint8_t *data, size_t length);
#ifdef __cplusplus
}
#endif
#endif
