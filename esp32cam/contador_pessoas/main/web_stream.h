#pragma once
#include <stddef.h>
#include <stdint.h>

void web_stream_init(void);   // aloca o quadro compartilhado (chamar antes de web_stream_publicar)
void wifi_init_sta(void);     // conecta no Wi-Fi e bloqueia ate obter IP
void start_webserver(void);   // porta 80 (pagina, /status, /zerar, /recalibrar, /capture) e porta 81 (/stream)

// Chamado pela tarefa de contagem a cada quadro. So copia o quadro se alguem estiver
// vendo a imagem (stream/capture); caso contrario nao custa nada.
void web_stream_publicar(const uint8_t *buf, size_t len, int w, int h);
