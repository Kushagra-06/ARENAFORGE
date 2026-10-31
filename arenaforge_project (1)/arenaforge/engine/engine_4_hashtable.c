/* ArenaForge - C DSA engine
 * Part 4 - Hash table: team-name search
 */
#include "arena_engine.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*4. HASH TABLE*/

static unsigned long hash_key(const char *s)       /* djb2, case-insensitive */
{
    unsigned long h = 5381;
    for (; *s; s++) h = ((h << 5) + h) + (unsigned char)tolower((unsigned char)*s);
    return h;
}

static int key_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

HashTable *ht_create(int nbuckets)
{
    if (nbuckets < 8) nbuckets = 8;
    HashTable *t = (HashTable *)malloc(sizeof *t);
    if (!t) return NULL;
    t->buckets = (HNode **)calloc((size_t)nbuckets, sizeof(HNode *));
    if (!t->buckets) { free(t); return NULL; }
    t->nbuckets = nbuckets;
    t->count = 0;
    return t;
}

int ht_put(HashTable *t, const char *key, int value)
{
    if (!t || !key) return 0;
    unsigned long b = hash_key(key) % (unsigned long)t->nbuckets;
    for (HNode *n = t->buckets[b]; n; n = n->next)
        if (key_eq(n->key, key)) { n->value = value; return 1; }

    HNode *n = (HNode *)malloc(sizeof *n);
    if (!n) return 0;
    size_t len = strlen(key) + 1;
    n->key = (char *)malloc(len);
    if (!n->key) { free(n); return 0; }
    memcpy(n->key, key, len);
    n->value = value;
    n->next = t->buckets[b];                 /* chaining: insert at head */
    t->buckets[b] = n;
    t->count++;
    return 1;
}

int ht_get(const HashTable *t, const char *key, int *out)
{
    if (!t || !key) return 0;
    unsigned long b = hash_key(key) % (unsigned long)t->nbuckets;
    for (HNode *n = t->buckets[b]; n; n = n->next)
        if (key_eq(n->key, key)) { if (out) *out = n->value; return 1; }
    return 0;
}

int ht_remove(HashTable *t, const char *key)
{
    if (!t || !key) return 0;
    unsigned long b = hash_key(key) % (unsigned long)t->nbuckets;
    HNode *prev = NULL;
    for (HNode *n = t->buckets[b]; n; prev = n, n = n->next)
        if (key_eq(n->key, key)) {
            if (prev) prev->next = n->next; else t->buckets[b] = n->next;
            free(n->key);
            free(n);
            t->count--;
            return 1;
        }
    return 0;
}

int ht_count(const HashTable *t) { return t ? t->count : 0; }

void ht_free(HashTable *t)
{
    if (!t) return;
    for (int i = 0; i < t->nbuckets; i++) {
        HNode *n = t->buckets[i];
        while (n) { HNode *nx = n->next; free(n->key); free(n); n = nx; }
    }
    free(t->buckets);
    free(t);
}
