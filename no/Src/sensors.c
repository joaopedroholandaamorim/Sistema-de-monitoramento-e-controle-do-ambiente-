#include "sensors.h"
#include <string.h>

/* ------------------------------------------------------------------
 * AHT_SEND_RAW = 1 -> temperature/humidity carregam o valor BRUTO de 20 bits
 *                     (0..1048575) convertido para float, sem escala.
 * AHT_SEND_RAW = 0 -> converte para %UR e °C.
 * ------------------------------------------------------------------ */
#ifndef AHT_SEND_RAW
#define AHT_SEND_RAW 1
#endif

/*I2C2 / AHT10 ===================== */
#define AHT10_I2C_ADDR    (0x38 << 1)
#define AHT10_CMD_INIT    0xBE
#define AHT10_CMD_MEASURE 0xAC

static void I2C2_Init_Manual(void);
static void ADC1_Init_Manual(void);

static SensStatus_t I2C_Master_Transmit(I2C_TypeDef *I2Cx, uint8_t address,
                                        uint8_t *data, uint16_t size, uint32_t timeout);

static SensStatus_t I2C_Master_Receive(I2C_TypeDef *I2Cx, uint8_t address,
                                       uint8_t *data, uint16_t size, uint32_t timeout);

static uint8_t AHT10_IsBusy(void);

extern volatile uint32_t SysTick_Count;

void delay_wfi_ms(uint32_t ms) {
    SysTick->LOAD = 8000U - 1U;
    SysTick->VAL  = 0U;
    /* REMOVIDO o SysTick_CTRL_TICKINT_Msk que causava o travamento */
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; 

    for(uint32_t i=0; i<ms; i++) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
    }
    SysTick->CTRL = 0U;
}


void sensors_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;   /* GPIOB (I2C2) */
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;   /* I2C2 */
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN | RCC_APB2ENR_AFIOEN; /* ADC1/AFIO */

    I2C2_Init_Manual();
    ADC1_Init_Manual();
}



/* Reinicializa I2C2 e ADC1 depois do Stop (PE e ADON foram zerados em sleep.c). */
void sensors_reinit_after_stop(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_ADC1EN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;

    I2C2_Init_Manual();
    ADC1_Init_Manual();
}

SensStatus_t aht10_init(void)
{
    uint8_t init_cmd[] = {AHT10_CMD_INIT, 0x08, 0x00};
    uint8_t status_byte = 0;

    delay_wfi_ms(40); //40ms init time

    if (I2C_Master_Receive(I2C2, AHT10_I2C_ADDR, &status_byte, 1, 100000) != SENS_OK)
        return SENS_TIMEOUT;

    if (!(status_byte & 0x08)) {
        if (I2C_Master_Transmit(I2C2, AHT10_I2C_ADDR, init_cmd, 3, 100000) != SENS_OK)
            return SENS_AHT_ERROR;
        delay_wfi_ms(15);
    }

    if (I2C_Master_Receive(I2C2, AHT10_I2C_ADDR, &status_byte, 1, 100000) != SENS_OK)
        return SENS_TIMEOUT;

    if (!(status_byte & 0x08))
        return SENS_AHT_ERROR;

    return SENS_OK;
}

SensStatus_t aht10_read(AHT_Data_t *data)
{
    uint8_t measure_cmd[] = {AHT10_CMD_MEASURE, 0x33, 0x00};
    uint8_t rx_buf[6] = {0};
    uint32_t raw_h = 0, raw_t = 0;

    if (!data) return SENS_AHT_ERROR;

    if (I2C_Master_Transmit(I2C2, AHT10_I2C_ADDR, measure_cmd, 3, 100000) != SENS_OK) {
        I2C2_Init_Manual();     /* destrava o periférico após erro/NACK */
        return SENS_AHT_ERROR;
    }

    delay_wfi_ms(80);

    uint32_t timeout = 0;
    while (AHT10_IsBusy()) {
        if (timeout++ > 1000) return SENS_TIMEOUT;
        for (volatile int i=0;i<300;i++) __NOP();
    }

    if (I2C_Master_Receive(I2C2, AHT10_I2C_ADDR, rx_buf, 6, 100000) != SENS_OK)
        return SENS_AHT_ERROR;

    if (!(rx_buf[0] & 0x08))
        return SENS_AHT_ERROR;

    raw_h  = ((uint32_t)rx_buf[1] << 12) | ((uint32_t)rx_buf[2] << 4) | ((rx_buf[3] >> 4) & 0x0F);
#if AHT_SEND_RAW
    data->humidity = (float)raw_h;                                  /* bruto (20 bits) */
#else
    data->humidity = (float)raw_h * 100.0f / 1048576.0f;            /* %UR */
#endif

    raw_t  = ((uint32_t)(rx_buf[3] & 0x0F) << 16) | ((uint32_t)rx_buf[4] << 8) | rx_buf[5];
#if AHT_SEND_RAW
    data->temperature = (float)raw_t;                               /* bruto (20 bits) */
#else
    data->temperature = (float)raw_t * 200.0f / 1048576.0f - 50.0f; /* °C */
#endif
    return SENS_OK;
}

