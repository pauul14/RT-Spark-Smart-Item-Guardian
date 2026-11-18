/*
 * ir_nec_codec.c - portable NEC encoder/decoder core (no HAL), unit-tested on the host.
 * Tolerances are wide (about +/-35 %) because the IRM-H638T stretches/shrinks marks
 * by up to ~100 us and remotes differ.
 */
#include "ir_nec_codec.h"
#include <string.h>

enum { S_IDLE = 0, S_LEAD_SPACE, S_BIT_MARK, S_BIT_SPACE, S_REPEAT_MARK };

static inline bool in(uint32_t v, uint32_t lo, uint32_t hi) { return v >= lo && v <= hi; }

void ir_nec_reset(ir_nec_dec_t *d) { uint32_t e = d->errors; memset(d, 0, sizeof *d); d->errors = e; }

int ir_nec_feed(ir_nec_dec_t *d, uint32_t dur, int level, ir_frame_t *out)
{
    switch (d->state) {
    case S_IDLE:
        if (level == 0 && in(dur, 7000, 11000)) d->state = S_LEAD_SPACE;
        return 0;
    case S_LEAD_SPACE:
        if (level == 1 && in(dur, 3500, 5500)) { d->state = S_BIT_MARK; d->nbits = 0; d->bits = 0; return 0; }
        if (level == 1 && in(dur, 1700, 2800)) { d->state = S_REPEAT_MARK; return 0; }
        d->errors++;
        ir_nec_reset(d);
        return 0;
    case S_REPEAT_MARK:
        ir_nec_reset(d);
        if (level == 0 && in(dur, 300, 900)) {
            memset(out, 0, sizeof *out);
            out->repeat = true;
            return 2;
        }
        return 0;
    case S_BIT_MARK:
        if (level == 0 && in(dur, 300, 900)) {
            if (d->nbits == 32) {                         /* stop mark */
                uint8_t a = (uint8_t)d->bits, na = (uint8_t)(d->bits >> 8);
                uint8_t c = (uint8_t)(d->bits >> 16), nc = (uint8_t)(d->bits >> 24);
                ir_nec_reset(d);
                uint8_t c_inv = (uint8_t)(0xFFu ^ c), a_inv = (uint8_t)(0xFFu ^ a);
                if (c_inv != nc) { d->errors++; return 0; }
                memset(out, 0, sizeof *out);
                out->addr = (a_inv == na) ? a : (uint16_t)(a | (na << 8));        /* extended NEC */
                out->cmd = c;
                return 1;
            }
            d->state = S_BIT_SPACE;
            return 0;
        }
        d->errors++;
        ir_nec_reset(d);
        return 0;
    case S_BIT_SPACE:
        if (level == 1 && in(dur, 300, 900))   { d->nbits++; d->state = S_BIT_MARK; return 0; }
        if (level == 1 && in(dur, 1200, 2200)) { d->bits |= 1u << d->nbits; d->nbits++; d->state = S_BIT_MARK; return 0; }
        d->errors++;
        ir_nec_reset(d);
        return 0;
    default:
        ir_nec_reset(d);
        return 0;
    }
}

int ir_nec_encode(uint16_t addr, uint8_t cmd, uint16_t *dur, int max)
{
    if (max < 68) return 0;
    uint32_t a = addr <= 0xFF ? (uint32_t)(addr | ((uint8_t)~addr << 8)) : addr;
    uint32_t bits = a | ((uint32_t)cmd << 16) | ((uint32_t)(uint8_t)~cmd << 24);
    int n = 0;
    dur[n++] = 9000;
    dur[n++] = 4500;
    for (int i = 0; i < 32; i++) {
        dur[n++] = 562;
        dur[n++] = (bits >> i) & 1u ? 1687 : 562;
    }
    dur[n++] = 562;                                     /* stop mark */
    dur[n++] = 0;                                       /* end marker (carrier off) */
    return n - 1;                                       /* 66 timed intervals + stop mark = 67 */
}
