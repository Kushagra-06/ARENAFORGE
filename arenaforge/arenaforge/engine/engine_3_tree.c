/* ArenaForge - C DSA engine
 * Part 3 - Tree: knockout bracket
 */
#include "arena_engine.h"
#include <stdlib.h>

/*3. TREE (BRACKET)*/

static BracketNode *new_node(int id, int round, int team)
{
    BracketNode *n = (BracketNode *)malloc(sizeof *n);
    if (!n) return NULL;
    n->node_id = id; n->round = round; n->team_id = team;
    n->left = n->right = NULL;
    return n;
}

/* standard seeding order: 1 v 8, 4 v 5, 2 v 7, 3 v 6 ... */
static void seed_order(int *order, int p)
{
    order[0] = 1;
    for (int len = 1; len < p; len *= 2)
        for (int i = len - 1; i >= 0; i--) {
            order[2 * i + 1] = 2 * len + 1 - order[i];
            order[2 * i]     = order[i];
        }
}

BracketNode *bracket_build(const int *seeded, int n)
{
    if (!seeded || n < 2) return NULL;
    int p = 1;
    while (p < n) p *= 2;

    int *order = (int *)malloc((size_t)p * sizeof(int));
    BracketNode **level = (BracketNode **)malloc((size_t)p * sizeof(BracketNode *));
    if (!order || !level) { free(order); free(level); return NULL; }
    seed_order(order, p);

    for (int i = 0; i < p; i++)                  /* leaves */
        level[i] = new_node(-1, 0, order[i] <= n ? seeded[order[i] - 1] : AE_BYE);

    int next_id = 1, count = p, round = 1;
    while (count > 1) {                          /* build upward */
        for (int i = 0; i < count / 2; i++) {
            BracketNode *l = level[2 * i], *r = level[2 * i + 1];
            BracketNode *m = new_node(next_id++, round, AE_TBD);
            m->left = l; m->right = r;
            if (l->team_id == AE_BYE && r->team_id == AE_BYE) m->team_id = AE_BYE;
            else if (l->team_id == AE_BYE) m->team_id = r->team_id;   /* bye: advance */
            else if (r->team_id == AE_BYE) m->team_id = l->team_id;
            level[i] = m;
        }
        count /= 2;
        round++;
    }
    BracketNode *root = level[0];
    free(order);
    free(level);
    return root;
}

static int collect(BracketNode *n, BracketNode **out, int max, int k)
{
    if (!n || !n->left || !n->right) return k;        /* leaf */
    k = collect(n->left, out, max, k);
    k = collect(n->right, out, max, k);
    if (k < max && n->team_id == AE_TBD &&
        n->left->team_id >= 0 && n->right->team_id >= 0)
        out[k++] = n;
    return k;
}

int bracket_collect_ready(BracketNode *root, BracketNode **out, int max)
{
    return collect(root, out, max, 0);
}

static BracketNode *find_node(BracketNode *n, int id)
{
    if (!n) return NULL;
    if (n->node_id == id) return n;
    BracketNode *f = find_node(n->left, id);
    return f ? f : find_node(n->right, id);
}

int bracket_report_winner(BracketNode *root, int node_id, int winner)
{
    BracketNode *n = find_node(root, node_id);
    if (!n || !n->left || !n->right || n->team_id != AE_TBD) return 0;
    if (n->left->team_id < 0 || n->right->team_id < 0)     return 0;
    if (winner != n->left->team_id && winner != n->right->team_id) return 0;
    n->team_id = winner;                                    /* winner moves up */
    return 1;
}

int bracket_champion(const BracketNode *root)
{
    return (root && root->team_id >= 0) ? root->team_id : AE_TBD;
}

void bracket_free(BracketNode *root)
{
    if (!root) return;
    bracket_free(root->left);
    bracket_free(root->right);
    free(root);
}
