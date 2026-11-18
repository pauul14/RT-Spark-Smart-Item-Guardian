/*
 * wired_link.h - opt-in wired telemetry to a building/shelf hub (product service function).
 *
 * CAN1 (PB8 RX / PB9 TX, TJA1042, CN8) at 500 kbit/s and RS485 (USART6 PG14 TX /
 * PG9 RX, SP3485 RE/DE on PG8, CN7) at 115200 8N1 carry the same information:
 *   - one record per product event (rough handling, test alert, gesture, ack, fault, ...)
 *   - a heartbeat every 5 s (shown state, alert, acknowledged, rough count, fault)
 * RS485 also answers the ASCII query "?STATUS" with an immediate heartbeat line.
 * Both outputs are OFF by default because they drive external bus lines; the
 * setting is kept in an RTC backup register ("wire can on", "wire rs485 on").
 *
 * CAN frames (standard IDs, DLC 8):
 *   0x5A0+event  [event, class, value_lo, value_hi, uptime_ms LE x4]
 *   0x5AF        [shown_class, alert, alert_acked, 0, rough_lo, rough_hi, sensor_fault, seq]
 * RS485 lines (NMEA-style XOR checksum over the characters between '$' and '*'):
 *   $SIGEV,<uptime_ms>,<EVENT>,<CLASS>,<value>*hh\r\n
 *   $SIGHB,<uptime_ms>,<SHOWN>,<ALERT>,<acked>,<rough_events>,<sensor_fault>*hh\r\n
 * Without a bus partner, CAN frames are not acknowledged (counted as ACK errors) and
 * RS485 output cannot be observed by the board itself: physical verification needs
 * a second node or a USB-CAN/USB-RS485 adapter.
 */
#ifndef SIG_WIRED_LINK_H
#define SIG_WIRED_LINK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     can_on, rs485_on;
    uint32_t can_tx_ok, can_tx_fail, can_ack_err, can_busy;
    uint8_t  can_tec, can_rec, can_lec;
    bool     can_error_passive, can_bus_off;
    uint32_t rs485_tx_lines, rs485_rx_bytes, rs485_queries;
} wired_stats_t;

void wired_init(bool can_on, bool rs485_on);     /* (re)configure per the flags */
void wired_reinit(void);                         /* after the CAN loop-back self-test */
void wired_event(uint8_t type, const char *type_name, uint8_t cls, const char *cls_name,
                 uint16_t value, uint32_t t_ms);
void wired_heartbeat(uint8_t shown, const char *shown_name, uint8_t alert, const char *alert_name, bool acked,
                     uint32_t rough, bool fault, uint32_t t_ms);
bool wired_rs485_query_pending(void);            /* "?STATUS" received since last call */
void wired_stats(wired_stats_t *s);
uint8_t wired_nmea_checksum(const char *body);   /* XOR of all characters (exported for tests) */

#endif
