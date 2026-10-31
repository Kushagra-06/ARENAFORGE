/* ArenaForge - C DSA engine
 * Part 6 - Linked list: match history
 */
#include "arena_engine.h"
#include <stdlib.h>

/*6. LINKED LIST*/

MatchHistory *history_create(void)
{
    MatchHistory *h = (MatchHistory *)malloc(sizeof *h);
    if (!h) return NULL;
    h->head = h->tail = NULL;
    h->count = 0;
    return h;
}

int history_append(MatchHistory *h, const HistNode *rec)
{
    if (!h || !rec) return 0;
    HistNode *n = (HistNode *)malloc(sizeof *n);
    if (!n) return 0;
    *n = *rec;
    n->next = NULL;
    if (h->tail) h->tail->next = n; else h->head = n;
    h->tail = n;
    h->count++;
    return 1;
}

int history_meetings(const MatchHistory *h, int a, int b)
{
    int c = 0;
    if (!h) return 0;
    for (const HistNode *n = h->head; n; n = n->next)
        if ((n->team_a == a && n->team_b == b) || (n->team_a == b && n->team_b == a))
            c++;
    return c;
}

const HistNode *history_head(const MatchHistory *h) { return h ? h->head : NULL; }
int history_count(const MatchHistory *h)            { return h ? h->count : 0; }

void history_free(MatchHistory *h)
{
    if (!h) return;
    HistNode *n = h->head;
    while (n) { HistNode *nx = n->next; free(n); n = nx; }
    free(h);
}
