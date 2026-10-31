/* ArenaForge - C DSA engine
 * Part 7 - Graph: scheduling conflicts + colouring
 */
#include "arena_engine.h"
#include <stdlib.h>
#include <string.h>

/*7. GRAPH*/

Graph *graph_create(int n)
{
    if (n < 0) return NULL;
    Graph *g = (Graph *)malloc(sizeof *g);
    if (!g) return NULL;
    g->n = n;
    g->deg = (int *)calloc((size_t)(n ? n : 1), sizeof(int));
    g->cap = (int *)calloc((size_t)(n ? n : 1), sizeof(int));
    g->adj = (int **)calloc((size_t)(n ? n : 1), sizeof(int *));
    if (!g->deg || !g->cap || !g->adj) { graph_free(g); return NULL; }
    return g;
}

int graph_has_edge(const Graph *g, int u, int v)
{
    if (!g || u < 0 || v < 0 || u >= g->n || v >= g->n) return 0;
    for (int i = 0; i < g->deg[u]; i++) if (g->adj[u][i] == v) return 1;
    return 0;
}

static int push_adj(Graph *g, int u, int v)
{
    if (g->deg[u] == g->cap[u]) {
        int nc = g->cap[u] ? g->cap[u] * 2 : 4;
        int *na = (int *)realloc(g->adj[u], (size_t)nc * sizeof(int));
        if (!na) return 0;
        g->adj[u] = na;
        g->cap[u] = nc;
    }
    g->adj[u][g->deg[u]++] = v;
    return 1;
}

int graph_add_edge(Graph *g, int u, int v)
{
    if (!g || u == v || u < 0 || v < 0 || u >= g->n || v >= g->n) return 0;
    if (graph_has_edge(g, u, v)) return 1;
    return push_adj(g, u, v) && push_adj(g, v, u);
}

int graph_degree(const Graph *g, int v)
{
    return (g && v >= 0 && v < g->n) ? g->deg[v] : 0;
}

int graph_color(const Graph *g, int *colors)
{
    if (!g || !colors) return 0;
    int n = g->n;
    if (n == 0) return 0;

    int *order = (int *)malloc((size_t)n * sizeof(int));
    char *forbidden = (char *)malloc((size_t)n + 1);
    if (!order || !forbidden) { free(order); free(forbidden); return 0; }

    for (int i = 0; i < n; i++) { order[i] = i; colors[i] = -1; }
    for (int i = 1; i < n; i++) {                 /* insertion sort: degree desc */
        int v = order[i], j = i - 1;
        while (j >= 0 && g->deg[order[j]] < g->deg[v]) { order[j + 1] = order[j]; j--; }
        order[j + 1] = v;
    }

    int used = 0;
    for (int i = 0; i < n; i++) {
        int v = order[i];
        memset(forbidden, 0, (size_t)n + 1);
        for (int k = 0; k < g->deg[v]; k++) {
            int c = colors[g->adj[v][k]];
            if (c >= 0) forbidden[c] = 1;
        }
        int c = 0;
        while (forbidden[c]) c++;
        colors[v] = c;
        if (c + 1 > used) used = c + 1;
    }
    free(order);
    free(forbidden);
    return used;
}

Graph *graph_build_conflicts(const int *ta, const int *tb, int m)
{
    Graph *g = graph_create(m);
    if (!g) return NULL;
    for (int i = 0; i < m; i++)
        for (int j = i + 1; j < m; j++)
            if (ta[i] == ta[j] || ta[i] == tb[j] || tb[i] == ta[j] || tb[i] == tb[j])
                graph_add_edge(g, i, j);
    return g;
}

void graph_free(Graph *g)
{
    if (!g) return;
    if (g->adj) for (int i = 0; i < g->n; i++) free(g->adj[i]);
    free(g->adj);
    free(g->deg);
    free(g->cap);
    free(g);
}
