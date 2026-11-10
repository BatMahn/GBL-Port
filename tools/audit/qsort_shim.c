/* qsort_shim - wc_qsort (src/shelldb.c) with the two comparators MW2SHELL sorts with, for tools/audit/qsort_diff.py:
 *   0x2b8a0 the Hall of Honor (rank +0x10, honor +0x14, missions +0xc, each descending)
 *   0x22db0 the debriefing's objectives (time +0xc; the first one's negative -> 1, else the second's -> -1, ascending)
 * Elements are 4-byte indices into the key arrays (the original sorts 4-byte pointers). */
#include "shelldb.h"
static const int *K0, *K1, *K2;
static int hoh(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    if (K1[y] < K1[x]) return -1;
    if (K1[x] < K1[y]) return 1;
    if (K2[y] < K2[x]) return -1;
    if (K2[x] < K2[y]) return 1;
    if (K0[y] < K0[x]) return -1;
    if (K0[x] < K0[y]) return 1;
    return 0;
}
static int obj(const void *a, const void *b)
{
    int x = K0[*(const int *)a], y = K0[*(const int *)b];
    if (x < 0) return 1;
    if (y < 0) return -1;
    return x > y ? 1 : x < y ? -1 : 0;
}
void shim_sort(int which, int *idx, int n, const int *k0, const int *k1, const int *k2)
{
    K0 = k0; K1 = k1; K2 = k2;
    wc_qsort(idx, (size_t)n, sizeof *idx, which ? obj : hoh);
}
