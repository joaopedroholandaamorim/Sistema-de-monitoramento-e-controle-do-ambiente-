#ifndef SLEEPMGR_H_
#define SLEEPMGR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Diagnóstico da última chamada de sleepmgr_sleep_seconds() */
typedef struct {
    uint8_t  stop_entered;   /* 1 = o clock de sistema caiu p/ HSI ao acordar => Stop de verdade */
    uint8_t  alarm_fired;    /* 1 = acordou pelo alarme RTC */
    uint8_t  standby_flag;   /* 1 = PWR_CSR.SBF setado (entrou em Standby: NÃO deveria) */
    uint8_t  wfi_count;      /* quantas vezes o WFI retornou (>1 = wakeups espúrios) */
    uint32_t rtc_before;     /* contador RTC ao armar o alarme (s) */
    uint32_t rtc_after;      /* contador RTC ao acordar (s) */
} SleepDiag_t;

const SleepDiag_t *sleepmgr_get_diag(void);

void sleepmgr_init(void);


void sleepmgr_sleep_seconds(uint32_t seconds);


uint8_t sleepmgr_last_wakeup_was_rtc(void);

#ifdef __cplusplus
}
#endif

#endif /* SLEEPMGR_H_ */
