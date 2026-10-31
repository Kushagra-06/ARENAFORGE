/*
 ArenaForge - C DSA Engine :-
 Pure C module used by the C++ application layer.
 1. Sorting: merge sort / quick sort   (ranking & seeding)
 2. Heap / Priority Q: min-heap of candidate pairs (matchmaking)
 3. Tree: knockout bracket (bracket tree)
 4. Hash table: team-name -> team-id lookup (fast search)
 5. Queue: registration / waitlist
 6. Linked list: match history
 7. Graph: match-conflict graph + greedy colouring (scheduling)
 */
#ifndef ARENA_ENGINE_H
#define ARENA_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#define AE_BYE (-1) /*empty bracket slot (team advances automatically)*/
#define AE_TBD (-2) /*winner not decided yet*/
#define AE_REPEAT_PENALTY 1000.0

/*1. SORTING*/
typedef struct {
    int    id;/*team id*/
    double score;/*rating / rank key*/
} RankEntry;

/*Both sorts order by score DESCENDING; ties are broken by lower id.*/
void ae_merge_sort(RankEntry *a, int n);
void ae_quick_sort(RankEntry *a, int n);

                                /*6. LINKED LIST*/
typedef struct HistNode {
    int match_id, round;
    int team_a, team_b;
    int score_a, score_b;
    int winner;
    struct HistNode *next;
} HistNode;

typedef struct {
    HistNode *head, *tail;
    int count;
} MatchHistory;

MatchHistory *history_create(void);
int history_append(MatchHistory *h, const HistNode *rec); /* copies rec, 1 = ok */
int history_meetings(const MatchHistory *h, int a, int b); /*times a met b*/
const HistNode *history_head(const MatchHistory *h);
int history_count(const MatchHistory *h);
void history_free(MatchHistory *h);

                                /*2. HEAP / PRIORITY QUEUE*/
typedef struct {
    int    team_a, team_b;
    double cost;/*Fair Matchmaking Score: LOWER = better match*/
} MatchPair;

typedef struct {
    MatchPair *data;
    int size, cap;
} MinHeap;

MinHeap *heap_create(int cap);
int heap_push(MinHeap *h, MatchPair p); /*1 = ok*/
int heap_pop(MinHeap *h, MatchPair *out); /*0 = heap is empty*/
int heap_size(const MinHeap *h);
void heap_free(MinHeap *h);

/*Fair Matchmaking Score = |ratingA - ratingB| + PENALTY * previous meetings*/
double ae_fair_score(double rating_a, double rating_b, int prev_meetings);

/*Pairs teams greedily from the cheapest candidate pair upward.
Returns number of pairs written to out (an odd team is left unpaired). */
int ae_matchmake(const RankEntry *teams, int n, const MatchHistory *hist,
                 MatchPair *out, int max_out);

                                    /*3. TREE (BRACKET)*/
typedef struct BracketNode {
    int node_id; /*>0 for matches, -1 for leaves*/
    int round; /*0 = leaf, 1 = first round, ...*/
    int team_id; /*team id, AE_BYE or AE_TBD*/
    struct BracketNode *left, *right;
} BracketNode;

BracketNode *bracket_build(const int *seeded_ids, int n);  /*seeded_ids[0] = seed 1*/
int bracket_collect_ready(BracketNode *root, BracketNode **out, int max);
int bracket_report_winner(BracketNode *root, int node_id, int winner); /*1 = ok*/
int bracket_champion(const BracketNode *root);    /*team id or AE_TBD*/
void bracket_free(BracketNode *root);

                                    /*4. HASH TABLE*/
typedef struct HNode {
    char *key;
    int   value;
    struct HNode *next;
} HNode;

typedef struct {
    HNode **buckets;
    int nbuckets, count;
} HashTable;

HashTable *ht_create(int nbuckets);
int ht_put(HashTable *t, const char *key, int value);/*insert/update*/
int ht_get(const HashTable *t, const char *key, int *out);/* 1 = found   */
int ht_remove(HashTable *t, const char *key);/*1 = removed*/
int ht_count(const HashTable *t);
void ht_free(HashTable *t);

                                        /*5. QUEUE*/
typedef struct QNode {
    int value;
    struct QNode *next;
} QNode;

typedef struct {
    QNode *head, *tail;
    int size;
} IntQueue;

IntQueue *queue_create(void);
int queue_enqueue(IntQueue *q, int v);
int queue_dequeue(IntQueue *q, int *out);/*0 = empty*/
int queue_peek(const IntQueue *q, int *out);
int queue_remove(IntQueue *q, int v);/*remove from the middle, 1 = removed*/
int queue_size(const IntQueue *q);
void queue_free(IntQueue *q);

                                        /*7. GRAPH*/
typedef struct {
    int n;/* number of vertices*/
    int *deg;/* degree of each vertex*/
    int *cap;/* capacity of each adj list*/
    int **adj;/* adjacency lists*/
} Graph;

Graph *graph_create(int n);
int    graph_add_edge(Graph *g, int u, int v);/* undirected, 1 = ok */
int    graph_has_edge(const Graph *g, int u, int v);
int    graph_degree(const Graph *g, int v);
/* Welsh-Powell greedy colouring. colors[v] in [0, k). Returns k (time slots needed). */
int    graph_color(const Graph *g, int *colors);
/* Vertex i = match i. Edge between two matches that share a team (they cannot
 * run in the same time slot). */
Graph *graph_build_conflicts(const int *team_a, const int *team_b, int m);
void   graph_free(Graph *g);

#ifdef __cplusplus
}
#endif
#endif /* ARENA_ENGINE_H */
