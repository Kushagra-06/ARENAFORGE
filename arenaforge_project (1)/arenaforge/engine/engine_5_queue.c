/* ArenaForge - C DSA engine
 * Part 5 - Queue: registration / waitlist
 */
#include "arena_engine.h"
#include <stdlib.h>

/*5. QUEUE*/

IntQueue *queue_create(void)
{
    IntQueue *q = (IntQueue *)malloc(sizeof *q);
    if (!q) return NULL;
    q->head = q->tail = NULL;
    q->size = 0;
    return q;
}

int queue_enqueue(IntQueue *q, int v)
{
    if (!q) return 0;
    QNode *n = (QNode *)malloc(sizeof *n);
    if (!n) return 0;
    n->value = v;
    n->next = NULL;
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
    q->size++;
    return 1;
}

int queue_dequeue(IntQueue *q, int *out)
{
    if (!q || !q->head) return 0;
    QNode *n = q->head;
    if (out) *out = n->value;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    free(n);
    q->size--;
    return 1;
}

int queue_peek(const IntQueue *q, int *out)
{
    if (!q || !q->head) return 0;
    if (out) *out = q->head->value;
    return 1;
}

int queue_remove(IntQueue *q, int v)
{
    if (!q) return 0;
    QNode *prev = NULL;
    for (QNode *n = q->head; n; prev = n, n = n->next)
        if (n->value == v) {
            if (prev) prev->next = n->next; else q->head = n->next;
            if (n == q->tail) q->tail = prev;
            free(n);
            q->size--;
            return 1;
        }
    return 0;
}

int queue_size(const IntQueue *q) { return q ? q->size : 0; }

void queue_free(IntQueue *q)
{
    if (!q) return;
    QNode *n = q->head;
    while (n) { QNode *nx = n->next; free(n); n = nx; }
    free(q);
}