uint16_t ldr_read_raw(void)
{
    ADC1->CR2 |= ADC_CR2_ADON;
    for (volatile int i=0;i<10;i++) __NOP();

    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC));
    return (uint16_t)ADC1->DR;
}

static void I2C2_Init_Manual(void)
{
    I2C2->CR1 |= I2C_CR1_SWRST;
    I2C2->CR1 &= ~I2C_CR1_SWRST;

    I2C2->CR2 = 8;
    I2C2->CCR = 40;
    I2C2->TRISE = 9;
    I2C2->OAR1 = (1 << 14);
    I2C2->CR1 |= I2C_CR1_PE;
}

static SensStatus_t I2C_Master_Transmit(I2C_TypeDef *I2Cx, uint8_t address,
                                        uint8_t *data, uint16_t size, uint32_t timeout)
{
    uint32_t t = 0;
    I2Cx->CR1 |= I2C_CR1_START;
    while (!(I2Cx->SR1 & I2C_SR1_SB) && (t++ < timeout));
    if (t >= timeout) return SENS_TIMEOUT;

    I2Cx->DR = address & ~0x01;
    t = 0; while (!(I2Cx->SR1 & I2C_SR1_ADDR) && (t++ < timeout));
    if (t >= timeout) return SENS_TIMEOUT;
    (void)I2Cx->SR2;

    for (uint16_t i = 0; i < size; i++) {
        t = 0; while (!(I2Cx->SR1 & I2C_SR1_TXE) && (t++ < timeout));
        if (t >= timeout) return SENS_TIMEOUT;
        I2Cx->DR = data[i];
    }

    t = 0; while (!(I2Cx->SR1 & I2C_SR1_BTF) && (t++ < timeout));
    if (t >= timeout) return SENS_TIMEOUT;

    I2Cx->CR1 |= I2C_CR1_STOP;
    return SENS_OK;
}

static SensStatus_t I2C_Master_Receive(I2C_TypeDef *I2Cx, uint8_t address,
                                       uint8_t *data, uint16_t size, uint32_t timeout)
{
    uint32_t t = 0;
    I2Cx->CR1 |= I2C_CR1_ACK;
    I2Cx->CR1 |= I2C_CR1_START;
    while (!(I2Cx->SR1 & I2C_SR1_SB) && (t++ < timeout));
    if (t >= timeout) return SENS_TIMEOUT;

    I2Cx->DR = address | 0x01;
    t = 0; while (!(I2Cx->SR1 & I2C_SR1_ADDR) && (t++ < timeout));
    if (t >= timeout) return SENS_TIMEOUT;
    (void)I2Cx->SR2;

    for (uint16_t i = 0; i < size; i++) {
        if (i == size - 1) {
            I2Cx->CR1 &= ~I2C_CR1_ACK;
            I2Cx->CR1 |= I2C_CR1_STOP;
        }
        t = 0; while (!(I2Cx->SR1 & I2C_SR1_RXNE) && (t++ < timeout));
        if (t >= timeout) return SENS_TIMEOUT;
        data[i] = I2Cx->DR;
    }

    t = 0; while (I2Cx->CR1 & I2C_CR1_STOP) { if (t++ >= timeout) return SENS_TIMEOUT; }
    I2Cx->CR1 |= I2C_CR1_ACK;
    return SENS_OK;
}

static uint8_t AHT10_IsBusy(void)
{
    uint8_t status_byte;
    if (I2C_Master_Receive(I2C2, AHT10_I2C_ADDR, &status_byte, 1, 100000) != SENS_OK)
        return 1;
    return (status_byte & 0x80);
}

static void ADC1_Init_Manual(void)
{
    ADC1->CR2 &= ~ADC_CR2_ADON;
    RCC->CFGR |= RCC_CFGR_ADCPRE_DIV2; /* 8/2=4MHz */

    ADC1->CR2 |= ADC_CR2_ADON;
    for (volatile int i=0;i<10;i++) __NOP();

    ADC1->CR2 |= ADC_CR2_RSTCAL;
    while(ADC1->CR2 & ADC_CR2_RSTCAL);

    ADC1->CR2 |= ADC_CR2_CAL;
    while(ADC1->CR2 & ADC_CR2_CAL);

    ADC1->CR2 &= ~ADC_CR2_CONT;
    ADC1->CR2 &= ~ADC_CR2_ALIGN;

    /* Canal fixo 8 (PB0) */
    ADC1->SQR3 = 8;
    ADC1->SQR1 = 0;

    ADC1->SMPR2 |= (0x02 << (8 * 3));
}
