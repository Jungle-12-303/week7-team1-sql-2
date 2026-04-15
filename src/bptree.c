#include "storage.h"

#include <stdlib.h>
#include <string.h>

#define BPTREE_MAX_KEYS 31

typedef struct BptNode BptNode;

struct BptNode {
    bool is_leaf;
    size_t size;
    uint64_t keys[BPTREE_MAX_KEYS];
    union {
        struct {
            RowRef refs[BPTREE_MAX_KEYS];
            BptNode *next;
        } leaf;
        struct {
            BptNode *children[BPTREE_MAX_KEYS + 1];
        } internal;
    } as;
};

typedef struct {
    bool split;
    uint64_t promoted_key;
    BptNode *right;
} BptSplitResult;

static BptNode *g_index_root = NULL;
static bool g_index_ready = false;

static BptNode *bpt_create_node(bool is_leaf) {
    BptNode *node = (BptNode *) calloc(1, sizeof(BptNode));
    if (node == NULL) {
        return NULL;
    }
    node->is_leaf = is_leaf;
    node->size = 0;
    node->as.leaf.next = NULL;
    return node;
}

static void bpt_free_node(BptNode *node) {
    size_t i;

    if (node == NULL) {
        return;
    }

    if (!node->is_leaf) {
        for (i = 0; i <= node->size; ++i) {
            bpt_free_node(node->as.internal.children[i]);
        }
    }

    free(node);
}

static int bpt_find(BptNode *root, uint64_t id, RowRef *out_ref) {
    BptNode *node = root;
    size_t i;

    while (!node->is_leaf) {
        i = 0;
        while (i < node->size && id >= node->keys[i]) {
            i++;
        }
        node = node->as.internal.children[i];
        if (node == NULL) {
            return -1;
        }
    }

    for (i = 0; i < node->size; ++i) {
        if (node->keys[i] == id) {
            *out_ref = node->as.leaf.refs[i];
            return 0;
        }
    }

    return 1;
}

