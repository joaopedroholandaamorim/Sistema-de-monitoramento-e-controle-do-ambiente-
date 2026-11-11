#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "contador.h"

static const char *TAG = "contador";

#define LW        CT_W
#define LH        CT_H
#define NPX       (CT_W * CT_H)
#define MAX_BLOBS  8
#define MAX_TRACKS 8

typedef struct { int32_t x16, y16, area; } blob_t;                 // centroide em 1/16 px
typedef struct { bool ativo; int32_t id, x16, y16, area; int faltas, lado; } track_t;

static uint8_t  s_g[NPX];     // quadro reduzido e suavizado
static uint16_t s_bg[NPX];    // fundo (valor x 256)
static uint8_t  s_mask[NPX];  // mascara de movimento (tambem usada como rascunho)
static uint16_t s_tmp[NPX];   // rascunho: filtro gaussiano / pilha do flood fill

static track_t s_tracks[MAX_TRACKS];
static int32_t s_prox_id;
static uint32_t s_quadro;                 // quadros desde a ultima calibracao
static volatile bool s_recalibrar;
static volatile int32_t s_ent, s_sai;

// geometria da linha (inteiros; calculada uma vez em contador_init)
static int32_t s_nx, s_ny, s_x1_16, s_y1_16, s_sinal_porta;

// estatisticas
static volatile int32_t s_fps_x10, s_proc_us;
static int64_t s_t_fps, s_soma_proc;
static int s_n_fps;

/* ------------------------------------------------------------------ */
void contador_init(void)
{
    float x1 = CT_P1X / 1000.0f * LW, y1 = CT_P1Y / 1000.0f * LH;
    float x2 = CT_P2X / 1000.0f * LW, y2 = CT_P2Y / 1000.0f * LH;
    float vx = x2 - x1, vy = y2 - y1;
    float L = sqrtf(vx * vx + vy * vy);

    s_nx    = (int32_t)lroundf(-vy / L * 1024.0f);   // normal da linha, escala 1024
    s_ny    = (int32_t)lroundf( vx / L * 1024.0f);
    s_x1_16 = (int32_t)lroundf(x1 * 16.0f);
    s_y1_16 = (int32_t)lroundf(y1 * 16.0f);

    int32_t xp16 = (int32_t)lroundf(CT_PORTA_X / 1000.0f * LW * 16.0f);
    int32_t yp16 = (int32_t)lroundf(CT_PORTA_Y / 1000.0f * LH * 16.0f);
    int32_t d = s_nx * (xp16 - s_x1_16) + s_ny * (yp16 - s_y1_16);
    if (d == 0) {
        ESP_LOGE(TAG, "CT_PORTA esta em cima da linha: escolha outro ponto");
    }
    s_sinal_porta = (d > 0) ? 1 : -1;

    s_ent = s_sai = 0;
    s_recalibrar = true;
    s_t_fps = esp_timer_get_time();
}

void contador_zerar(void)      { s_ent = 0; s_sai = 0; }
void contador_recalibrar(void) { s_recalibrar = true; }

contador_estado_t contador_estado(void)
{
    contador_estado_t e;
    e.entradas = s_ent;
    e.saidas   = s_sai;
    e.ocupacao = (s_ent > s_sai) ? (s_ent - s_sai) : 0;
    e.fps_x10  = s_fps_x10;
    e.proc_us  = s_proc_us;
    return e;
}

/* ------------------------------------------------------------------ */
// -1 = lado da porta, +1 = lado da sala, 'anterior' se estiver na zona morta
static inline int lado_de(int32_t x16, int32_t y16, int anterior)
{
    int32_t d = s_nx * (x16 - s_x1_16) + s_ny * (y16 - s_y1_16);
    const int32_t banda = CT_BANDA * 16 * 1024;
    if (d <= banda && d >= -banda) return anterior;
    return ((d > 0) == (s_sinal_porta > 0)) ? -1 : 1;
}

// 320x240 -> 80x60 pela media de blocos 4x4 (equivale ao INTER_AREA do OpenCV)
static void reduzir(const uint8_t *src, uint8_t *dst)
{
    for (int oy = 0; oy < LH; oy++) {
        uint16_t acc[LW] = {0};
        for (int dy = 0; dy < 4; dy++) {
            const uint8_t *row = src + (oy * 4 + dy) * CT_SRC_W;
            for (int ox = 0; ox < LW; ox++) {
                const uint8_t *p = row + ox * 4;
                acc[ox] += p[0] + p[1] + p[2] + p[3];
            }
        }
        for (int ox = 0; ox < LW; ox++) dst[oy * LW + ox] = (uint8_t)((acc[ox] + 8) >> 4);
    }
}

