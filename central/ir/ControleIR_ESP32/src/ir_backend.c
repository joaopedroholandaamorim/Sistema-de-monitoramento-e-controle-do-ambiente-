#include "ir_backend.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "sdkconfig.h"
#include <string.h>
#if !defined(CONFIG_IDF_TARGET_ESP32) || CONFIG_FREERTOS_UNICORE
#error "Este exemplo exige ESP32 classica dual-core (WROOM-32)."
#endif
static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static rmt_symbol_word_t symbols[IR_MAX_SYMBOLS];
static bool failed;
esp_err_t ir_backend_init(int gpio) {
    rmt_tx_channel_config_t cfg = {
        .gpio_num = gpio, .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, .mem_block_symbols = IR_MAX_SYMBOLS,
        .trans_queue_depth = 1
    };
    esp_err_t e = rmt_new_tx_channel(&cfg, &channel);
    if (e != ESP_OK) return e;
    rmt_carrier_config_t carrier = {.frequency_hz = 38000, .duty_cycle = 1.0f/3.0f};
    e = rmt_apply_carrier(channel, &carrier);
    if (e != ESP_OK) goto cleanup;
    rmt_copy_encoder_config_t enc = {};
    e = rmt_new_copy_encoder(&enc, &encoder);
    if (e != ESP_OK) goto cleanup;
    e = rmt_enable(channel);
    if (e == ESP_OK) return ESP_OK;
    rmt_del_encoder(encoder); encoder = NULL;
cleanup:
    rmt_del_channel(channel); channel = NULL;
    return e;
}
esp_err_t ir_backend_send(const ir_pulse_t *pulses, size_t count) {
    if (!channel || failed) return ESP_ERR_INVALID_STATE;
    if (!pulses || !count || count > IR_MAX_HALVES) return ESP_ERR_INVALID_ARG;
    memset(symbols, 0, sizeof symbols);
    for (size_t i = 0; i < count; ++i) {
        if (i & 1) {
            symbols[i/2].duration1 = pulses[i].us;
            symbols[i/2].level1 = pulses[i].level;
        } else {
            symbols[i/2].duration0 = pulses[i].us;
            symbols[i/2].level0 = pulses[i].level;
        }
    }
    /* Metade final zero termina a emissão quando há número ímpar de pulsos.
     * Sempre há espaço adicional para o EOF do driver. */
    rmt_transmit_config_t tx = { .loop_count = 0 };
    esp_err_t e = rmt_transmit(channel, encoder, symbols,
                              ((count + 1)/2) * sizeof symbols[0], &tx);
    if (e == ESP_OK) e = rmt_tx_wait_all_done(channel, 3000);
    /* Em timeout, manter buffer intacto e bloquear novos envios até reboot. */
    if (e != ESP_OK) failed = true;
    return e;
}
