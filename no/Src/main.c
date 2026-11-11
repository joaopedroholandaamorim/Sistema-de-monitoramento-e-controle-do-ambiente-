#include "stm32f1xx.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "nrf24.h"
#include "sleep.h"
#include "sensors.h"


typedef struct __attribute__((__packed__)) {
    uint8_t  board_id;
    float    temperature;
    float    humidity;
    uint16_t adc_raw;
} SensorData_t;


static uint8_t board_id = 6;
static AHT_Data_t   sensor_data;
static SensorData_t tx_data;
static char         tx_buffer[128];
static uint16_t     ldr_adc_value;

static NRF24_t radio;

static uint8_t GW_ADDR[5] = {0xE7,0xE7,0xE7,0xE7,0xE7};  //endereço do gateway

/* ================= DEFINIÇÕES TEEN / APTEEN ================= */
#define ST_TEMP_RAW       2621U   /* ~0,5 °C (AHT_SEND_RAW = 1) */
#define ST_HUM_RAW       20972U   /* ~2 %UR  (AHT_SEND_RAW = 1) */
#define ST_LDR_RAW         120U   /* Ajuste pela faixa do ADC   */
#define HEARTBEAT_CYCLES     5U   /* Ts=60 s -> heartbeat a cada 5 min */

static uint32_t last_t = 0, last_h = 0;
static uint16_t last_l = 0;
static uint8_t  cycles_since_tx = 0xFF; /* Força o 1º envio no boot */

static inline uint32_t adiff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static uint8_t should_send(uint32_t t, uint32_t h, uint16_t l) {
    if (cycles_since_tx >= HEARTBEAT_CYCLES) return 1; /* APTEEN: Heartbeat */
    if (adiff(t, last_t) >= ST_TEMP_RAW)     return 1; /* TEEN: Limiar de Temp */
    if (adiff(h, last_h) >= ST_HUM_RAW)      return 1; /* TEEN: Limiar de Umi */
    if (adiff(l, last_l) >= ST_LDR_RAW)      return 1; /* TEEN: Limiar de Luz */
    return 0;
}

void Peripherals_Config_8MHz(void);
static void GPIO_Init_Manual(void);
static void USART2_Init_Manual(void);
void UART_Transmit_String(const char *str);


#ifndef CONFIG
#define CONFIG 0x00
#endif

/* ============================================================
 *  MODO DE DESENVOLVIMENTO
 *  DEV_KEEP_DEBUG = 1 -> mantém o SWD/ST-Link ativo mesmo em
 *  Stop/Sleep/Standby (DBGMCU). Consome mais corrente em sleep.
 *  Coloque 0 na versão final de produção (bateria).
 * ============================================================ */
#ifndef DEV_KEEP_DEBUG
#define DEV_KEEP_DEBUG 0
#endif

/* Janela de segurança no boot: o chip fica acordado (LED piscando)
 * por este tempo antes da 1ª vez que entra em Stop, para permitir
 * conexão do debugger mesmo sem DBGMCU. */
#define BOOT_GUARD_MS      5000U

/* Jumper PB5 -> GND: NÃO entra em Stop (fica acordado, debug livre) */
#define NOSLEEP_PORT  GPIOB
#define NOSLEEP_PIN   5

#define SLEEP_SECONDS 60U

/* SLEEP_DIAG = 1 -> imprime na UART, a cada acordada, o resultado da verificação do Stop */
#ifndef SLEEP_DIAG
#define SLEEP_DIAG 1
#endif


// parte do wakeup
#define NRF24_PWR_UP   (1U << 1)
#define NRF24_PRIM_RX  (1U << 0)

#define NRF_CSN_PORT GPIOA
#define NRF_CSN_PIN  3
#define NRF_CE_PORT  GPIOA
#define NRF_CE_PIN   4


static inline void nrf_csn_hi(void) { NRF_CSN_PORT->BSRR = (1U << NRF_CSN_PIN); }
static inline void nrf_csn_lo(void) { NRF_CSN_PORT->BSRR = (1U << (NRF_CSN_PIN + 16)); }

