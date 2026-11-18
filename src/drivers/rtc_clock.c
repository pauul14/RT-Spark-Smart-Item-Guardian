#include "rtc_clock.h"
#include "bsp.h"

static RTC_HandleTypeDef s_rtc;
static rtc_src_t s_src = RTC_SRC_NONE;
#define RTC_SET_FLAG  (1u << 1)        /* in BKP_FLAGS: calendar set via `time` */

const char *rtc_src_name(rtc_src_t s)
{
    return s == RTC_SRC_LSE ? "LSE" : (s == RTC_SRC_LSI ? "LSI" : "none");
}

rtc_src_t rtc_init(bool *was_running)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    *was_running = false;

    uint32_t bdcr = RCC->BDCR;
    bool lse_running = (bdcr & RCC_BDCR_LSERDY) && (bdcr & RCC_BDCR_RTCEN) &&
                       ((bdcr & RCC_BDCR_RTCSEL) == RCC_BDCR_RTCSEL_0);
    s_rtc.Instance = RTC;
    s_rtc.Init.HourFormat = RTC_HOURFORMAT_24;
    s_rtc.Init.AsynchPrediv = 127;          /* 32768 / 128 / 256 = 1 Hz */
    s_rtc.Init.SynchPrediv = 255;
    s_rtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    s_rtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    s_rtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

    if (lse_running) {
        /* Keep the running calendar untouched; only attach the handle. */
        s_rtc.State = HAL_RTC_STATE_READY;
        *was_running = true;
        s_src = RTC_SRC_LSE;
        return s_src;
    }
    uint32_t sel = bdcr & RCC_BDCR_RTCSEL;
    if (sel != 0) {
        /* A clock source is already selected (earlier firmware). Changing RTCSEL would
         * need a backup-domain reset (RM0090 5.3.20) that erases the calendar and backup
         * registers, so the SELECTED source is (re)started instead: oscillator on, RTC
         * clock enabled, prescalers set only if the calendar was never initialised.
         * Hardware finding 2026-11-18: this board had RTCSEL already set but the RTC was
         * not running; the first firmware version returned "none" here. */
        RCC_OscInitTypeDef o = {0};
        o.PLL.PLLState = RCC_PLL_NONE;
        if (sel == RCC_BDCR_RTCSEL_0) {                       /* LSE */
            if (!(bdcr & RCC_BDCR_LSERDY)) {
                o.OscillatorType = RCC_OSCILLATORTYPE_LSE;
                o.LSEState = RCC_LSE_ON;
                if (HAL_RCC_OscConfig(&o) != HAL_OK) return RTC_SRC_NONE;   /* crystal not starting */
            }
            s_src = RTC_SRC_LSE;
        } else if (sel == RCC_BDCR_RTCSEL_1) {                /* LSI */
            o.OscillatorType = RCC_OSCILLATORTYPE_LSI;
            o.LSIState = RCC_LSI_ON;
            if (HAL_RCC_OscConfig(&o) != HAL_OK) return RTC_SRC_NONE;
            s_rtc.Init.SynchPrediv = 249;
            s_src = RTC_SRC_LSI;
        } else {
            return RTC_SRC_NONE;                              /* HSE/128: reported as unusable */
        }
        *was_running = (bdcr & RCC_BDCR_RTCEN) != 0;
        if (!(RCC->BDCR & RCC_BDCR_RTCEN)) __HAL_RCC_RTC_ENABLE();
        if (!(RTC->ISR & RTC_ISR_INITS)) {
            if (HAL_RTC_Init(&s_rtc) != HAL_OK) { s_src = RTC_SRC_NONE; return s_src; }
        } else {
            s_rtc.State = HAL_RTC_STATE_READY;
        }
        return s_src;
    }

    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState = RCC_LSE_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    bool lse_ok = HAL_RCC_OscConfig(&osc) == HAL_OK;
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    if (lse_ok) {
        pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    } else {
        osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
        osc.LSEState = RCC_LSE_OFF;
        osc.LSIState = RCC_LSI_ON;
        if (HAL_RCC_OscConfig(&osc) != HAL_OK) return RTC_SRC_NONE;
        pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
        s_rtc.Init.AsynchPrediv = 127;       /* LSI ~32 kHz: approximate 1 Hz */
        s_rtc.Init.SynchPrediv = 249;
    }
    /* RTCSEL is still 0 here (never selected), so selecting it needs no backup reset. */
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) return RTC_SRC_NONE;
    __HAL_RCC_RTC_ENABLE();
    if (HAL_RTC_Init(&s_rtc) != HAL_OK) return RTC_SRC_NONE;
    s_src = lse_ok ? RTC_SRC_LSE : RTC_SRC_LSI;
    return s_src;
}

bool rtc_get(rtc_time_t *t)
{
    if (s_src == RTC_SRC_NONE) return false;
    RTC_TimeTypeDef tm;
    RTC_DateTypeDef dt;
    if (HAL_RTC_GetTime(&s_rtc, &tm, RTC_FORMAT_BIN) != HAL_OK) return false;
    if (HAL_RTC_GetDate(&s_rtc, &dt, RTC_FORMAT_BIN) != HAL_OK) return false;   /* unlocks shadow regs */
    t->year = (uint16_t)(2000u + dt.Year);
    t->mon = dt.Month;
    t->day = dt.Date;
    t->hour = tm.Hours;
    t->min = tm.Minutes;
    t->sec = tm.Seconds;
    return true;
}

bool rtc_set(const rtc_time_t *t)
{
    if (s_src == RTC_SRC_NONE || t->year < 2000 || t->year > 2099) return false;
    RTC_TimeTypeDef tm = {0};
    RTC_DateTypeDef dt = {0};
    tm.Hours = t->hour; tm.Minutes = t->min; tm.Seconds = t->sec;
    tm.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    tm.StoreOperation = RTC_STOREOPERATION_RESET;
    dt.Year = (uint8_t)(t->year - 2000u); dt.Month = t->mon; dt.Date = t->day;
    dt.WeekDay = RTC_WEEKDAY_MONDAY;        /* not used */
    if (HAL_RTC_SetTime(&s_rtc, &tm, RTC_FORMAT_BIN) != HAL_OK) return false;
    if (HAL_RTC_SetDate(&s_rtc, &dt, RTC_FORMAT_BIN) != HAL_OK) return false;
    bkp_write(BKP_FLAGS, bkp_read(BKP_FLAGS) | RTC_SET_FLAG);
    return true;
}

bool rtc_time_valid(void)
{
    return s_src != RTC_SRC_NONE && bkp_read(BKP_MAGIC) == BKP_MAGIC_VALUE && (bkp_read(BKP_FLAGS) & RTC_SET_FLAG);
}

uint32_t bkp_read(uint32_t idx)
{
    if (idx > 19) return 0;
    return (&RTC->BKP0R)[idx];
}

void bkp_write(uint32_t idx, uint32_t v)
{
    if (idx > 19) return;
    HAL_PWR_EnableBkUpAccess();
    (&RTC->BKP0R)[idx] = v;
}
