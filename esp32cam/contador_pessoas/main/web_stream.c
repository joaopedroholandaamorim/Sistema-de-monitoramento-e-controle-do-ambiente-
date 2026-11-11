#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_camera.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "img_converters.h"

#include "contador.h"
#include "web_stream.h"

#define WIFI_SSID "ContadorCam"
#define WIFI_PASS "12345678"

static const char *TAG = "web";
static EventGroupHandle_t s_wifi_eg;
#define GOT_IP_BIT BIT0

/* ---------------- quadro compartilhado ----------------
 * A tarefa de contagem e a UNICA que le a camera. Quando alguem esta vendo a imagem,
 * ela deixa uma copia do ultimo quadro aqui e os handlers HTTP so convertem para JPEG.
 * Se o handler estiver ocupado convertendo, o quadro e simplesmente pulado
 * (a contagem nunca espera pelo navegador).
 */
#define QUADRO_MAX (CT_SRC_W * CT_SRC_H)

static uint8_t *s_quadro;
static size_t s_len;
static int s_w, s_h;
static SemaphoreHandle_t s_mutex;
static volatile uint32_t s_seq;   // sobe a cada quadro publicado
static volatile int s_clientes;   // quantos handlers estao esperando imagem

void web_stream_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    s_quadro = heap_caps_malloc(QUADRO_MAX, MALLOC_CAP_SPIRAM);
    if (!s_quadro) {
        ESP_LOGE(TAG, "sem memoria para o quadro compartilhado");
    }
}

void web_stream_publicar(const uint8_t *buf, size_t len, int w, int h)
{
    if (s_clientes <= 0 || !s_quadro || len > QUADRO_MAX) return;
    if (xSemaphoreTake(s_mutex, 0) != pdTRUE) return;   // handler ocupado: pula este quadro
    memcpy(s_quadro, buf, len);
    s_len = len;
    s_w = w;
    s_h = h;
    s_seq++;
    xSemaphoreGive(s_mutex);
}