static inline void nrf_ce_lo(void)  { NRF_CE_PORT->BSRR  = (1U << (NRF_CE_PIN + 16)); }
static inline void nrf_ce_hi(void)  { NRF_CE_PORT->BSRR  = (1U << NRF_CE_PIN); }

/* Delay em ms baseado no SysTick (polling, sem interrupção). Assume HCLK=8 MHz.
 * O sleepmgr zera SysTick->CTRL antes do Stop; aqui ele é reconfigurado a cada uso. */
static void delay_ms_systick(uint32_t ms)
{
    SysTick->LOAD = 8000U - 1U;
    SysTick->VAL  = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while (ms--) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
    }
    SysTick->CTRL = 0U;
}

static inline void led_toggle(void) { GPIOC->ODR ^= (1U << 13); }

static uint8_t nosleep_jumper_active(void)
{
    return (NOSLEEP_PORT->IDR & (1U << NOSLEEP_PIN)) ? 0U : 1U;  /* pull-up: GND = ativo */
}

static void boot_guard(void)
{
    for (uint32_t t = 0; t < BOOT_GUARD_MS; t += 250U) {
        led_toggle();
        delay_ms_systick(250U);
    }
}

static void nrf24_power_down(NRF24_t *dev)
{
    Nrf24_ceLow(dev);
    Nrf24_configRegister(dev, CONFIG, 0x08);
}

static void NRF_Gateway_TX_Setup(void)
{
    Nrf24_init(&radio);

    Nrf24_config(&radio, 0x5A, 16);      // canal 0x5A, payload 16
    Nrf24_setTADDR(&radio, GW_ADDR);     // TX_ADDR e RX_ADDR_P0 (para auto-ack)

    Nrf24_SetSpeedDataRates(&radio, 2);  // 2 = 250kbps
    Nrf24_SetOutputRF_PWR(&radio, 3);    // 3 = PA_MAX

    Nrf24_setRetransmitDelay(&radio, 10);
    Nrf24_setRetransmitCount(&radio, 10);

    Nrf24_configRegister(&radio, EN_AA, 0x01);

    UART_Transmit_String("NRF MiRF: TX setup OK
");
}
static void gpio_lowpower_spi_pins(void)
{
    // Desliga SPI1
    SPI1->CR1 &= ~SPI_CR1_SPE;
    RCC->APB2ENR &= ~RCC_APB2ENR_SPI1EN;

    // PA5
    GPIOA->CRL &= ~(GPIO_CRL_MODE5 | GPIO_CRL_CNF5);
    // PA6
    GPIOA->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6);
    // PA7
    GPIOA->CRL &= ~(GPIO_CRL_MODE7 | GPIO_CRL_CNF7);
}

static void nrf24_pins_safe(void)
{
    // CE baixo sempre
    nrf_ce_lo();

    // CSN alto (desseleciona SPI)
    nrf_csn_hi();
}

static void spi1_pins_restore(void)
{
    // Habilita clock do SPI1
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    // PA5 (SCK) AF-PP 50MHz
    GPIOA->CRL &= ~(GPIO_CRL_MODE5 | GPIO_CRL_CNF5);
    GPIOA->CRL |=  (GPIO_CRL_MODE5_1 | GPIO_CRL_MODE5_0); // 50 MHz
    GPIOA->CRL |=  (GPIO_CRL_CNF5_1);                     // AF push-pull

    // PA7 (MOSI) AF-PP 50MHz
    GPIOA->CRL &= ~(GPIO_CRL_MODE7 | GPIO_CRL_CNF7);
    GPIOA->CRL |=  (GPIO_CRL_MODE7_1 | GPIO_CRL_MODE7_0);
    GPIOA->CRL |=  (GPIO_CRL_CNF7_1);

    // PA6 (MISO) input floating
    GPIOA->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6);
    GPIOA->CRL |=  (GPIO_CRL_CNF6_0); // input floating

    // (Re)habilita SPI (se seu nrf24.c não fizer isso)
    SPI1->CR1 |= SPI_CR1_SPE;
}

