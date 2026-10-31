/* ArenaForge - C DSA engine
 * Part 2 - Heap / priority queue + smart matchmaking
 */
#include "arena_engine.h"
#include <stdlib.h>

/*2. HEAP / PRIORITY QUEUE*/

static int pair_less(const MatchPair *x, const MatchPair *y)
{
    if (x->cost != y->cost)       return x->cost < y->cost;
    if (x->team_a != y->team_a)   return x->team_a < y->team_a;
    return x->team_b < y->team_b;
}

MinHeap *heap_create(int cap)
{
    if (cap < 4) cap = 4;
    MinHeap *h = (MinHeap *)malloc(sizeof *h);
    if (!h) return NULL;
    h->data = (MatchPair *)malloc((size_t)cap * sizeof(MatchPair));
    if (!h->data) { free(h); return NULL; }
    h->size = 0;
    h->cap = cap;
    return h;
}

int heap_push(MinHeap *h, MatchPair p)
{
    if (!h) return 0;
    if (h->size == h->cap) {
        int nc = h->cap * 2;
        MatchPair *nd = (MatchPair *)realloc(h->data, (size_t)nc * sizeof(MatchPair));
        if (!nd) return 0;
        h->data = nd;
        h->cap = nc;
    }
    int i = h->size++;                      /* sift up */
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!pair_less(&p, &h->data[parent])) break;
        h->data[i] = h->data[parent];
        i = parent;
    }
    h->data[i] = p;
    return 1;
}

int heap_pop(MinHeap *h, MatchPair *out)
{
    if (!h || h->size == 0) return 0;
    *out = h->data[0];
    MatchPair last = h->data[--h->size];
    if (h->size == 0) return 1;

    int i = 0;/* sift down */
    for (;;) {
        int c = 2 * i + 1;
        if (c >= h->size) break;
        if (c + 1 < h->size && pair_less(&h->data[c + 1], &h->data[c])) c++;
        if (!pair_less(&h->data[c], &last)) break;
        h->data[i] = h->data[c];
        i = c;
    }
    h->data[i] = last;
    return 1;
}

int  heap_size(const MinHeap *h) { return h ? h->size : 0; }
void heap_free(MinHeap *h)       { if (h) { free(h->data); free(h); } }

double ae_fair_score(double ra, double rb, int prev)
{
    double diff = ra > rb ? ra - rb : rb - ra;
    return diff + AE_REPEAT_PENALTY * prev;
}

static int index_of(const RankEntry *t, int n, int id)
{
    for (int i = 0; i < n; i++) if (t[i].id == id) return i;
    return -1;
}

static double pair_cost(const RankEntry *t, int n, const MatchHistory *hist, int a, int b)
{
    return ae_fair_score(t[index_of(t, n, a)].score, t[index_of(t, n, b)].score,
                         history_meetings(hist, a, b));
}

int ae_matchmake(const RankEntry *t, int n, const MatchHistory *hist,
                 MatchPair *out, int max_out)
{
    if (!t || !out || n < 2 || max_out < 1) return 0;

    MinHeap *h = heap_create(n * (n - 1) / 2);
    char *used = (char *)calloc((size_t)n, 1);
    if (!h || !used) { heap_free(h); free(used); return 0; }

    /* every possible pairing goes into the heap, cheapest on top */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            MatchPair p;
            p.team_a = t[i].id;
            p.team_b = t[j].id;
            p.cost = ae_fair_score(t[i].score, t[j].score,
                                   history_meetings(hist, t[i].id, t[j].id));
            heap_push(h, p);
        }

    int count = 0;
    MatchPair p;
    while (count < max_out && heap_pop(h, &p)) {
        int ia = index_of(t, n, p.team_a), ib = index_of(t, n, p.team_b);
        if (used[ia] || used[ib]) continue;     /* a team plays once per round */
        used[ia] = used[ib] = 1;
        out[count++] = p;
    }
    heap_free(h);
    free(used);
    for (int pass = 0, improved = 1; improved && pass < 100; pass++) {
        improved = 0;
        for (int i = 0; i < count; i++)
            for (int j = i + 1; j < count; j++) {
                int a = out[i].team_a, b = out[i].team_b;
                int c = out[j].team_a, d = out[j].team_b;
                double cur = out[i].cost + out[j].cost;
                double ac = pair_cost(t, n, hist, a, c), bd = pair_cost(t, n, hist, b, d);
                double ad = pair_cost(t, n, hist, a, d), bc = pair_cost(t, n, hist, b, c);
                if (ac + bd < cur - 1e-9 && ac + bd <= ad + bc) {
                    out[i].team_a = a; out[i].team_b = c; out[i].cost = ac;
                    out[j].team_a = b; out[j].team_b = d; out[j].cost = bd;
                    improved = 1;
                } else if (ad + bc < cur - 1e-9) {
                    out[i].team_a = a; out[i].team_b = d; out[i].cost = ad;
                    out[j].team_a = b; out[j].team_b = c; out[j].cost = bc;
                    improved = 1;
                }
            }
    }
    return count;
}