static inline int refl(int i, int n)            // borda "reflect 101", como o OpenCV
{
    if (i < 0) i = -i;
    if (i >= n) i = 2 * (n - 1) - i;
    return i;
}

// Gaussiana 5x5 separavel [1 4 6 4 1]/16 (mesmo nucleo do cv2.GaussianBlur 5x5)
static void suavizar(const uint8_t *in, uint8_t *out)
{
    static const uint8_t K[5] = {1, 4, 6, 4, 1};
    for (int y = 0; y < LH; y++) {
        for (int x = 0; x < LW; x++) {
            uint16_t s = 0;
            for (int k = 0; k < 5; k++) s += K[k] * in[y * LW + refl(x + k - 2, LW)];
            s_tmp[y * LW + x] = s;
        }
    }
    for (int y = 0; y < LH; y++) {
        for (int x = 0; x < LW; x++) {
            uint32_t s = 0;
            for (int k = 0; k < 5; k++) s += K[k] * s_tmp[refl(y + k - 2, LH) * LW + x];
            out[y * LW + x] = (uint8_t)((s + 128) >> 8);
        }
    }
}

// erosao/dilatacao 3x3 (separavel); 't' e um rascunho do mesmo tamanho
static void morf3(uint8_t *m, uint8_t *t, bool dilatar)
{
    for (int y = 0; y < LH; y++) {
        for (int x = 0; x < LW; x++) {
            uint8_t v = m[y * LW + x];
            if (x > 0)      { uint8_t a = m[y * LW + x - 1]; v = dilatar ? (v | a) : (v & a); }
            if (x < LW - 1) { uint8_t a = m[y * LW + x + 1]; v = dilatar ? (v | a) : (v & a); }
            t[y * LW + x] = v;
        }
    }
    for (int y = 0; y < LH; y++) {
        for (int x = 0; x < LW; x++) {
            uint8_t v = t[y * LW + x];
            if (y > 0)      { uint8_t a = t[(y - 1) * LW + x]; v = dilatar ? (v | a) : (v & a); }
            if (y < LH - 1) { uint8_t a = t[(y + 1) * LW + x]; v = dilatar ? (v | a) : (v & a); }
            m[y * LW + x] = v;
        }
    }
}

// componentes conexos (8 vizinhos) por flood fill; usa s_tmp como pilha
static int achar_blobs(blob_t *out, int max)
{
    int nb = 0;
    for (int i = 0; i < NPX; i++) {
        if (s_mask[i] != 1) continue;
        int sp = 0;
        s_tmp[sp++] = (uint16_t)i;
        s_mask[i] = 2;
        int32_t area = 0, sx = 0, sy = 0;
        while (sp > 0) {
            int p = s_tmp[--sp];
            int px = p % LW, py = p / LW;
            area++; sx += px; sy += py;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = px + dx, ny = py + dy;
                    if (nx < 0 || nx >= LW || ny < 0 || ny >= LH) continue;
                    int q = ny * LW + nx;
                    if (s_mask[q] == 1) { s_mask[q] = 2; s_tmp[sp++] = (uint16_t)q; }
                }
            }
        }
        if (area >= CT_AREA_MIN && nb < max) {
            out[nb].area = area;
            out[nb].x16  = (sx * 16) / area;
            out[nb].y16  = (sy * 16) / area;
            nb++;
        }
    }
    return nb;
}

