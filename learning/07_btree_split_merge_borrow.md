# 🎓 Topic #7: B-Tree Split, Merge, and Borrow

## Why This Matters for ArborDB
These are the **most complex operations** in your database. They maintain tree balance and ensure all nodes stay within capacity bounds.

---

## The Three Balancing Operations

| Operation | Trigger | Action |
|-----------|---------|--------|
| **Split** | Node > MAX | Divide node into two |
| **Merge** | Node < MIN | Combine two nodes |
| **Borrow** | Node < MIN, sibling has extra | Move key from sibling |

---

## 1. SPLIT - Node Overflow

**Trigger:** Node has more than MAX keys
- Leaf: > 13 keys
- Internal: > 510 keys

### Leaf Node Split

**Before split (14 keys, MAX = 13):**
```
Old Leaf: [1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27]
```

**After split:**
```
Old Leaf: [1, 3, 5, 7, 9, 11, 13]         (7 keys - SPLIT_LEFT)
New Leaf: [15, 17, 19, 21, 23, 25, 27]    (7 keys - SPLIT_RIGHT)
```

**Parent updated:**
```
Parent: [..., 13, ...]  ← Separator key (max of left node)
         ↓    ↓
      Old   New
```

### Your Implementation Pattern

```cpp
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* old_node = pager_get_page(pager, cursor->page_num);
    
    // 1. Allocate new node
    uint32_t new_page_num = get_unused_page_num(pager);
    void* new_node = pager_get_page(pager, new_page_num);
    initialize_leaf_node(new_node);
    
    // 2. Copy parent pointer
    *get_node_parent(new_node) = *get_node_parent(old_node);
    
    // 3. Update sibling link (for range queries)
    *get_leaf_node_next_leaf(new_node) = *get_leaf_node_next_leaf(old_node);
    *get_leaf_node_next_leaf(old_node) = new_page_num;
    
    // 4. Redistribute keys
    for (i = LEAF_NODE_MAX_CELLS; i >= 0; i--) {
        void* destination_node;
        if (i >= LEAF_NODE_LEFT_SPLIT_COUNT) {
            destination_node = new_node;
        } else {
            destination_node = old_node;
        }
        // Copy key/value to destination
    }
    
    // 5. Update parent
    internal_node_insert(parent, new_page_num);
}
```

### Root Split (Special Case)

**When root splits, tree grows taller:**

**Before:**
```
Root: [1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27]  (full!)
```

**After:**
```
         New Root: [13]
              /        \
    Old Root: [...]  New Node: [...]
```

**Implementation:**
```cpp
void create_new_root(Table* table, uint32_t right_child_page_num) {
    void* root = pager_get_page(pager, root_page_num);
    void* right_child = pager_get_page(pager, right_child_page_num);
    
    // 1. Allocate new page for old root's data
    uint32_t left_child_page_num = get_unused_page_num(pager);
    void* left_child = pager_get_page(pager, left_child_page_num);
    
    // 2. Copy old root to new left child
    memcpy(left_child, root, PAGE_SIZE);
    set_node_root(left_child, false);
    
    // 3. Root becomes internal node
    initialize_internal_node(root);
    set_node_root(root, true);
    *get_internal_node_num_keys(root) = 1;
    *get_internal_node_child(root, 0) = left_child_page_num;
    
    uint32_t left_child_max_key = get_node_max_key(pager, left_child);
    *get_internal_node_key(root, 0) = left_child_max_key;
    *get_internal_node_right_child(root) = right_child_page_num;
    
    // 4. Update parent pointers
    *get_node_parent(left_child) = root_page_num;
    *get_node_parent(right_child) = root_page_num;
}
```

---

## 2. MERGE - Node Underflow

**Trigger:** After delete, node has < MIN keys
- Leaf: < 6 keys
- Internal: < 255 keys

