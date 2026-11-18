/*
 * latency_stats.h - fixed-size ring of timing samples with min/median/p95/max.
 * Portable; the caller provides mutual exclusion if several tasks touch it.
 */
#ifndef SIG_LATENCY_STATS_H
#define SIG_LATENCY_STATS_H

#include <stdint.h>

#define LAT_RING 256

typedef struct {
    uint32_t v[LAT_RING];
    uint32_t count;      /* total samples ever added */
} lat_ring_t;

typedef struct {
    uint32_t n;          /* samples summarised (<= LAT_RING) */
    uint32_t total;      /* samples ever recorded */
    uint32_t min, median, p95, max;
} lat_summary_t;

void lat_reset(lat_ring_t *r);
void lat_add(lat_ring_t *r, uint32_t value);
/* scratch must hold LAT_RING entries; nearest-rank percentiles on the last n samples. */
void lat_summarise(const lat_ring_t *r, uint32_t *scratch, lat_summary_t *out);

#endif
