

#include "stm32f1xx.h"
#include <stdint.h>
#include "sleep.h"

static SleepDiag_t g_diag;
const SleepDiag_t *sleepmgr_get_diag(void) { return &g_diag; }


void sleepmgr_init(void);
void sleepmgr_sleep_seconds(uint32_t seconds);
uint8_t sleepmgr_last_wakeup_was_rtc(void);

extern void Peripherals_Config_8MHz(void);

static volatile uint8_t g_rtc_alarm_fired = 0;


static uint32_t rtc_get_counter(void)
{
    /* leitura atômica: repete se CNTH mudou entre as leituras (rollover do CNTL) */
    uint32_t high1, high2, low;
    do {
        high1 = RTC->CNTH;
        low   = RTC->CNTL;
        high2 = RTC->CNTH;
    } while (high1 != high2);
    return (high1 << 16) | low;
}

/* Após Stop o barramento APB1 volta a ser clockado: ressincroniza os registradores
 * de leitura do RTC (RSF) antes de ler o contador. */
static void rtc_resync(void)
{
    uint32_t guard = 0x100000U;
    RTC->CRL &= ~RTC_CRL_RSF;
    while (!(RTC->CRL & RTC_CRL_RSF) && guard--) { }
}

static void rtc_wait_write_ready(void)
{
    while (!(RTC->CRL & RTC_CRL_RTOFF)) {
    }
}

static void rtc_enter_config(void)
{
    rtc_wait_write_ready();
    RTC->CRL |= RTC_CRL_CNF;
}

static void rtc_exit_config(void)
{
    RTC->CRL &= ~RTC_CRL_CNF;
    rtc_wait_write_ready();
}

static void rtc_set_prescaler_1hz_lsi(void)
{
    /* LSI ~ 40 kHz -> PRL = 39999 -> ~1 Hz */
    rtc_enter_config();
    RTC->PRLH = 0;
    RTC->PRLL = 39999;
    rtc_exit_config();
}

static void rtc_set_alarm(uint32_t alarm_value)
{
    rtc_enter_config();
    RTC->ALRH = (alarm_value >> 16) & 0xFFFF;
    RTC->ALRL = (alarm_value) & 0xFFFF;
    rtc_exit_config();
}


static inline void rtc_alarm_isr_common(void)
{
    EXTI->PR = EXTI_PR_PR17;
    RTC->CRL &= ~(RTC_CRL_ALRF);
    g_rtc_alarm_fired = 1;
}


void RTC_Alarm_IRQHandler(void) { rtc_alarm_isr_common(); }
void RTCAlarm_IRQHandler(void)  { rtc_alarm_isr_common(); }


void sleepmgr_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;

    PWR->CR |= PWR_CR_DBP;

    RCC->CSR |= RCC_CSR_LSION;
    while (!(RCC->CSR & RCC_CSR_LSIRDY)) {
    }

    RCC->BDCR &= ~RCC_BDCR_RTCSEL;
    RCC->BDCR |= RCC_BDCR_RTCSEL_1;

    RCC->BDCR |= RCC_BDCR_RTCEN;

    RTC->CRL &= ~RTC_CRL_RSF;
    while (!(RTC->CRL & RTC_CRL_RSF)) {
    }

    rtc_set_prescaler_1hz_lsi();

    rtc_wait_write_ready();
    RTC->CRH |= RTC_CRH_ALRIE;

    EXTI->IMR  |= EXTI_IMR_MR17;
    EXTI->RTSR |= EXTI_RTSR_TR17;
    EXTI->PR    = EXTI_PR_PR17;

    NVIC_SetPriority(RTC_Alarm_IRQn, 2);
    NVIC_EnableIRQ(RTC_Alarm_IRQn);

    RTC->CRL &= ~(RTC_CRL_ALRF);
    g_rtc_alarm_fired = 0;
}