static void ce_csn_restore_outputs(void)
{
    // Garante clock GPIOA
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    // PA3 (CSN) output PP 2MHz
    GPIOA->CRL &= ~(GPIO_CRL_MODE3 | GPIO_CRL_CNF3);
    GPIOA->CRL |=  GPIO_CRL_MODE3_1; // 2MHz
    // CNF3 = 00 -> GP push-pull

    // PA4 (CE) output PP 2MHz
    GPIOA->CRL &= ~(GPIO_CRL_MODE4 | GPIO_CRL_CNF4);
    GPIOA->CRL |=  GPIO_CRL_MODE4_1;

    // Estados seguros
    nrf_ce_lo();
    nrf_csn_hi();
}

static void reinit_everything(void)
{
    GPIO_Init_Manual();
    USART2_Init_Manual();
    sensors_reinit_after_stop();
    ce_csn_restore_outputs();
    spi1_pins_restore();
    // NRF_Gateway_TX_Setup() removido daqui para rodar apenas no boot
}

void Peripherals_Config_8MHz(void)
{
    /* ADC a 4 MHz (Clock / 2) */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_ADCPRE) | RCC_CFGR_ADCPRE_DIV2;
    
    /* I2C2 a 100 kHz */
    I2C2->CR2 = 8;        /* Frequência do APB1 = 8 MHz */
    I2C2->CCR = 40;       /* 8MHz / (2 * 100kHz) */
    I2C2->TRISE = 9;      /* 1000ns / 125ns + 1 */
    
    /* SPI1 a 2 MHz */
    SPI1->CR1 = (SPI1->CR1 & ~SPI_CR1_BR) | SPI_CR1_BR_0; /* BR = /4 */
    
    /* USART2 a 115200 bps */
    USART2->BRR = 69;     /* 8 MHz / 115200 = ~69.44 */
    
    /* SysTick a 1ms */
    SysTick->LOAD = 8000 - 1;
}

static void GPIO_Init_Manual(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPAEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN | RCC_APB1ENR_USART2EN;

    /* LED PC13 */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->CRH &= ~GPIO_CRH_CNF13;

    /* I2C2 PB10/PB11 AF-OD 2MHz */
    GPIOB->CRH &= ~(GPIO_CRH_MODE10 | GPIO_CRH_CNF10 | GPIO_CRH_MODE11 | GPIO_CRH_CNF11);
    GPIOB->CRH |= (GPIO_CRH_MODE10_1 | GPIO_CRH_MODE11_1);
    GPIOB->CRH |= (GPIO_CRH_CNF10_1 | GPIO_CRH_CNF10_0);
    GPIOB->CRH |= (GPIO_CRH_CNF11_1 | GPIO_CRH_CNF11_0);

    /* USART2 TX PA2 AF-PP 50MHz */
    GPIOA->CRL &= ~(GPIO_CRL_MODE2 | GPIO_CRL_CNF2);
    GPIOA->CRL |= GPIO_CRL_MODE2;
    GPIOA->CRL |= GPIO_CRL_CNF2_1;

    /* ADC1 PB0 (canal 8) analógico */
    GPIOB->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);

    /* PB5: jumper "não dormir" -> entrada com pull-up (CNF=10, MODE=00, ODR=1) */
    GPIOB->CRL &= ~(GPIO_CRL_MODE5 | GPIO_CRL_CNF5);
    GPIOB->CRL |=  GPIO_CRL_CNF5_1;
    GPIOB->BSRR = (1U << NOSLEEP_PIN);
}

static void USART2_Init_Manual(void)
{
    USART2->BRR = 69; // 115200 a 8MHz
    USART2->CR1 |= USART_CR1_TE | USART_CR1_UE; /* só TX */
    USART2->CR2 = 0x0000;
    USART2->CR3 = 0x0000;
}

