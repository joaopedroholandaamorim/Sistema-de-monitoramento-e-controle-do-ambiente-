#ifndef MIRF_STM32_H_
#define MIRF_STM32_H_

#include "stm32f1xx.h"
#include <stdint.h>
#include <stdbool.h>

/* ===== NRF24 registers ===== */
#define CONFIG      0x00
#define EN_AA       0x01
#define EN_RXADDR   0x02
#define SETUP_AW    0x03
#define SETUP_RETR  0x04
#define RF_CH       0x05
#define RF_SETUP    0x06
#define STATUS      0x07
#define RX_ADDR_P0  0x0A
#define RX_ADDR_P1  0x0B
#define TX_ADDR     0x10
#define RX_PW_P0    0x11
#define RX_PW_P1    0x12
#define FIFO_STATUS 0x17
#define DYNPD       0x1C
#define FEATURE     0x1D

/* ===== Bit positions ===== */
#define MASK_RX_DR  6
#define MASK_TX_DS  5
#define MASK_MAX_RT 4
#define EN_CRC      3
#define CRCO        2
#define PWR_UP      1
#define PRIM_RX     0

#define RX_DR       6
#define TX_DS       5
#define MAX_RT      4

#define RF_DR_LOW   5
#define RF_DR_HIGH  3
#define RF_PWR      1

#define RX_EMPTY    0

/* ===== Commands ===== */
#define R_REGISTER    0x00
#define W_REGISTER    0x20
#define REGISTER_MASK 0x1F
#define R_RX_PAYLOAD  0x61
#define W_TX_PAYLOAD  0xA0
#define FLUSH_TX      0xE1
#define FLUSH_RX      0xE2
#define NOP           0xFF
#define W_TX_PAYLOAD_NO_ACK  0xB0

#define mirf_ADDR_LEN  5

/* MiRF base CONFIG:
   - mascara TX_DS (igual seu dump do gateway com CONFIG=0x2B)
   - CRC habilitado (8-bit)
*/
#define mirf_CONFIG ((1<<MASK_TX_DS) | (1<<EN_CRC) | (0<<CRCO))

typedef struct {
    uint8_t PTX;
    uint8_t channel;
    uint8_t payload;
    uint8_t status;
} NRF24_t;

/* ===== Hardware mapping (ajuste aqui se mudar pinos) ===== */
#define NRF24_CSN_PORT GPIOA
#define NRF24_CSN_PIN  3     // PA3
#define NRF24_CE_PORT  GPIOA
#define NRF24_CE_PIN   4     // PA4

/* ===== Low-level ===== */
void     mirf_hw_init(void);
void     mirf_delay_ms(uint32_t ms);
uint8_t  mirf_spi_transfer(uint8_t data);

void     spi_csnLow(NRF24_t *dev);
void     spi_csnHi(NRF24_t *dev);
void     Nrf24_ceLow(NRF24_t *dev);
void     Nrf24_ceHi(NRF24_t *dev);

/* ===== MiRF API (STM32 port) ===== */
void     Nrf24_init(NRF24_t *dev);
void     Nrf24_config(NRF24_t *dev, uint8_t channel, uint8_t payload);

void     Nrf24_configRegister(NRF24_t *dev, uint8_t reg, uint8_t value);
void     Nrf24_readRegister(NRF24_t *dev, uint8_t reg, uint8_t *value, uint8_t len);
void     Nrf24_writeRegister(NRF24_t *dev, uint8_t reg, uint8_t *value, uint8_t len);

bool     Nrf24_dataReady(NRF24_t *dev);
void     Nrf24_getData(NRF24_t *dev, uint8_t *data);

uint8_t  Nrf24_getStatus(NRF24_t *dev);

void     Nrf24_powerUpRx(NRF24_t *dev);
void     Nrf24_powerUpTx(NRF24_t *dev);
void     Nrf24_powerDown(NRF24_t *dev);

void     Nrf24_flushRx(NRF24_t *dev);
void     Nrf24_flushTx(NRF24_t *dev);

void     Nrf24_setRADDR(NRF24_t *dev, uint8_t *adr); // RX_ADDR_P1
void     Nrf24_setTADDR(NRF24_t *dev, uint8_t *adr); // TX_ADDR + RX_ADDR_P0

void     Nrf24_send(NRF24_t *dev, uint8_t *value);
bool     Nrf24_isSend(NRF24_t *dev, int timeout_ms);

void     Nrf24_SetOutputRF_PWR(NRF24_t *dev, uint8_t val);      // 0..3
void     Nrf24_SetSpeedDataRates(NRF24_t *dev, uint8_t val);    // 0=1Mbps 1=2Mbps 2=250Kbps
void     Nrf24_setRetransmitDelay(NRF24_t *dev, uint8_t val);   // 0..15 (250us steps)
void     Nrf24_setRetransmitCount(NRF24_t *dev, uint8_t val);   // 0..15

#endif /* MIRF_STM32_H_ */
