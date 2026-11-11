/*
 * Medidor de corrente/tensão com módulo INA226 (WCMCU-226 / CJMCU-226) no ESP32
 * ESP-IDF v5.2+ (API nova i2c_master, a mesma do seu exemplo do MPU9250).
 *
 * Ligações (ESP32 DevKit):
 *   3V3 -> VCC   | GND -> GND | GPIO21 -> SDA | GPIO22 -> SCL
 *   Circuito medido em série: carga ligada entre IN+ (lado da fonte) e IN- (lado da carga)
 *
 * Saída serial (CSV):  t_ms, vbus_V, i_mA, p_mW, q_uAh
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"

#define TAG "ina226"

#define PIN_SDA            GPIO_NUM_21
#define PIN_SCL            GPIO_NUM_22
#define I2C_FREQ_HZ        400000            
#define INA_ADDR           0x40              
#define SHUNT_OHMS         0.1f              
#define SHUNT_OFFSET_UV    0.0f             
#define INA_AVG            0
#define INA_VBUSCT         4
#define INA_VSHCT          
#define REG_CONFIG         0x00
#define REG_SHUNT_V        0x01   /* 2,5 uV/LSB, com sinal */
#define REG_BUS_V          0x02   /* 1,25 mV/LSB */
#define REG_MASK_EN        0x06   /* bit 3 = CVRF (conversão pronta) */
#define REG_MANUF_ID       0xFE   /* 0x5449 */
#define REG_DIE_ID         0xFF   /* 0x2260 */

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t dev;

static esp_err_t ina_read16(uint8_t reg, uint16_t *val)
{
    uint8_t rx[2];
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, rx, 2, 100);
    if (err == ESP_OK) *val = ((uint16_t)rx[0] << 8) | rx[1];
    return err;
}

static esp_err_t ina_write16(uint8_t reg, uint16_t val)
{
    uint8_t tx[3] = { reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    return i2c_master_transmit(dev, tx, sizeof(tx), 100);
}

static void scan_bus(void)
{
    ESP_LOGW(TAG, "Procurando dispositivos I2C...");
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(bus, a, 20) == ESP_OK) {
            ESP_LOGW(TAG, "  achei dispositivo em 0x%02X", a);
        }
    }
}

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* a placa costuma ter pull-ups; os internos ajudam */
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &dev));

    uint16_t manuf = 0, die = 0;
    if (ina_read16(REG_MANUF_ID, &manuf) != ESP_OK || ina_read16(REG_DIE_ID, &die) != ESP_OK) {
        ESP_LOGE(TAG, "Sem resposta em 0x%02X. Confira fiação, VCC e endereço.", INA_ADDR);
        scan_bus();
        return;
    }
    ESP_LOGI(TAG, "MANUF_ID=0x%04X (esperado 0x5449)  DIE_ID=0x%04X (esperado 0x2260)", manuf, die);

    uint16_t cfg = 0x4000 | (INA_AVG << 9) | (INA_VBUSCT << 6) | (INA_VSHCT << 3) | 0x07;
    ESP_ERROR_CHECK(ina_write16(REG_CONFIG, cfg));

    ESP_LOGI(TAG, "Shunt = %.3f ohm | faixa = +/-%.0f mA | resolucao = %.1f uA",
             SHUNT_OHMS, 81.92e-3f / SHUNT_OHMS * 1000.0f, 2.5e-6f / SHUNT_OHMS * 1e6f);
    printf("t_ms,vbus_V,i_mA,p_mW,q_uAh\n");

    double q_uAh = 0.0;
    int64_t t_prev = esp_timer_get_time();

    while (1) {
        uint16_t mask = 0;
        if (ina_read16(REG_MASK_EN, &mask) != ESP_OK) {      /* ler limpa o CVRF */
            ESP_LOGE(TAG, "Erro de leitura I2C");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!(mask & (1 << 3))) {                            
            vTaskDelay(1);
            continue;
        }

        uint16_t raw_sh = 0, raw_bus = 0;
        if (ina_read16(REG_SHUNT_V, &raw_sh) != ESP_OK || ina_read16(REG_BUS_V, &raw_bus) != ESP_OK) {
            ESP_LOGE(TAG, "Erro de leitura I2C");
            continue;
        }

        float vshunt_uV = (int16_t)raw_sh * 2.5f - SHUNT_OFFSET_UV;
        float i_mA      = vshunt_uV / SHUNT_OHMS / 1000.0f;   /* uV / ohm = uA; /1000 = mA */
        float vbus_V    = raw_bus * 1.25e-3f;                 /* 0 se o pino VBS não estiver ligado */
        float p_mW      = vbus_V * i_mA;

        int64_t now = esp_timer_get_time();
        q_uAh += (double)i_mA * 1000.0 * ((now - t_prev) / 1e6) / 3600.0;  /* mA->uA, us->s, s->h */
        t_prev = now;

        printf("%lld,%.3f,%.3f,%.2f,%.3f\n", (long long)(now / 1000), vbus_V, i_mA, p_mW, q_uAh);
    }
}
