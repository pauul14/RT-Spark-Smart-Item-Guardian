#include "latency_stats.h"
#include <string.h>

void lat_reset(lat_ring_t *r) { memset(r, 0, sizeof(*r)); }

void lat_add(lat_ring_t *r, uint32_t value)
{
    r->v[r->count % LAT_RING] = value;
    r->count++;
}

/* Nearest-rank percentile: rank = ceil(p/100 * n), 1-based. */
static uint32_t rank_of(uint32_t n, uint32_t pct)
{
    uint32_t r = (pct * n + 99u) / 100u;
    return r == 0 ? 1 : r;
}

void lat_summarise(const lat_ring_t *r, uint32_t *s, lat_summary_t *out)
{
    uint32_t n = r->count < LAT_RING ? r->count : LAT_RING;
    memset(out, 0, sizeof(*out));
    out->n = n;
    out->total = r->count;
    if (n == 0) return;
    memcpy(s, r->v, n * sizeof(uint32_t));
    for (uint32_t i = 1; i < n; i++) {          /* insertion sort, n <= 256 */
        uint32_t x = s[i];
        uint32_t j = i;
        while (j > 0 && s[j - 1] > x) { s[j] = s[j - 1]; j--; }
        s[j] = x;
    }
    out->min = s[0];
    out->max = s[n - 1];
    out->median = s[rank_of(n, 50) - 1];
    out->p95 = s[rank_of(n, 95) - 1];
}
