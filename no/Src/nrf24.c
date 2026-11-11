#include "nrf24.h"
#include <string.h>

static inline void gpio_set_high(GPIO_TypeDef *port, uint8_t pin) { port->BSRR = (1U << pin); }
static inline void gpio_set_low (GPIO_TypeDef *port, uint8_t pin) { port->BSRR = (1U << (pin + 16)); }

void mirf_hw_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN | RCC_APB2ENR_SPI1EN;

    /* CSN PA3 output PP 2MHz */
    GPIOA->CRL &= ~(GPIO_CRL_MODE3 | GPIO_CRL_CNF3);
    GPIOA->CRL |= GPIO_CRL_MODE3_1;
    /* CE PA4 output PP 2MHz */
    GPIOA->CRL &= ~(GPIO_CRL_MODE4 | GPIO_CRL_CNF4);
    GPIOA->CRL |= GPIO_CRL_MODE4_1;

    /* SCK PA5 AF PP 50MHz */
    GPIOA->CRL &= ~(GPIO_CRL_MODE5 | GPIO_CRL_CNF5);
    GPIOA->CRL |= (GPIO_CRL_MODE5_1 | GPIO_CRL_MODE5_0 | GPIO_CRL_CNF5_1);

    /* MISO PA6 input floating (ou pull-up) */
    GPIOA->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6);
    GPIOA->CRL |= GPIO_CRL_CNF6_0; // floating input

    /* MOSI PA7 AF PP 50MHz */
    GPIOA->CRL &= ~(GPIO_CRL_MODE7 | GPIO_CRL_CNF7);
    GPIOA->CRL |= (GPIO_CRL_MODE7_1 | GPIO_CRL_MODE7_0 | GPIO_CRL_CNF7_1);

    SPI1->CR1 = 0;
    SPI1->CR1 |= SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI1->CR1 |= SPI_CR1_BR_1 | SPI_CR1_BR_0; // /16
    SPI1->CR1 |= SPI_CR1_SPE;

    gpio_set_high(NRF24_CSN_PORT, NRF24_CSN_PIN); // CSN high
    gpio_set_low (NRF24_CE_PORT,  NRF24_CE_PIN);  // CE low
}

void mirf_delay_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms * 800U; i++) __NOP();
}

uint8_t mirf_spi_transfer(uint8_t data) {
    while (!(SPI1->SR & SPI_SR_TXE));
    *((__IO uint8_t *)&SPI1->DR) = data;
    while (!(SPI1->SR & SPI_SR_RXNE));
    return *((__IO uint8_t *)&SPI1->DR);
}

void spi_csnLow(NRF24_t *dev) { (void)dev; gpio_set_low (NRF24_CSN_PORT, NRF24_CSN_PIN); }
void spi_csnHi (NRF24_t *dev) { (void)dev; gpio_set_high(NRF24_CSN_PORT, NRF24_CSN_PIN); }
void Nrf24_ceLow(NRF24_t *dev){ (void)dev; gpio_set_low (NRF24_CE_PORT,  NRF24_CE_PIN);  }
void Nrf24_ceHi (NRF24_t *dev){ (void)dev; gpio_set_high(NRF24_CE_PORT,  NRF24_CE_PIN);  }

void Nrf24_configRegister(NRF24_t *dev, uint8_t reg, uint8_t value) {
    spi_csnLow(dev);
    mirf_spi_transfer(W_REGISTER | (REGISTER_MASK & reg));
    mirf_spi_transfer(value);
    spi_csnHi(dev);
}

void Nrf24_readRegister(NRF24_t *dev, uint8_t reg, uint8_t *value, uint8_t len) {
    spi_csnLow(dev);
    mirf_spi_transfer(R_REGISTER | (REGISTER_MASK & reg));
    for (uint8_t i=0;i<len;i++) value[i] = mirf_spi_transfer(NOP);
    spi_csnHi(dev);
}

void Nrf24_writeRegister(NRF24_t *dev, uint8_t reg, uint8_t *value, uint8_t len) {
    spi_csnLow(dev);
    mirf_spi_transfer(W_REGISTER | (REGISTER_MASK & reg));
    for (uint8_t i=0;i<len;i++) mirf_spi_transfer(value[i]);
    spi_csnHi(dev);
}