**Condition:** Sibling also at MIN (can't borrow)

### Merge Process

**Before (after deleting 9):**
```
Left:  [1, 3, 5]     (3 keys - UNDERFLOW!)
Right: [15, 17, 19]  (3 keys - at MIN)
Parent: [7, 13, ...]
         ↓   ↓
       Left Right
```

**After merge:**
```
Merged: [1, 3, 5, 15, 17, 19]  (6 keys - OK!)
Parent: [13, ...]  (separator 7 removed)
```

**Implementation:**
```cpp
void handle_leaf_underflow(Table* table, uint32_t page_num) {
    void* node = pager_get_page(pager, page_num);
    uint32_t num_keys = *get_leaf_node_num_cells(node);
    
    if (num_keys >= LEAF_NODE_MIN_CELLS) {
        return;  // Not underflow
    }
    
    // Try to borrow from sibling first
    if (can_borrow_from_sibling) {
        borrow_from_sibling();
        return;
    }
    
    // Must merge
    merge_with_sibling();
    
    // May cause parent underflow (recursive)
    handle_internal_underflow(table, parent_page_num);
}
```

---

## 3. BORROW - Transfer Key from Sibling

**Trigger:** Node underflows AND sibling has > MIN keys

**Condition:** Sibling can spare a key

### Borrow from Right Sibling

**Before (after deleting 5):**
```
Left:  [1, 3]           (2 keys - UNDERFLOW!)
Right: [15, 17, 19, 21] (4 keys - can spare 1)
Parent: [7, 13, ...]
         ↓   ↓
       Left Right
```

**After:**
```
Left:  [1, 3, 15]    (3 keys - OK!)
Right: [17, 19, 21]  (3 keys - OK!)
Parent: [7, 15, ...]  ← Separator updated!
```

### Borrow from Left Sibling

**Before:**
```
Left:  [1, 3, 5, 7]  (4 keys - can spare 1)
Right: [19, 21]      (2 keys - UNDERFLOW!)
Parent: [9, 17, ...]
         ↓   ↓
       Left Right
```

**After:**
```
Left:  [1, 3, 5]  (3 keys - OK!)
Right: [7, 19, 21]  (3 keys - OK!)
Parent: [9, 5, ...]  ← Separator updated!
```

---

## Cascading Operations

**Key insight:** Split/merge can propagate up the tree!

### Cascading Split
```
1. Insert into full leaf → leaf splits
2. Insert separator into parent → parent full → parent splits
3. Insert separator into grandparent → grandparent full → splits
4. Eventually reaches root → root splits → tree grows
```

### Cascading Merge
```
1. Delete from leaf → leaf underflows → merge
2. Remove separator from parent → parent underflows → merge
3. Remove separator from grandparent → underflows → merge
4. Eventually root has only 1 child → make child the new root → tree shrinks
```

---

## Critical Update: Parent Keys

**After split/merge/borrow, must update parent separator keys!**

```cpp
void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key) {
    uint32_t num_keys = *get_internal_node_num_keys(node);
    
    // Find and replace old key
    for (uint32_t i = 0; i < num_keys; i++) {
        if (*get_internal_node_key(node, i) == old_key) {
            *get_internal_node_key(node, i) = new_key;
            mark_page_dirty(pager, page_num);
            return;
        }
    }
}
```

**Pattern:**
1. Modify child node
2. Get new max key from child
3. Update separator in parent
4. Recursively update ancestors if needed

---

## Decision Tree for Delete

```
Delete key from leaf
   ↓
Leaf has >= MIN keys?
   ↓ NO (underflow)
   ↓
Has sibling with > MIN keys?
   ↓ YES → BORROW
   ↓ NO
   ↓
MERGE with sibling
   ↓
Parent loses a key → check parent underflow (recursive)
```

---

## Why 50% Fill Factor?

**MIN = MAX / 2:**
- Leaf: MIN = 6, MAX = 13
- Internal: MIN = 255, MAX = 510

**Benefits:**
1. **Prevents immediate re-split** after split
2. **Allows borrowing** (sibling likely has spare keys)
3. **Reduces cascading** operations
4. **Standard B-Tree property**

**Trade-off:**
- Wastes ~50% space in nodes
- But operations are simpler and faster

---

## Quick Self-Test

1. What triggers a split operation?
2. When do you merge vs borrow?
3. What happens when the root splits?
4. Why update parent keys after borrow?
5. Can a split cause another split?

**Answers:**
1. Node exceeds MAX capacity
2. Merge if sibling at MIN, borrow if sibling has extra
3. Tree grows by 1 level, new root created
4. Separator key changes when child max key changes
5. Yes! Splits can cascade up to root

---

**Ready for Topic #8: Serialization & Deserialization?** 🚀
