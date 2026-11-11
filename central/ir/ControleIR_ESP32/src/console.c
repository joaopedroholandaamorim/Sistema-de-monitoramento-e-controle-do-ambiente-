#include "console.h"
#include "ir_service.h"
#include "IR_utils.h"
#include <stdio.h>
#include <string.h>
void ir_console_char(char c) {
    static char line[32]; static size_t n; static bool overflow;
    if (c != '\r' && c != '\n') {
        if (n < sizeof line - 1 && !overflow) line[n++] = c;
        else overflow = true;
        return;
    }
    if (overflow) puts("Linha longa: descartada.");
    else if (n) {
        line[n] = 0; bool ok = false, valid = true;
        if (!strcmp(line, "ON")) ok = ir_request_coolix(LIGARAC23_COOLIX);
        else if (!strcmp(line, "OFF")) ok = ir_request_coolix(DESLIGARAC_COOLIX);
        else if (!strcmp(line, "SWING")) ok = ir_request_coolix(MUDARDIRECAO_COOLIX);
        else if (n == 2 && line[0] >= '0' && line[0] <= '9' && line[1] >= '0' && line[1] <= '9') {
            unsigned t = 10 * (line[0]-'0') + line[1]-'0';
            valid = t >= 18 && t <= 30;
            if (valid) ok = ir_request_temperature(t);
        } else valid = false;
        puts(!valid ? "Use ON OFF SWING ou 18..30." : ok ? "Enfileirado (nao e ACK)." : "Recusado: fila cheia ou IR indisponivel.");
    }
    n = 0; overflow = false;
}
