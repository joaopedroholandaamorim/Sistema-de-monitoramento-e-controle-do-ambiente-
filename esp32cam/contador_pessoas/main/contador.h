#pragma once
#include <stdint.h>

/*
 * Contador de pessoas leve para a ESP32-CAM (porta do contar.py validado no PC).
 * Trabalha em 80x60 (o quadro 320x240 e reduzido 4x) so com inteiros, sem malloc.
 */

// Quadro de entrada (QVGA, grayscale) e resolucao de trabalho
#define CT_SRC_W 320
#define CT_SRC_H 240
#define CT_W     80
#define CT_H     60

// Linha diagonal em MILESIMOS da imagem (0..1000): fracao do contar.py x 1000
#define CT_P1X 0
#define CT_P1Y 0
#define CT_P2X 1000
#define CT_P2Y 1000
// Um ponto que fica do lado da PORTA da linha (milesimos)
#define CT_PORTA_X 200
#define CT_PORTA_Y 50

// Parametros do algoritmo (mesmos do contar.py)
#define CT_BANDA          3    // zona morta ao redor da linha (px em 80x60)
#define CT_LIMIAR         25   // diferenca minima de brilho
#define CT_ALFA_NUM       5    // velocidade do fundo: 5/256 ~ 0.02
#define CT_AREA_MIN       10   // area minima do blob (px em 80x60)
#define CT_DIST_MAX       10   // deslocamento maximo do centroide por quadro (px)
#define CT_FALTAS_MAX     5    // quadros sem ver um rastro antes de descarta-lo
#define CT_AQUECIMENTO    15   // quadros iniciais (entrada vazia) para montar o fundo
#define CT_LUZ_GLOBAL_PCT 40   // % da imagem mudando = mudanca de luz, nao pessoa
#define CT_AREA_PESSOA    0    // area de 1 pessoa (px); 0 = cada blob conta 1

typedef struct {
    int32_t entradas;
    int32_t saidas;
    int32_t ocupacao;   // entradas - saidas, nunca abaixo de 0
    int32_t fps_x10;    // FPS do processamento x10
    int32_t proc_us;    // tempo medio de processamento por quadro (microssegundos)
} contador_estado_t;

void contador_init(void);
void contador_processar(const uint8_t *quadro_gray_320x240);
void contador_zerar(void);        // zera entradas e saidas
void contador_recalibrar(void);   // refaz o fundo (deixe a entrada vazia ~2 s)
contador_estado_t contador_estado(void);