uint8_t Nrf24_getStatus(NRF24_t *dev) {
    uint8_t s;
    Nrf24_readRegister(dev, STATUS, &s, 1);
    return s;
}

void Nrf24_flushRx(NRF24_t *dev) {
    spi_csnLow(dev);
    mirf_spi_transfer(FLUSH_RX);
    spi_csnHi(dev);
}

void Nrf24_flushTx(NRF24_t *dev) {
    spi_csnLow(dev);
    mirf_spi_transfer(FLUSH_TX);
    spi_csnHi(dev);
}

void Nrf24_powerUpRx(NRF24_t *dev) {
    dev->PTX = 0;
    Nrf24_ceLow(dev);
    Nrf24_configRegister(dev, CONFIG, (uint8_t)(mirf_CONFIG | (1<<PWR_UP) | (1<<PRIM_RX)));
    mirf_delay_ms(2);
    Nrf24_ceHi(dev);
    /* clear IRQ bits TX_DS/MAX_RT */
    Nrf24_configRegister(dev, STATUS, (1<<TX_DS) | (1<<MAX_RT));
}

void Nrf24_powerUpTx(NRF24_t *dev) {
    dev->PTX = 1;
    Nrf24_ceLow(dev);
    Nrf24_configRegister(dev, CONFIG, (uint8_t)(mirf_CONFIG | (1<<PWR_UP) | (0<<PRIM_RX)));
    mirf_delay_ms(2);
    /* clear IRQ bits TX_DS/MAX_RT */
    Nrf24_configRegister(dev, STATUS, (1<<TX_DS) | (1<<MAX_RT));
}

void Nrf24_powerDown(NRF24_t *dev) {
    Nrf24_ceLow(dev);
    Nrf24_configRegister(dev, CONFIG, (uint8_t)mirf_CONFIG);
}

/* ===== Addressing ===== */
void Nrf24_setRADDR(NRF24_t *dev, uint8_t *adr) {
    Nrf24_writeRegister(dev, RX_ADDR_P1, adr, mirf_ADDR_LEN);
}

void Nrf24_setTADDR(NRF24_t *dev, uint8_t *adr) {
    Nrf24_writeRegister(dev, RX_ADDR_P0, adr, mirf_ADDR_LEN);
    Nrf24_writeRegister(dev, TX_ADDR,    adr, mirf_ADDR_LEN);
}

/* ===== RF setup helpers ===== */
void Nrf24_SetOutputRF_PWR(NRF24_t *dev, uint8_t val) {
    if (val > 3) return;
    uint8_t r;
    Nrf24_readRegister(dev, RF_SETUP, &r, 1);
    r = (r & 0xF9) | (uint8_t)(val << RF_PWR);
    Nrf24_configRegister(dev, RF_SETUP, r);
}

void Nrf24_SetSpeedDataRates(NRF24_t *dev, uint8_t val) {
    if (val > 2) return;
    uint8_t r;
    Nrf24_readRegister(dev, RF_SETUP, &r, 1);

    if (val == 2) {         /* 250kbps: RF_DR_LOW=1, RF_DR_HIGH=0 */
        r |=  (1<<RF_DR_LOW);
        r &= ~(1<<RF_DR_HIGH);
    } else {                /* 1Mbps:00, 2Mbps:01 (RF_DR_HIGH) */
        r &= ~(1<<RF_DR_LOW);
        if (val == 1) r |= (1<<RF_DR_HIGH);
        else          r &= ~(1<<RF_DR_HIGH);
    }
    Nrf24_configRegister(dev, RF_SETUP, r);
}

void Nrf24_setRetransmitDelay(NRF24_t *dev, uint8_t val) {
    if (val > 15) val = 15;
    uint8_t r;
    Nrf24_readRegister(dev, SETUP_RETR, &r, 1);
    r = (r & 0x0F) | (uint8_t)(val << 4);
    Nrf24_configRegister(dev, SETUP_RETR, r);
}