void UART_Transmit_String(const char *str)
{
    size_t n = strlen(str);
    for (size_t i = 0; i < n; i++) {
        while (!(USART2->SR & USART_SR_TXE));
        USART2->DR = (uint16_t)str[i];
    }
    while (!(USART2->SR & USART_SR_TC));   /* garante que o último byte saiu */
}

int main(void)
{
#if DEV_KEEP_DEBUG
    /* Mantém clock de debug em Stop/Sleep/Standby: o ST-Link continua conectando.
     * (DBGMCU->CR só é zerado em power-on reset.) */
    DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
#endif

    Peripherals_Config_8MHz();
    GPIO_Init_Manual();
    USART2_Init_Manual();

    boot_guard();               /* ~5 s acordado: janela p/ conectar o debugger */

    sensors_init();
    (void)aht10_init();         /* calibra o AHT10 se necessário (ignora falha) */
    sleepmgr_init();
    NRF_Gateway_TX_Setup();

    while (1)
    {
    	if (aht10_read(&sensor_data) == SENS_OK) {
    	    ldr_adc_value = ldr_read_raw();
            
            if (cycles_since_tx < 0xFF) {
                cycles_since_tx++;
            }
            
            uint32_t t = (uint32_t)sensor_data.temperature;
            uint32_t h = (uint32_t)sensor_data.humidity;
            
            if (should_send(t, h, ldr_adc_value)) {
                /* 1. Liga o rádio apenas para TX */
                Nrf24_configRegister(&radio, CONFIG, 0x0A); /* CONFIG = PWR_UP | CRC EN */
                delay_ms_systick(2);                  /* tpd2stby ~1.5 a 2 ms */
                
                tx_data.board_id    = board_id;
                tx_data.temperature = sensor_data.temperature;
                tx_data.humidity    = sensor_data.humidity;
                tx_data.adc_raw = ldr_adc_value;

                uint8_t pkt[16];
                memset(pkt, 0, sizeof(pkt));
                memcpy(pkt, &tx_data, sizeof(tx_data));

                Nrf24_send(&radio, pkt);
                if (Nrf24_isSend(&radio, 50)) {
                    UART_Transmit_String("TX_OK\r\n");
                    last_t = t; 
                    last_h = h; 
                    last_l = ldr_adc_value; 
                    cycles_since_tx = 0;
                } else {
                    UART_Transmit_String("TX_FAIL\r\n");
                }
                
                /* 4. Desliga o rádio imediatamente */
                Nrf24_configRegister(&radio, CONFIG, 0x08); /* CONFIG = PWR_DOWN | CRC EN */
            }

        } else {
            UART_Transmit_String("AHT10 read error\r\n");
        }

        nrf24_power_down(&radio); //desabilita TX/RX imediatamente
        nrf24_pins_safe();        // assegura tx e rx de ruidos
        gpio_lowpower_spi_pins(); //desabilita spi

        if (nosleep_jumper_active()) {
            /* Jumper PB5-GND: fica acordado (debug livre), pisca LED */
            for (uint32_t i = 0; i < SLEEP_SECONDS; i++) {
                led_toggle();
                delay_ms_systick(1000U);
            }
        } else {
            sleepmgr_sleep_seconds(SLEEP_SECONDS); //RTC Alarm
        }
        reinit_everything(); //restaura

#if SLEEP_DIAG
        {
            const SleepDiag_t *d = sleepmgr_get_diag();
            snprintf(tx_buffer, sizeof(tx_buffer),
                     "SLEEP stop=%u alarm=%u sbf=%u wfi=%u rtc_dt=%lu s\r\n",
                     d->stop_entered, d->alarm_fired, d->standby_flag, d->wfi_count,
                     (unsigned long)(d->rtc_after - d->rtc_before));
            UART_Transmit_String(tx_buffer);
        }
#endif


    }
}