static void rastrear(const blob_t *b, int nb)
{
    bool usado[MAX_BLOBS] = {0};
    const int32_t dmax2 = (CT_DIST_MAX * 16) * (CT_DIST_MAX * 16);

    for (int ti = 0; ti < MAX_TRACKS; ti++) {
        track_t *t = &s_tracks[ti];
        if (!t->ativo) continue;

        int melhor = -1;
        int32_t menor = dmax2;
        for (int bi = 0; bi < nb; bi++) {
            if (usado[bi]) continue;
            int32_t dx = b[bi].x16 - t->x16, dy = b[bi].y16 - t->y16;
            int32_t d2 = dx * dx + dy * dy;
            if (d2 < menor) { menor = d2; melhor = bi; }
        }
        if (melhor < 0) { t->faltas++; continue; }

        usado[melhor] = true;
        t->x16  = b[melhor].x16;
        t->y16  = b[melhor].y16;
        t->area = (7 * t->area + 3 * b[melhor].area) / 10;
        t->faltas = 0;

        int novo = lado_de(t->x16, t->y16, t->lado);
        if (t->lado != 0 && novo != 0 && novo != t->lado) {
            int qtd = 1;
#if CT_AREA_PESSOA > 0
            qtd = (t->area + CT_AREA_PESSOA / 2) / CT_AREA_PESSOA;
            if (qtd < 1) qtd = 1;
#endif
            if (novo == 1) s_ent += qtd;      // porta -> sala
            else           s_sai += qtd;      // sala -> porta
        }
        t->lado = novo;
    }

    for (int bi = 0; bi < nb; bi++) {         // blobs sem rastro viram rastros novos
        if (usado[bi]) continue;
        for (int ti = 0; ti < MAX_TRACKS; ti++) {
            track_t *t = &s_tracks[ti];
            if (t->ativo) continue;
            t->ativo = true;
            t->id    = s_prox_id++;
            t->x16   = b[bi].x16;
            t->y16   = b[bi].y16;
            t->area  = b[bi].area;
            t->faltas = 0;
            t->lado  = lado_de(t->x16, t->y16, 0);
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
void contador_processar(const uint8_t *frame)
{
    int64_t t0 = esp_timer_get_time();

    if (s_recalibrar) {
        s_recalibrar = false;
        s_quadro = 0;
        memset(s_bg, 0, sizeof(s_bg));
        memset(s_tracks, 0, sizeof(s_tracks));
    }

    reduzir(frame, s_mask);          // s_mask serve de rascunho aqui
    suavizar(s_mask, s_g);

    if (s_quadro < CT_AQUECIMENTO) {
        for (int i = 0; i < NPX; i++) s_bg[i] += s_g[i];
        s_quadro++;
        if (s_quadro == CT_AQUECIMENTO) {
            for (int i = 0; i < NPX; i++)
                s_bg[i] = (uint16_t)(((uint32_t)s_bg[i] * 256) / CT_AQUECIMENTO);
        }
    } else {
        const int limiar = CT_LIMIAR * 256;
        int mudou = 0;
        for (int i = 0; i < NPX; i++) {
            int d = ((int)s_g[i] << 8) - (int)s_bg[i];
            if (d < 0) d = -d;
            s_mask[i] = (d > limiar) ? 1 : 0;
            mudou += s_mask[i];
        }

        if (mudou * 100 > CT_LUZ_GLOBAL_PCT * NPX) {
            // mudanca global de luz (ex.: porta abrindo): reaprende o fundo e ignora o quadro
            for (int i = 0; i < NPX; i++) s_bg[i] = (uint16_t)s_g[i] << 8;
            memset(s_mask, 0, sizeof(s_mask));
            for (int ti = 0; ti < MAX_TRACKS; ti++)
                if (s_tracks[ti].ativo) s_tracks[ti].faltas++;
        } else {
            for (int i = 0; i < NPX; i++) {              // fundo so se atualiza onde nao ha movimento
                if (!s_mask[i]) {
                    int d = ((int)s_g[i] << 8) - (int)s_bg[i];
                    s_bg[i] = (uint16_t)((int)s_bg[i] + ((d * CT_ALFA_NUM) >> 8));
                }
            }
            morf3(s_mask, s_g, false);   // abertura (erosao + dilatacao)...
            morf3(s_mask, s_g, true);
            morf3(s_mask, s_g, true);    // ...mais 2 dilatacoes, como no contar.py
            morf3(s_mask, s_g, true);

            blob_t blobs[MAX_BLOBS];
            int nb = achar_blobs(blobs, MAX_BLOBS);
            rastrear(blobs, nb);
        }

        for (int ti = 0; ti < MAX_TRACKS; ti++)
            if (s_tracks[ti].ativo && s_tracks[ti].faltas > CT_FALTAS_MAX) s_tracks[ti].ativo = false;
    }

    // estatisticas (a cada 30 quadros)
    s_soma_proc += esp_timer_get_time() - t0;
    if (++s_n_fps >= 30) {
        int64_t agora = esp_timer_get_time();
        s_fps_x10 = (int32_t)(300LL * 1000000LL / (agora - s_t_fps));
        s_proc_us = (int32_t)(s_soma_proc / 30);
        s_t_fps = agora; s_soma_proc = 0; s_n_fps = 0;
        ESP_LOGI(TAG, "E=%d S=%d | %d.%d FPS | proc %d us",
                 (int)s_ent, (int)s_sai, (int)(s_fps_x10 / 10), (int)(s_fps_x10 % 10), (int)s_proc_us);
    }
}