void Nrf24_setRetransmitCount(NRF24_t *dev, uint8_t val) {
    if (val > 15) val = 15;
    uint8_t r;
    Nrf24_readRegister(dev, SETUP_RETR, &r, 1);
    r = (r & 0xF0) | (val & 0x0F);
    Nrf24_configRegister(dev, SETUP_RETR, r);
}

void Nrf24_init(NRF24_t *dev) {
    memset(dev, 0, sizeof(*dev));
    dev->channel = 1;
    dev->payload = 16;

    mirf_hw_init();
    mirf_delay_ms(100);

    Nrf24_configRegister(dev, SETUP_AW, 0x03);   // 5 bytes
    Nrf24_configRegister(dev, EN_AA,    0x01);   // auto-ack pipe0
    Nrf24_configRegister(dev, EN_RXADDR,0x01);   // enable pipe0 (para ACK)
    Nrf24_configRegister(dev, DYNPD,    0x00);
    Nrf24_configRegister(dev, FEATURE,  0x00);

    Nrf24_powerUpRx(dev);
    Nrf24_flushRx(dev);
    Nrf24_flushTx(dev);
}

void Nrf24_config(NRF24_t *dev, uint8_t channel, uint8_t payload) {
    dev->channel = channel;
    dev->payload = payload;

    Nrf24_configRegister(dev, RF_CH,   dev->channel);
    Nrf24_configRegister(dev, RX_PW_P0,dev->payload);
    Nrf24_configRegister(dev, RX_PW_P1,dev->payload);

    Nrf24_powerUpRx(dev);
    Nrf24_flushRx(dev);
}

/* ===== TX/RX data path ===== */
bool Nrf24_dataReady(NRF24_t *dev) {
    uint8_t st = Nrf24_getStatus(dev);
    if (st & (1<<RX_DR)) {
        dev->status = st;
        return true;
    }
    return false;
}

void Nrf24_getData(NRF24_t *dev, uint8_t *data) {
    spi_csnLow(dev);
    mirf_spi_transfer(R_RX_PAYLOAD);
    for (uint8_t i=0;i<dev->payload;i++) data[i] = mirf_spi_transfer(NOP);
    spi_csnHi(dev);

    /* clear RX_DR */
    Nrf24_configRegister(dev, STATUS, (1<<RX_DR));
}

void Nrf24_send(NRF24_t *dev, uint8_t *value) {
    /* espera TX anterior terminar, com timeout (~50 ms) para não travar se o nRF não responder */
    for (uint32_t tries = 0; dev->PTX && tries < 50U; tries++) {
        uint8_t st = Nrf24_getStatus(dev);
        if (st & ((1<<TX_DS) | (1<<MAX_RT))) break;
        mirf_delay_ms(1);
    }
    dev->PTX = 0;

    Nrf24_ceLow(dev);
    Nrf24_powerUpTx(dev);

    /* flush tx */
    Nrf24_flushTx(dev);

    /* write payload */
    spi_csnLow(dev);
    mirf_spi_transfer(W_TX_PAYLOAD);
    for (uint8_t i=0;i<dev->payload;i++) mirf_spi_transfer(value[i]);
    spi_csnHi(dev);

    /* start transmission */
    Nrf24_ceHi(dev);
    mirf_delay_ms(1);
    Nrf24_ceLow(dev);
}

bool Nrf24_isSend(NRF24_t *dev, int timeout_ms) {
    uint32_t waited = 0;
    while (waited < (uint32_t)timeout_ms) {
        uint8_t st = Nrf24_getStatus(dev);

        if (st & (1<<TX_DS)) {
            Nrf24_configRegister(dev, STATUS, (1<<TX_DS));
            return true;
        }
        if (st & (1<<MAX_RT)) {
            Nrf24_configRegister(dev, STATUS, (1<<MAX_RT));
            Nrf24_flushTx(dev);
            return false;
        }
        
        SysTick->LOAD = 8000U - 1U;
        SysTick->VAL  = 0U;
        SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; /* SEM TICKINT! */
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
        SysTick->CTRL = 0U;
        
        waited++;
    }
    return false;
}