static void sleepmgr_prepare_for_stop(void)
{
    SysTick->CTRL = 0;



    if (RCC->APB1ENR & RCC_APB1ENR_USART2EN) {
        while ((USART2->CR1 & USART_CR1_UE) && !(USART2->SR & USART_SR_TC));
        USART2->CR1 &= ~USART_CR1_UE;
    }

    if (RCC->APB1ENR & RCC_APB1ENR_I2C2EN) {
        I2C2->CR1 &= ~I2C_CR1_PE;
    }

    if (RCC->APB2ENR & RCC_APB2ENR_ADC1EN) {
        ADC1->CR2 &= ~ADC_CR2_ADON;
    }

    if (RCC->APB2ENR & RCC_APB2ENR_SPI1EN) {
        SPI1->CR1 &= ~SPI_CR1_SPE;
    }


    GPIOA->CRL &= ~(
        (0xF << (2*4)) |  /* PA2 */
        (0xF << (5*4)) |  /* PA5 */
        (0xF << (6*4)) |  /* PA6 */
        (0xF << (7*4))    /* PA7 */
    );


    GPIOB->CRH &= ~(
        (0xF << ((10-8)*4)) | /* PB10 */
        (0xF << ((11-8)*4))   /* PB11 */
    );
    
    /* Configura o pino PC13 para Analog Input (alta impedância): 
       Evita dreno no LED ligado em Stop */
    GPIOC->CRH &= ~(0xF << 20); 


    RCC->APB1ENR &= ~(RCC_APB1ENR_USART2EN | RCC_APB1ENR_I2C2EN);
    RCC->APB2ENR &= ~(RCC_APB2ENR_ADC1EN | RCC_APB2ENR_SPI1EN);

	#ifdef FLASH_ACR_SLEEP_PD
    	FLASH->ACR |= FLASH_ACR_SLEEP_PD;
#endif
}

static void sleepmgr_restore_after_stop(void)
{
    RCC->APB1ENR |= (RCC_APB1ENR_USART2EN | RCC_APB1ENR_I2C2EN);
    RCC->APB2ENR |= (RCC_APB2ENR_ADC1EN  | RCC_APB2ENR_SPI1EN);

    /* Restaura pino PC13 para Output push-pull 2MHz */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->CRH &= ~GPIO_CRH_CNF13;
}


void sleepmgr_sleep_seconds(uint32_t seconds)
{
    g_rtc_alarm_fired = 0;

    uint32_t now = rtc_get_counter();
    rtc_set_alarm(now + seconds);

    g_diag.stop_entered = 0;
    g_diag.alarm_fired  = 0;
    g_diag.wfi_count    = 0;
    g_diag.rtc_before   = now;
    PWR->CR |= PWR_CR_CSBF;      /* limpa SBF antigo para o diagnóstico ser confiável */

    __enable_irq();

    PWR->CR |= PWR_CR_CWUF;

    RTC->CRL &= ~(RTC_CRL_ALRF);
    EXTI->PR = EXTI_PR_PR17;
    NVIC_ClearPendingIRQ(RTC_Alarm_IRQn);

    sleepmgr_prepare_for_stop();

    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    PWR->CR &= ~PWR_CR_PDDS;     /* Stop (e NÃO Standby) */
    PWR->CR |= PWR_CR_LPDS;      /* regulador em low-power no Stop */

    /* Só sai do loop quando o alarme RTC realmente disparou (ignora wakeups espúrios) */
    while (!g_rtc_alarm_fired) {
        __WFI();
        g_diag.wfi_count++;
        /* Ao acordar de um Stop real o hardware troca SYSCLK para HSI (SWS=00).
         * Se ainda estiver em PLL (SWS=10) o WFI virou só "Sleep" (clock não parou). */
        if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_HSI) {
            g_diag.stop_entered = 1;
        }
    }

    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

    Peripherals_Config_8MHz();

    sleepmgr_restore_after_stop();

    rtc_resync();
    g_diag.alarm_fired  = g_rtc_alarm_fired;
    g_diag.standby_flag = (PWR->CSR & PWR_CSR_SBF) ? 1U : 0U;
    g_diag.rtc_after    = rtc_get_counter();
}


uint8_t sleepmgr_last_wakeup_was_rtc(void)
{
    return g_rtc_alarm_fired;
}
