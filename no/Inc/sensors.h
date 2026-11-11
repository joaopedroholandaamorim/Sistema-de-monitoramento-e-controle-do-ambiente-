#ifndef SENSORS_H
#define SENSORS_H

#include "stm32f1xx.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature;
    float humidity;
} AHT_Data_t;

typedef enum {
    SENS_OK = 0,
    SENS_AHT_ERROR = 1,
    SENS_TIMEOUT = 2
} SensStatus_t;

/* Inicializa I2C2 + ADC1 e, opcionalmente, inicializa o AHT10 */
void sensors_init(void);

/* AHT10 */
SensStatus_t aht10_init(void);
SensStatus_t aht10_read(AHT_Data_t *data);

/* LDR */
uint16_t ldr_read_raw(void);

/* Se você quiser manter a mesma conversão que estava usando */
static inline uint16_t ldr_convert_to_scaled(uint16_t raw)
{
    return (uint16_t)((float)raw * 2.25f);
}

/* Re-init pós STOP (você pode chamar no after_stop_reinit_everything) */
void sensors_reinit_after_stop(void);

#ifdef __cplusplus
}
#endif

#endif
