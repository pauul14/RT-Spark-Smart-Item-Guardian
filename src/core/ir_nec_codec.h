/*
 * ir_nec_codec.h - portable NEC infrared protocol core (encoder + decoder), host-tested.
 * NEC timing (0.5625 ms unit): 9 ms mark, 4.5 ms space, 32 bits LSB first
 * (addr, ~addr, cmd, ~cmd), bit = 0.5625 ms mark + 0.5625 ms (0) / 1.6875 ms (1)
 * space, stop mark. Repeat code: 9 ms mark, 2.25 ms space, stop mark.
 */
#ifndef SIG_IR_NEC_CODEC_H
#define SIG_IR_NEC_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#define IR_RELAY_ADDR      0x5Au   /* address used by this product's own alert relay frames */
#define IR_CMD_ROUGH       0x01u
#define IR_CMD_TEST_ALERT  0x02u   /* relayed when a user test alert is raised */
#define IR_CMD_FAULT       0x03u
#define IR_CMD_ACK         0x04u
#define IR_CMD_TEST        0x10u

typedef struct {
    uint16_t addr;      /* 8-bit address, or 16-bit for extended NEC */
    uint8_t  cmd;
    bool     repeat;    /* NEC repeat code (key held) */
    uint32_t t_us;      /* end-of-frame timestamp */
} ir_frame_t;

typedef struct {
    uint8_t  state;
    uint8_t  nbits;
    uint32_t bits;
    uint32_t errors;
} ir_nec_dec_t;

void ir_nec_reset(ir_nec_dec_t *d);
/* Feed one interval: its duration and the receiver level during it
 * (0 = mark/carrier present, 1 = space). Returns 1 for a frame, 2 for a repeat code. */
int  ir_nec_feed(ir_nec_dec_t *d, uint32_t dur_us, int level, ir_frame_t *out);
/* Alternating mark/space durations of one frame, starting with a mark and ending
 * with the stop mark followed by a 0 terminator. Returns the number of intervals (67). */
int  ir_nec_encode(uint16_t addr, uint8_t cmd, uint16_t *dur, int max);

#endif