static int bpt_insert_recursive(BptNode *node, uint64_t id, RowRef ref, BptSplitResult *out_split) {
    size_t i;

    memset(out_split, 0, sizeof(*out_split));

    if (node->is_leaf) {
        uint64_t merged_keys[BPTREE_MAX_KEYS + 1];
        RowRef merged_refs[BPTREE_MAX_KEYS + 1];
        size_t insert_at = 0;
        BptNode *right;
        size_t total;
        size_t left_count;
        size_t right_count;

        while (insert_at < node->size && node->keys[insert_at] < id) {
            insert_at++;
        }

        if (insert_at < node->size && node->keys[insert_at] == id) {
            return 1;
        }

        if (node->size < BPTREE_MAX_KEYS) {
            for (i = node->size; i > insert_at; --i) {
                node->keys[i] = node->keys[i - 1];
                node->as.leaf.refs[i] = node->as.leaf.refs[i - 1];
            }
            node->keys[insert_at] = id;
            node->as.leaf.refs[insert_at] = ref;
            node->size++;
            return 0;
        }

        total = node->size + 1;
        for (i = 0; i < insert_at; ++i) {
            merged_keys[i] = node->keys[i];
            merged_refs[i] = node->as.leaf.refs[i];
        }
        merged_keys[insert_at] = id;
        merged_refs[insert_at] = ref;
        for (i = insert_at; i < node->size; ++i) {
            merged_keys[i + 1] = node->keys[i];
            merged_refs[i + 1] = node->as.leaf.refs[i];
        }

        left_count = total / 2;
        right_count = total - left_count;
        right = bpt_create_node(true);
        if (right == NULL) {
            return -1;
        }

        for (i = 0; i < left_count; ++i) {
            node->keys[i] = merged_keys[i];
            node->as.leaf.refs[i] = merged_refs[i];
        }
        node->size = left_count;

        for (i = 0; i < right_count; ++i) {
            right->keys[i] = merged_keys[left_count + i];
            right->as.leaf.refs[i] = merged_refs[left_count + i];
        }
        right->size = right_count;

        right->as.leaf.next = node->as.leaf.next;
        node->as.leaf.next = right;

        out_split->split = true;
        out_split->promoted_key = right->keys[0];
        out_split->right = right;
        return 0;
    }

    i = 0;
    while (i < node->size && id >= node->keys[i]) {
        i++;
    }

    {
        BptSplitResult child_split;
        int rc = bpt_insert_recursive(node->as.internal.children[i], id, ref, &child_split);
        if (rc != 0) {
            return rc;
        }

        if (!child_split.split) {
            return 0;
        }

        if (node->size < BPTREE_MAX_KEYS) {
            size_t j;

            for (j = node->size; j > i; --j) {
                node->keys[j] = node->keys[j - 1];
            }
            for (j = node->size + 1; j > i + 1; --j) {
                node->as.internal.children[j] = node->as.internal.children[j - 1];
            }

            node->keys[i] = child_split.promoted_key;
            node->as.internal.children[i + 1] = child_split.right;
            node->size++;
            return 0;
        }

        {
            uint64_t merged_keys[BPTREE_MAX_KEYS + 1];
            BptNode *merged_children[BPTREE_MAX_KEYS + 2];
            BptNode *right;
            size_t total_keys;
            size_t mid;
            size_t left_keys;
            size_t right_keys;
            size_t j;

            for (j = 0; j < i; ++j) {
                merged_keys[j] = node->keys[j];
            }
            merged_keys[i] = child_split.promoted_key;
            for (j = i; j < node->size; ++j) {
                merged_keys[j + 1] = node->keys[j];
            }

            for (j = 0; j <= i; ++j) {
                merged_children[j] = node->as.internal.children[j];
            }
            merged_children[i + 1] = child_split.right;
            for (j = i + 1; j <= node->size; ++j) {
                merged_children[j + 1] = node->as.internal.children[j];
            }

            total_keys = node->size + 1;
            mid = total_keys / 2;
            left_keys = mid;
            right_keys = total_keys - mid - 1;

            right = bpt_create_node(false);
            if (right == NULL) {
                return -1;
            }

            for (j = 0; j < left_keys; ++j) {
                node->keys[j] = merged_keys[j];
                node->as.internal.children[j] = merged_children[j];
            }
            node->as.internal.children[left_keys] = merged_children[left_keys];
            node->size = left_keys;

            for (j = 0; j < right_keys; ++j) {
                right->keys[j] = merged_keys[mid + 1 + j];
                right->as.internal.children[j] = merged_children[mid + 1 + j];
            }
            right->as.internal.children[right_keys] = merged_children[total_keys];
            right->size = right_keys;

            out_split->split = true;
            out_split->promoted_key = merged_keys[mid];
            out_split->right = right;
            return 0;
        }
    }
}

int index_init(void) {
    bpt_free_node(g_index_root);
    g_index_root = bpt_create_node(true);
    if (g_index_root == NULL) {
        g_index_ready = false;
        return -1;
    }
    g_index_ready = true;
    return 0;
}

int index_insert(uint64_t id, RowRef ref) {
    BptSplitResult split;
    int rc;

    if (!g_index_ready || g_index_root == NULL) {
        return -1;
    }

    rc = bpt_insert_recursive(g_index_root, id, ref, &split);
    if (rc != 0) {
        return rc;
    }

    if (split.split) {
        BptNode *new_root = bpt_create_node(false);
        if (new_root == NULL) {
            return -1;
        }
        new_root->keys[0] = split.promoted_key;
        new_root->size = 1;
        new_root->as.internal.children[0] = g_index_root;
        new_root->as.internal.children[1] = split.right;
        g_index_root = new_root;
    }

    return 0;
}

int index_find(uint64_t id, RowRef *out_ref) {
    if (!g_index_ready || g_index_root == NULL) {
        return -1;
    }

    return bpt_find(g_index_root, id, out_ref);
}

bool index_is_ready(void) {
    return g_index_ready && g_index_root != NULL;
}