// Espera um quadro novo e converte para JPEG (o chamador libera com free()).
static bool jpeg_do_proximo_quadro(uint32_t *ultimo, uint8_t qualidade,
                                   uint8_t **jpg, size_t *len, int timeout_ms)
{
    int64_t limite = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (s_seq == *ultimo) {
        if (esp_timer_get_time() > limite) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *ultimo = s_seq;
    bool ok = fmt2jpg(s_quadro, s_len, (uint16_t)s_w, (uint16_t)s_h,
                      PIXFORMAT_GRAYSCALE, qualidade, jpg, len);
    xSemaphoreGive(s_mutex);
    return ok;
}

/* ---------------- Wi-Fi ---------------- */
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_eg, GOT_IP_BIT);
        ESP_LOGW(TAG, "Wi-Fi caiu, reconectando...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP obtido: " IPSTR "  -> abra http://" IPSTR "/",
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_wifi_eg, GOT_IP_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    xEventGroupWaitBits(s_wifi_eg, GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

/* ---------------- pagina ---------------- */
static const char PAGINA[] =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Contador</title>"
    "<style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center;margin:0;padding:8px}"
    "#n{font-size:64px;font-weight:bold;line-height:1.1}"
    ".b{background:#333;color:#eee;border:1px solid #555;padding:8px 12px;margin:4px;border-radius:6px}"
    "</style></head><body>"
    "<div>Pessoas na sala</div><div id='n'>-</div>"
    "<div id='d' style='color:#9ab'>entradas: - | saidas: -</div>"
    "<div style='position:relative;display:inline-block;width:100%;max-width:640px;margin-top:8px'>"
    "<img id='s' style='width:100%;image-rendering:pixelated;display:block'>"
    "<svg viewBox='0 0 1000 1000' preserveAspectRatio='none' "
    "style='position:absolute;left:0;top:0;width:100%;height:100%'>"
    "<line id='ln' x1='0' y1='0' x2='0' y2='0' stroke='#ff0' stroke-width='2' "
    "vector-effect='non-scaling-stroke'/></svg>"
    "</div>"
    "<div><button class='b' onclick='zerar()'>Zerar contagem</button>"
    "<button class='b' onclick='recal()'>Recalibrar fundo (entrada vazia)</button></div>"
    "<div id='i' style='color:#789;font-size:12px'></div>"
    "<script>"
    "const $=i=>document.getElementById(i);"
    "$('s').src=location.protocol+'//'+location.hostname+':81/stream';"
    "function zerar(){fetch('/zerar')}"
    "function recal(){fetch('/recalibrar')}"
    "function a(){fetch('/status').then(r=>r.json()).then(j=>{"
    "$('n').textContent=j.ocup;"
    "$('d').textContent='entradas: '+j.ent+'  |  saidas: '+j.sai;"
    "$('i').textContent='processamento: '+(j.proc_us/1000).toFixed(1)+' ms  |  '+(j.fps10/10).toFixed(1)+' FPS';"
    "const l=$('ln');l.setAttribute('x1',j.x1);l.setAttribute('y1',j.y1);"
    "l.setAttribute('x2',j.x2);l.setAttribute('y2',j.y2);"
    "}).catch(()=>{})}"
    "setInterval(a,1000);a();"
    "</script></body></html>";

/* ---------------- handlers ---------------- */
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGINA, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    contador_estado_t e = contador_estado();
    char buf[200];
    int n = snprintf(buf, sizeof(buf),
                     "{\"ent\":%d,\"sai\":%d,\"ocup\":%d,\"fps10\":%d,\"proc_us\":%d,"
                     "\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d}",
                     (int)e.entradas, (int)e.saidas, (int)e.ocupacao,
                     (int)e.fps_x10, (int)e.proc_us,
                     CT_P1X, CT_P1Y, CT_P2X, CT_P2Y);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t zerar_handler(httpd_req_t *req)
{
    contador_zerar();
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t recalibrar_handler(httpd_req_t *req)
{
    contador_recalibrar();
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    __atomic_add_fetch(&s_clientes, 1, __ATOMIC_SEQ_CST);
    uint32_t ultimo = s_seq;
    uint8_t *jpg = NULL;
    size_t len = 0;
    bool ok = jpeg_do_proximo_quadro(&ultimo, 80, &jpg, &len, 1500);
    __atomic_sub_fetch(&s_clientes, 1, __ATOMIC_SEQ_CST);
    if (!ok) { httpd_resp_send_500(req); return ESP_FAIL; }

    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t r = httpd_resp_send(req, (const char *)jpg, len);
    free(jpg);
    return r;
}

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CT   = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BND  = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, STREAM_CT);
    if (res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    __atomic_add_fetch(&s_clientes, 1, __ATOMIC_SEQ_CST);
    uint32_t ultimo = s_seq;
    char part[64];

    while (true) {
        uint8_t *jpg = NULL;
        size_t len = 0;
        if (!jpeg_do_proximo_quadro(&ultimo, 60, &jpg, &len, 2000)) { res = ESP_FAIL; break; }

        res = httpd_resp_send_chunk(req, STREAM_BND, strlen(STREAM_BND));
        if (res == ESP_OK) {
            size_t h = snprintf(part, sizeof(part), STREAM_PART, (unsigned)len);
            res = httpd_resp_send_chunk(req, part, h);
        }
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)jpg, len);
        free(jpg);
        if (res != ESP_OK) break;   // o navegador fechou a aba
    }

    __atomic_sub_fetch(&s_clientes, 1, __ATOMIC_SEQ_CST);
    return res;
}

void start_webserver(void)
{
    // limite de sockets do lwIP e 10: 4 clientes no servidor da pagina + 2 no do stream
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;

    httpd_uri_t u_index   = { .uri = "/",           .method = HTTP_GET, .handler = index_handler };
    httpd_uri_t u_status  = { .uri = "/status",     .method = HTTP_GET, .handler = status_handler };
    httpd_uri_t u_zerar   = { .uri = "/zerar",      .method = HTTP_GET, .handler = zerar_handler };
    httpd_uri_t u_recal   = { .uri = "/recalibrar", .method = HTTP_GET, .handler = recalibrar_handler };
    httpd_uri_t u_capture = { .uri = "/capture",    .method = HTTP_GET, .handler = capture_handler };

    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) == ESP_OK) {
        httpd_register_uri_handler(srv, &u_index);
        httpd_register_uri_handler(srv, &u_status);
        httpd_register_uri_handler(srv, &u_zerar);
        httpd_register_uri_handler(srv, &u_recal);
        httpd_register_uri_handler(srv, &u_capture);
    } else {
        ESP_LOGE(TAG, "falha ao iniciar o servidor da porta 80");
    }

    // servidor separado para o stream (porta 81): o stream ocupa a conexao inteira
    cfg.server_port += 1;
    cfg.ctrl_port += 1;
    cfg.max_open_sockets = 2;
    httpd_uri_t u_stream = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };
    httpd_handle_t srv2 = NULL;
    if (httpd_start(&srv2, &cfg) == ESP_OK) {
        httpd_register_uri_handler(srv2, &u_stream);
    } else {
        ESP_LOGE(TAG, "falha ao iniciar o servidor da porta 81");
    }
}
