/* ArenaForge - C DSA engine
 * Part 1 - Sorting: merge sort and quick sort (ranking & seeding)
 */
#include "arena_engine.h"
#include <stdlib.h>
#include <string.h>

/*1. SORTING*/

/* strict ordering: higher score first, then lower id */
static int before(const RankEntry *x, const RankEntry *y)
{
    if (x->score != y->score) return x->score > y->score;
    return x->id < y->id;
}

static void msort(RankEntry *a, RankEntry *tmp, int lo, int hi) /* [lo, hi) */
{
    if (hi - lo < 2) return;
    int mid = lo + (hi - lo) / 2;
    msort(a, tmp, lo, mid);
    msort(a, tmp, mid, hi);

    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi)
        tmp[k++] = before(&a[j], &a[i]) ? a[j++] : a[i++];
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi)  tmp[k++] = a[j++];
    memcpy(a + lo, tmp + lo, (size_t)(hi - lo) * sizeof(RankEntry));
}

void ae_merge_sort(RankEntry *a, int n)
{
    if (!a || n < 2) return;
    RankEntry *tmp = (RankEntry *)malloc((size_t)n * sizeof(RankEntry));
    if (!tmp) return;
    msort(a, tmp, 0, n);
    free(tmp);
}

static void qsort_rec(RankEntry *a, int lo, int hi) /* [lo, hi] inclusive */
{
    while (lo < hi) {
        RankEntry p = a[lo + (hi - lo) / 2];
        int i = lo, j = hi;
        while (i <= j) {
            while (before(&a[i], &p)) i++;
            while (before(&p, &a[j])) j--;
            if (i <= j) {
                RankEntry t = a[i]; a[i] = a[j]; a[j] = t;
                i++; j--;
            }
        }
        /* recurse on the smaller half, loop on the larger (bounded stack) */
        if (j - lo < hi - i) { qsort_rec(a, lo, j); lo = i; }
        else                 { qsort_rec(a, i, hi); hi = j; }
    }
}

void ae_quick_sort(RankEntry *a, int n)
{
    if (!a || n < 2) return;
    qsort_rec(a, 0, n - 1);
}
