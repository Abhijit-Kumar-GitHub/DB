# 🎓 ArborDB Viva/Interview Questions - Part 4: Split, Merge, Borrow

## Overview
This part covers the most complex B-Tree operations: splitting full nodes, merging underflowing nodes, and borrowing from siblings.

---

## 🔄 Section 4: Split, Merge, Borrow Operations (15 Questions)

### Q53: When and why do we need to split a node?
**Answer:**

**When:** Node reaches maximum capacity

**Capacity limits:**
- Leaf node: 13 cells max
- Internal node: 510 keys max

**Why split?**
- Maintain B-Tree invariant: nodes must be within [min, max] bounds
- Keep tree balanced
- Prevent overflow

**Trigger:**
```cpp
// In execute_insert():
uint32_t num_cells = *get_leaf_node_num_cells(node);

if (num_cells >= LEAF_NODE_MAX_CELLS) {
    // FULL! Must split before inserting
    leaf_node_split_and_insert(cursor, key, row_to_insert);
} else {
    // Space available, simple insert
    leaf_node_insert(cursor, key, row_to_insert);
}
```

**Visual example:**

**Before insert (FULL):**
```
Leaf: [10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130]
      ↑ 13 cells (LEAF_NODE_MAX_CELLS)
```

**Insert key=135:**
```
Can't fit! Must split first.
```

**After split:**
```
Left:  [10, 20, 30, 40, 50, 60, 70]     ← 7 cells
Right: [80, 90, 100, 110, 120, 130, 135] ← 7 cells (includes new key)

Parent updated with separator key (80)
```

**Split threshold calculation:**
```cpp
#define LEAF_NODE_RIGHT_SPLIT_COUNT ((LEAF_NODE_MAX_CELLS + 1) / 2)
#define LEAF_NODE_LEFT_SPLIT_COUNT ((LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT)

// With 13 max cells:
// Left gets: (13+1) - 7 = 7 cells
// Right gets: (13+1)/2 = 7 cells
// Total: 14 (13 old + 1 new)
```

---

### Q54: Explain leaf_node_split_and_insert() step by step.
**Answer:**

**High-level algorithm:**
```
1. Allocate new node
2. Move half the cells to new node
3. Insert new key into appropriate node
4. Update parent
5. Update sibling links
```

**Detailed code:**

```cpp
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
    // Step 1: Get new page
    void* old_node = pager_get_page(cursor->table->pager, cursor->page_num);
    uint32_t new_page_num = pager_get_unused_page_num(cursor->table->pager);
    void* new_node = pager_get_page(cursor->table->pager, new_page_num);
    initialize_leaf_node(new_node);
    
    // Step 2: Set up parent pointer
    *get_node_parent(new_node) = *get_node_parent(old_node);
    
    // Step 3: Update sibling links
    uint32_t old_next = *get_leaf_node_next_leaf(old_node);
    *get_leaf_node_next_leaf(new_node) = old_next;
    *get_leaf_node_next_leaf(old_node) = new_page_num;
    
    // Step 4: Distribute cells
    // Create temporary array to hold all cells (old + new)
    uint32_t all_keys[LEAF_NODE_MAX_CELLS + 1];
    Row all_values[LEAF_NODE_MAX_CELLS + 1];
    
    // Copy existing cells
    for (uint32_t i = 0; i < LEAF_NODE_MAX_CELLS; i++) {
        all_keys[i] = *get_leaf_node_key(old_node, i);
        deserialize_row(get_leaf_node_value(old_node, i), &all_values[i]);
    }
    
    // Insert new key/value at correct position
    uint32_t insert_index = cursor->cell_num;
    for (uint32_t i = LEAF_NODE_MAX_CELLS; i > insert_index; i--) {
        all_keys[i] = all_keys[i - 1];
        all_values[i] = all_values[i - 1];
    }
    all_keys[insert_index] = key;
    all_values[insert_index] = *value;
    
    // Step 5: Split the cells
    // Left node gets first LEAF_NODE_LEFT_SPLIT_COUNT cells
    *get_leaf_node_num_cells(old_node) = LEAF_NODE_LEFT_SPLIT_COUNT;
    for (uint32_t i = 0; i < LEAF_NODE_LEFT_SPLIT_COUNT; i++) {
        *get_leaf_node_key(old_node, i) = all_keys[i];
        serialize_row(&all_values[i], get_leaf_node_value(old_node, i));
    }
    
    // Right node gets remaining cells
    *get_leaf_node_num_cells(new_node) = LEAF_NODE_RIGHT_SPLIT_COUNT;
    for (uint32_t i = 0; i < LEAF_NODE_RIGHT_SPLIT_COUNT; i++) {
        *get_leaf_node_key(new_node, i) = all_keys[LEAF_NODE_LEFT_SPLIT_COUNT + i];
        serialize_row(&all_values[LEAF_NODE_LEFT_SPLIT_COUNT + i], 
                     get_leaf_node_value(new_node, i));
    }
    
    // Step 6: Mark pages dirty
    mark_page_dirty(cursor->table->pager, cursor->page_num);
    mark_page_dirty(cursor->table->pager, new_page_num);
    
    // Step 7: Update parent
    if (is_node_root(old_node)) {
        create_new_root(cursor->table, new_page_num);
    } else {
        uint32_t parent_page_num = *get_node_parent(old_node);
        uint32_t new_max = get_node_max_key(cursor->table->pager, old_node);
        void* parent = pager_get_page(cursor->table->pager, parent_page_num);
        internal_node_insert(cursor->table, parent_page_num, new_page_num);
    }
}
```

**Visual trace:**

**Initial state (inserting 135):**
```
Old: [10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130]
     13 cells, insert_index = 13
```

**Temporary array:**
```
All: [10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 135]
     14 cells total
```

**After split:**
```
Left:  [10, 20, 30, 40, 50, 60, 70]  ← cells 0-6
Right: [80, 90, 100, 110, 120, 130, 135]  ← cells 7-13

Sibling links: Left → Right → (old next)

Parent gets: new child (Right) with max key 135
```

---

### Q55: What is create_new_root() and when is it called?
**Answer:**

**When called:** Root node splits (tree grows taller)

**Purpose:** Create new root to maintain single-root property

**Algorithm:**
```
1. Allocate new root page
2. Initialize as internal node
3. Set old root as left child
4. Set new sibling as right child
5. Update parent pointers
6. Copy max key from left child
```

**Code:**

```cpp
void create_new_root(Table* table, uint32_t right_child_page_num) {
    void* root = pager_get_page(table->pager, table->pager->root_page_num);
    void* right_child = pager_get_page(table->pager, right_child_page_num);
    
    // Step 1: Allocate new root page
    uint32_t new_root_page_num = pager_get_unused_page_num(table->pager);
    void* new_root = pager_get_page(table->pager, new_root_page_num);
    initialize_internal_node(new_root);
    set_node_root(new_root, true);
    *get_node_parent(new_root) = 0;  // Root has no parent
    
    // Step 2: Old root becomes left child
    uint32_t left_child_page_num = table->pager->root_page_num;
    void* left_child = root;
    
    // Copy old root to new location
    uint32_t temp_page_num = pager_get_unused_page_num(table->pager);
    void* temp_page = pager_get_page(table->pager, temp_page_num);
    memcpy(temp_page, left_child, PAGE_SIZE);
    
    // Initialize old root as new root
    initialize_internal_node(left_child);
    memcpy(left_child, new_root, PAGE_SIZE);
    
    // Restore original left child data
    memcpy(pager_get_page(table->pager, left_child_page_num), temp_page, PAGE_SIZE);
    
    // Step 3: Set up parent-child relationships
    *get_node_parent(pager_get_page(table->pager, left_child_page_num)) = 
        table->pager->root_page_num;
    *get_node_parent(right_child) = table->pager->root_page_num;
    
    // Step 4: Set children in new root
    *get_internal_node_num_keys(root) = 1;
    *get_internal_node_child(root, 0) = left_child_page_num;
    uint32_t left_child_max_key = get_node_max_key(table->pager, 
                                   pager_get_page(table->pager, left_child_page_num));
    *get_internal_node_key(root, 0) = left_child_max_key;
    *get_internal_node_right_child(root) = right_child_page_num;
    
    // Step 5: Mark pages dirty
    mark_page_dirty(table->pager, table->pager->root_page_num);
    mark_page_dirty(table->pager, left_child_page_num);
    mark_page_dirty(table->pager, right_child_page_num);
}
```

**Visual:**

**Before (root splitting):**
```
Height = 1

Root (Leaf): [10, 20, 30, ..., 130]  ← FULL, must split
```

**During split:**
```
Old Root splits into:
  Left:  [10, 20, 30, 40, 50, 60, 70]
  Right: [80, 90, 100, 110, 120, 130, 135]
```

**After create_new_root:**
```
Height = 2

         New Root (Internal)
         [child0, 70, right_child]
         /                   \
    Left Leaf              Right Leaf
    [10-70]                [80-135]
```

**Key insight:** Tree grows at the root, not at leaves!

**Complexity:**
- Page allocations: O(1)
- Memory copies: O(1)
- Parent updates: O(1)
- **Total: O(1)**

---

### Q56: How does internal_node_insert() work?
**Answer:**

**Purpose:** Insert new child pointer and key into internal node

**Called after:** Leaf split (must tell parent about new child)

**Algorithm:**
```
1. Check if internal node is full
2. If full, split internal node first
3. Find insertion index via binary search
4. Shift entries to make space
5. Insert new child and key
6. Update max keys
```

**Code:**

```cpp
void internal_node_insert(Table* table, uint32_t parent_page_num, 
                         uint32_t child_page_num) {
    void* parent = pager_get_page(table->pager, parent_page_num);
    void* child = pager_get_page(table->pager, child_page_num);
    
    uint32_t child_max_key = get_node_max_key(table->pager, child);
    uint32_t index = internal_node_find_child(parent, child_max_key);
    
    uint32_t num_keys = *get_internal_node_num_keys(parent);
    
    // Check if split needed
    if (num_keys >= INTERNAL_NODE_MAX_CELLS) {
        internal_node_split_and_insert(table, parent_page_num, child_page_num);
        return;
    }
    
    // Shift entries to make space
    uint32_t right_child_page_num = *get_internal_node_right_child(parent);
    
    if (index == num_keys) {
        // Inserting at end - old right becomes regular child
        *get_internal_node_child(parent, index) = right_child_page_num;
        *get_internal_node_key(parent, index) = 
            get_node_max_key(table->pager, 
                           pager_get_page(table->pager, right_child_page_num));
        *get_internal_node_right_child(parent) = child_page_num;
    } else {
        // Shift existing entries right
        for (uint32_t i = num_keys; i > index; i--) {
            void* destination = get_internal_node_cell(parent, i);
            void* source = get_internal_node_cell(parent, i - 1);
            memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
        }
        
        // Insert new entry
        *get_internal_node_child(parent, index) = child_page_num;
        *get_internal_node_key(parent, index) = child_max_key;
    }
    
    *get_internal_node_num_keys(parent) += 1;
    mark_page_dirty(table->pager, parent_page_num);
}
```

**Visual example:**

**Before:** Internal node with 2 children
```
Parent: [child0, 50, right_child]
         page5        page7
```

**Insert new child (page 6, max_key=30):**
```
Step 1: Binary search - insert at index 1 (between 5 and 7)

Step 2: Shift
  Old: [5, 50, 7]
  New: [5, 50, 50, 7]  ← Duplicate 50 temporarily

Step 3: Insert
  [5, 50, 6, 30, 7]  ← Insert page 6 with key 30

Final: [child0, 50, child1, 30, right_child]
        page5        page6        page7
```

**When does it trigger internal split?**
```cpp
if (num_keys >= INTERNAL_NODE_MAX_CELLS) {
    // 510 keys! Internal node full!
    internal_node_split_and_insert(...);
}
```

---

### Q57: Explain internal_node_split_and_insert().
**Answer:**

**When:** Internal node reaches 510 keys

**Algorithm:**
```
1. Create new internal node (right sibling)
2. Move half the entries to new node
3. Insert new child into appropriate node
4. Update parent with new node
5. Recursively handle if parent is full
```

**Code (simplified):**

```cpp
void internal_node_split_and_insert(Table* table, uint32_t parent_page_num, 
                                   uint32_t child_page_num) {
    void* old_node = pager_get_page(table->pager, parent_page_num);
    uint32_t new_page_num = pager_get_unused_page_num(table->pager);
    void* new_node = pager_get_page(table->pager, new_page_num);
    initialize_internal_node(new_node);
    
    *get_node_parent(new_node) = *get_node_parent(old_node);
    
    // Create temp array for all children + new one
    uint32_t all_children[INTERNAL_NODE_MAX_CELLS + 2];
    uint32_t all_keys[INTERNAL_NODE_MAX_CELLS + 1];
    
    // Copy existing entries
    for (uint32_t i = 0; i < INTERNAL_NODE_MAX_CELLS; i++) {
        all_children[i] = *get_internal_node_child(old_node, i);
        all_keys[i] = *get_internal_node_key(old_node, i);
    }
    all_children[INTERNAL_NODE_MAX_CELLS] = 
        *get_internal_node_right_child(old_node);
    
    // Insert new child at correct position
    void* child = pager_get_page(table->pager, child_page_num);
    uint32_t child_max_key = get_node_max_key(table->pager, child);
    uint32_t insert_index = internal_node_find_child(old_node, child_max_key);
    
    // Shift and insert (similar to leaf split)
    // ...
    
    // Split: left gets first half, right gets second half
    uint32_t split_point = (INTERNAL_NODE_MAX_CELLS + 1) / 2;
    
    *get_internal_node_num_keys(old_node) = split_point;
    for (uint32_t i = 0; i < split_point; i++) {
        *get_internal_node_child(old_node, i) = all_children[i];
        *get_internal_node_key(old_node, i) = all_keys[i];
    }
    *get_internal_node_right_child(old_node) = all_children[split_point];
    
    *get_internal_node_num_keys(new_node) = 
        INTERNAL_NODE_MAX_CELLS + 1 - split_point;
    for (uint32_t i = 0; i < *get_internal_node_num_keys(new_node); i++) {
        *get_internal_node_child(new_node, i) = all_children[split_point + 1 + i];
        *get_internal_node_key(new_node, i) = all_keys[split_point + 1 + i];
    }
    *get_internal_node_right_child(new_node) = 
        all_children[INTERNAL_NODE_MAX_CELLS + 1];
    
    // Update parent pointers of moved children
    for (uint32_t i = 0; i <= *get_internal_node_num_keys(new_node); i++) {
        uint32_t child_page;
        if (i == *get_internal_node_num_keys(new_node)) {
            child_page = *get_internal_node_right_child(new_node);
        } else {
            child_page = *get_internal_node_child(new_node, i);
        }
        void* child_node = pager_get_page(table->pager, child_page);
        *get_node_parent(child_node) = new_page_num;
        mark_page_dirty(table->pager, child_page);
    }
    
    // Mark dirty
    mark_page_dirty(table->pager, parent_page_num);
    mark_page_dirty(table->pager, new_page_num);
    
    // Update parent (may cause recursive split!)
    if (is_node_root(old_node)) {
        create_new_root(table, new_page_num);
    } else {
        uint32_t grandparent = *get_node_parent(old_node);
        internal_node_insert(table, grandparent, new_page_num);
    }
}
```

**Key difference from leaf split:**
- ✅ Must update parent pointers of ALL moved children!
- ✅ No sibling links (only leaves have those)
- ✅ Can trigger cascading splits up the tree

**Visual:**
```
Before (510 keys):
Internal: [child0, k0, child1, k1, ..., child509, k509, right]

After split (255 keys each):
Left:  [child0, k0, ..., child254, k254, child255]
Right: [child256, k256, ..., child509, k509, child510]

All children in Right (256-510) get updated parent pointer!
```

---

### Q58: When do we need to merge nodes?
**Answer:**

**When:** Node has fewer than minimum cells (underflow)

**Minimum thresholds:**
```cpp
#define LEAF_NODE_MIN_CELLS 6        // Half of 13
#define INTERNAL_NODE_MIN_KEYS 255   // Half of 510
```

**Underflow occurs after:**
- DELETE removes too many keys
- BORROW not possible (sibling also near minimum)

**Detection:**
```cpp
void leaf_node_delete(Cursor* cursor) {
    // ... delete cell ...
    
    *get_leaf_node_num_cells(node) -= 1;
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    
    // Check for underflow
    if (!is_node_root(node) && num_cells < LEAF_NODE_MIN_CELLS) {
        handle_leaf_underflow(cursor->table, cursor->page_num);
    }
}
```

**Why minimum matters:**
```
B-Tree invariant: All non-root nodes must have [min, max] keys

Without minimum:
  - Tree becomes unbalanced
  - Search performance degrades to O(n)
  - Wasted disk space (many nearly-empty pages)

With minimum:
  - Tree stays balanced
  - Height remains O(log n)
  - Good space utilization
```

**Example:**
```
Before delete:
Leaf: [10, 20, 30, 40, 50, 60]  ← 6 cells (at minimum)

Delete 10:
Leaf: [20, 30, 40, 50, 60]  ← 5 cells (UNDERFLOW!)

Must fix: Borrow from sibling or merge
```

**Decision tree:**
```
Underflow detected
   ↓
Check left sibling
   ↓
Has > min cells?
   ├─ Yes → Borrow from left
   └─ No ↓
      Check right sibling
         ↓
      Has > min cells?
         ├─ Yes → Borrow from right
         └─ No → Merge with sibling
```

---

### Q59: Explain handle_leaf_underflow() algorithm.
**Answer:**

**Purpose:** Fix leaf node with too few cells

**Algorithm:**
```
1. Find parent and siblings
2. Try to borrow from left sibling
3. If can't, try to borrow from right sibling
4. If can't borrow, merge with sibling
5. Update parent keys
6. Check if parent underflows (recursive)
```

**Code:**

```cpp
void handle_leaf_underflow(Table* table, uint32_t page_num) {
    void* node = pager_get_page(table->pager, page_num);
    uint32_t parent_page_num = *get_node_parent(node);
    void* parent = pager_get_page(table->pager, parent_page_num);
    
    // Find child index in parent
    int32_t child_index = find_child_index_in_parent(parent, page_num);
    uint32_t num_keys = *get_internal_node_num_keys(parent);
    
    // Try borrowing from left sibling
    if (child_index > 0) {
        uint32_t left_sibling_page = *get_internal_node_child(parent, child_index - 1);
        void* left_sibling = pager_get_page(table->pager, left_sibling_page);
        uint32_t left_cells = *get_leaf_node_num_cells(left_sibling);
        
        if (left_cells > LEAF_NODE_MIN_CELLS) {
            // Borrow from left
            borrow_from_left_leaf(table, page_num, left_sibling_page);
            return;
        }
    }
    
    // Try borrowing from right sibling
    if (child_index < num_keys) {
        uint32_t right_sibling_page;
        if (child_index == num_keys - 1) {
            right_sibling_page = *get_internal_node_right_child(parent);
        } else {
            right_sibling_page = *get_internal_node_child(parent, child_index + 1);
        }
        
        void* right_sibling = pager_get_page(table->pager, right_sibling_page);
        uint32_t right_cells = *get_leaf_node_num_cells(right_sibling);
        
        if (right_cells > LEAF_NODE_MIN_CELLS) {
            // Borrow from right
            borrow_from_right_leaf(table, page_num, right_sibling_page);
            return;
        }
    }
    
    // Can't borrow - must merge
    if (child_index > 0) {
        // Merge with left sibling
        uint32_t left_sibling_page = *get_internal_node_child(parent, child_index - 1);
        merge_leaf_nodes(table, left_sibling_page, page_num);
    } else {
        // Merge with right sibling
        uint32_t right_sibling_page = *get_internal_node_child(parent, child_index + 1);
        merge_leaf_nodes(table, page_num, right_sibling_page);
    }
    
    // Check if parent underflows after removing child
    uint32_t parent_keys = *get_internal_node_num_keys(parent);
    if (!is_node_root(parent) && parent_keys < INTERNAL_NODE_MIN_KEYS) {
        handle_internal_underflow(table, parent_page_num);
    }
}
```

**Decision flow:**

```
Leaf [20, 30] ← 2 cells (underflow!)
Parent: [Left, 40, Current, 80, Right]

Step 1: Check Left
  Left has [10, 15, 25, 35, 40, 45, 50]  ← 7 cells (can borrow!)
  Action: borrow_from_left_leaf()
  Result: Current = [15, 20, 30], Left = [10, 25, 35, 40, 45, 50]
  DONE ✓

Alternative Step 1: Check Left
  Left has [10, 15, 25, 35, 40]  ← 5 cells (too few!)
  Continue to Step 2

Step 2: Check Right
  Right has [80, 85, 90, 95, 100, 105, 110]  ← 7 cells (can borrow!)
  Action: borrow_from_right_leaf()
  Result: Current = [20, 30, 80], Right = [85, 90, 95, 100, 105, 110]
  DONE ✓

Alternative Step 2: Check Right
  Right has [80, 85, 90]  ← 3 cells (too few!)
  Continue to Step 3

Step 3: Merge
  Action: merge_leaf_nodes(Left, Current)
  Result: Left = [10, 15, 20, 30, 25, 35, 40], Current freed
  Parent updated to remove Current
```

---

### Q60: How does borrow_from_left_leaf() work?
**Answer:**

**Purpose:** Move one cell from left sibling to current node

**Precondition:** Left sibling has > LEAF_NODE_MIN_CELLS

**Algorithm:**
```
1. Get rightmost cell from left sibling
2. Insert at beginning of current node
3. Update parent separator key
4. Update cell counts
```

**Code:**

```cpp
void borrow_from_left_leaf(Table* table, uint32_t page_num, 
                          uint32_t left_sibling_page) {
    void* node = pager_get_page(table->pager, page_num);
    void* left_sibling = pager_get_page(table->pager, left_sibling_page);
    
    uint32_t left_cells = *get_leaf_node_num_cells(left_sibling);
    uint32_t curr_cells = *get_leaf_node_num_cells(node);
    
    // Shift current node's cells right
    for (int32_t i = curr_cells; i > 0; i--) {
        void* dest = get_leaf_node_cell(node, i);
        void* src = get_leaf_node_cell(node, i - 1);
        memcpy(dest, src, LEAF_NODE_CELL_SIZE);
    }
    
    // Copy rightmost cell from left sibling
    void* dest = get_leaf_node_cell(node, 0);
    void* src = get_leaf_node_cell(left_sibling, left_cells - 1);
    memcpy(dest, src, LEAF_NODE_CELL_SIZE);
    
    // Update counts
    *get_leaf_node_num_cells(node) = curr_cells + 1;
    *get_leaf_node_num_cells(left_sibling) = left_cells - 1;
    
    // Update parent separator key
    uint32_t parent_page = *get_node_parent(node);
    void* parent = pager_get_page(table->pager, parent_page);
    int32_t child_index = find_child_index_in_parent(parent, left_sibling_page);
    
    uint32_t new_max = get_node_max_key(table->pager, left_sibling);
    *get_internal_node_key(parent, child_index) = new_max;
    
    // Mark dirty
    mark_page_dirty(table->pager, page_num);
    mark_page_dirty(table->pager, left_sibling_page);
    mark_page_dirty(table->pager, parent_page);
}
```

**Visual trace:**

**Before:**
```
Parent: [Left, 40, Current, 80, Right]

Left:    [10, 20, 30, 40]  ← 4 cells (can give 1)
Current: [50]              ← 1 cell (underflow)
```

**After borrow:**
```
Parent: [Left, 30, Current, 80, Right]  ← Updated separator!

Left:    [10, 20, 30]     ← Lost rightmost (40)
Current: [40, 50]         ← Gained at beginning
```

**Key steps:**
1. Move 40 from Left to Current
2. Left max key changes: 40 → 30
3. Update parent key from 40 to 30

---

### Q61: How does merge_leaf_nodes() work?
**Answer:**

**Purpose:** Combine two leaf nodes into one

**When:** Both nodes have ≤ LEAF_NODE_MIN_CELLS

**Algorithm:**
```
1. Copy all cells from right to left
2. Update sibling links
3. Free right node
4. Remove right node from parent
```

**Code:**

```cpp
void merge_leaf_nodes(Table* table, uint32_t left_page, uint32_t right_page) {
    void* left_node = pager_get_page(table->pager, left_page);
    void* right_node = pager_get_page(table->pager, right_page);
    
    uint32_t left_cells = *get_leaf_node_num_cells(left_node);
    uint32_t right_cells = *get_leaf_node_num_cells(right_node);
    
    // Copy all cells from right to left
    for (uint32_t i = 0; i < right_cells; i++) {
        void* dest = get_leaf_node_cell(left_node, left_cells + i);
        void* src = get_leaf_node_cell(right_node, i);
        memcpy(dest, src, LEAF_NODE_CELL_SIZE);
    }
    
    // Update left's cell count
    *get_leaf_node_num_cells(left_node) = left_cells + right_cells;
    
    // Update sibling link (left points to right's next)
    uint32_t right_next = *get_leaf_node_next_leaf(right_node);
    *get_leaf_node_next_leaf(left_node) = right_next;
    
    // Mark left dirty
    mark_page_dirty(table->pager, left_page);
    
    // Free right node
    pager_free_page(table->pager, right_page);
    
    // Remove right from parent
    uint32_t parent_page = *get_node_parent(right_node);
    void* parent = pager_get_page(table->pager, parent_page);
    internal_node_remove_child(table, parent_page, right_page);
    
    // Update left's max key in parent
    int32_t left_index = find_child_index_in_parent(parent, left_page);
    uint32_t new_max = get_node_max_key(table->pager, left_node);
    *get_internal_node_key(parent, left_index) = new_max;
    mark_page_dirty(table->pager, parent_page);
}
```

**Visual:**

**Before merge:**
```
Parent: [Left, 40, Right, 80, Next]

Left:  [10, 20, 30]     ← 3 cells
Right: [40, 50]         ← 2 cells
Next:  [80, 90, 100]
```

**After merge:**
```
Parent: [Left, 50, Next]  ← Right removed!

Left:  [10, 20, 30, 40, 50]  ← Merged
Right: (freed)
Next:  [80, 90, 100]
```

**Sibling links updated:**
```
Before: Left → Right → Next
After:  Left --------→ Next  (Right removed)
```

**Parent underflow check:**
```cpp
// After removing Right, parent has 1 key
if (!is_node_root(parent) && 1 < INTERNAL_NODE_MIN_KEYS) {
    handle_internal_underflow(table, parent_page);  // May cascade!
}
```

---

### Q62: What is handle_internal_underflow()?
**Answer:**

**Purpose:** Fix internal node with too few keys

**Similar to leaf underflow, but:**
- Works with children (pointers), not cells (data)
- Must update parent pointers of moved children
- More complex because internal nodes have right_child

**Algorithm:**
```
1. Try to borrow from left sibling
2. Try to borrow from right sibling
3. If can't borrow, merge with sibling
4. Recursively check parent
```

**Code (simplified):**

```cpp
void handle_internal_underflow(Table* table, uint32_t page_num) {
    void* node = pager_get_page(table->pager, page_num);
    uint32_t parent_page_num = *get_node_parent(node);
    void* parent = pager_get_page(table->pager, parent_page_num);
    
    int32_t child_index = find_child_index_in_parent(parent, page_num);
    uint32_t num_keys = *get_internal_node_num_keys(parent);
    
    // Try borrow from left
    if (child_index > 0) {
        uint32_t left_page = *get_internal_node_child(parent, child_index - 1);
        void* left = pager_get_page(table->pager, left_page);
        
        if (*get_internal_node_num_keys(left) > INTERNAL_NODE_MIN_KEYS) {
            borrow_from_left_internal(table, page_num, left_page);
            return;
        }
    }
    
    // Try borrow from right
    if (child_index < num_keys) {
        uint32_t right_page = (child_index == num_keys - 1) ?
            *get_internal_node_right_child(parent) :
            *get_internal_node_child(parent, child_index + 1);
        void* right = pager_get_page(table->pager, right_page);
        
        if (*get_internal_node_num_keys(right) > INTERNAL_NODE_MIN_KEYS) {
            borrow_from_right_internal(table, page_num, right_page);
            return;
        }
    }
    
    // Must merge
    if (child_index > 0) {
        uint32_t left_page = *get_internal_node_child(parent, child_index - 1);
        merge_internal_nodes(table, left_page, page_num);
    } else {
        uint32_t right_page = *get_internal_node_child(parent, child_index + 1);
        merge_internal_nodes(table, page_num, right_page);
    }
    
    // Check parent
    if (!is_node_root(parent) && 
        *get_internal_node_num_keys(parent) < INTERNAL_NODE_MIN_KEYS) {
        handle_internal_underflow(table, parent_page_num);  // Recursive!
    }
}
```

**Key difference from leaf:**

**Borrowing from sibling:**
```cpp
// Must update parent pointer of borrowed child!
uint32_t borrowed_child = *get_internal_node_right_child(sibling);
void* borrowed_child_node = pager_get_page(table->pager, borrowed_child);
*get_node_parent(borrowed_child_node) = current_page_num;
mark_page_dirty(table->pager, borrowed_child);
```

**Merging:**
```cpp
// Must update parent pointers of ALL merged children!
for (uint32_t i = 0; i <= right_keys; i++) {
    uint32_t child = (i == right_keys) ?
        *get_internal_node_right_child(right_node) :
        *get_internal_node_child(right_node, i);
    
    void* child_node = pager_get_page(table->pager, child);
    *get_node_parent(child_node) = left_page;
    mark_page_dirty(table->pager, child);
}
```

---

### Q63: Can underflow cascade up the tree?
**Answer:**

**Yes!** Underflow can trigger chain reaction.

**Example cascade:**

**Step 1: Delete from leaf**
```
Leaf: [10, 20] ← 2 cells (underflow!)
```

**Step 2: Merge with sibling**
```
Merge two leaves → parent loses one child
```

**Step 3: Parent underflows**
```
Internal parent: [child0, child1]  ← Only 2 children (underflow!)
(Assuming MIN_KEYS = 255, this is way below)
```

**Step 4: Fix parent**
```
Merge parent with its sibling → grandparent loses child
```

**Step 5: Grandparent underflows?**
```
If grandparent also underflows, continue up...
```

**Eventually reaches root:**
```
Root with only 1 child:
  Root → OnlyChild

Special case: Reduce tree height!
  Make OnlyChild the new root
```

**Code for root shrinking:**
```cpp
void shrink_tree_height(Table* table) {
    void* root = pager_get_page(table->pager, table->pager->root_page_num);
    
    // Root must be internal with 0 keys (only right_child)
    if (get_node_type(root) == NODE_INTERNAL && 
        *get_internal_node_num_keys(root) == 0) {
        
        uint32_t only_child = *get_internal_node_right_child(root);
        void* child_node = pager_get_page(table->pager, only_child);
        
        // Copy child to root position
        memcpy(root, child_node, PAGE_SIZE);
        
        // Free old child page
        pager_free_page(table->pager, only_child);
        
        // Update root properties
        set_node_root(root, true);
        *get_node_parent(root) = 0;
        
        mark_page_dirty(table->pager, table->pager->root_page_num);
    }
}
```

**Visual cascade:**

**Tree before cascade:**
```
Height = 3

           Root
          /    \
      Internal1  Internal2
      /    \      /    \
    L1   L2   L3   L4
```

**Delete from L1 triggers merge with L2:**
```
           Root
          /    \
      Internal1  Internal2
      /          /    \
   L1+L2      L3   L4
```

**Internal1 underflows, merges with Internal2:**
```
        Root
         |
    Internal1+2
     /    |    \
  L1+L2  L3   L4
```

**Root has only 1 child, shrink height:**
```
Height = 2

    Internal1+2
     /    |    \
  L1+L2  L3   L4
```

**Result: Tree height reduced from 3 to 2!**

---

### Q64: What is the cost of split/merge/borrow operations?
**Answer:**

**Time Complexity:**

| Operation | Time | Reason |
|-----------|------|--------|
| **Leaf Split** | O(M) | Copy M cells |
| **Internal Split** | O(M) | Copy M children + update parent pointers |
| **Leaf Borrow** | O(M) | Shift cells in node |
| **Internal Borrow** | O(M) | Shift children + update parent pointer |
| **Leaf Merge** | O(M) | Copy cells from sibling |
| **Internal Merge** | O(M) | Copy children + update all parent pointers |
| **Cascade (worst)** | O(h × M) | h = height levels |

Where M = max cells/keys (13 for leaf, 510 for internal)

**Space Complexity:**
- Temporary arrays: O(M)
- No recursion (except cascade)
- **Total: O(M)**

**I/O Complexity:**

**Split:**
```
Read:
  - Old node: 1 page
  - New node: 1 page (allocated)
  - Parent: 1 page
Total: 3 page reads

Write (dirty):
  - Old node: 1 page
  - New node: 1 page
  - Parent: 1 page
Total: 3 page writes
```

**Merge:**
```
Read:
  - Left node: 1 page
  - Right node: 1 page
  - Parent: 1 page
Total: 3 page reads

Write (dirty):
  - Left node: 1 page
  - Right node: freed (write to freelist)
  - Parent: 1 page
Total: 2-3 page writes
```

**Amortized cost:**
```
Most operations: O(log n) without split/merge

With split/merge:
  - Happens infrequently (when threshold crossed)
  - Amortized over many operations
  - Still O(log n) amortized
```

**Real-world performance:**

**1 million inserts:**
```
Regular inserts: ~999,000 (no split)
Split operations: ~1,000 (0.1%)

Amortized cost per insert:
  Regular: O(log n)
  Split: O(log n + M) ÷ 1000 = O(log n)
```

**Why still efficient:**
- Splits/merges are rare
- Cost spread over many operations
- Keeps tree balanced (worth the cost!)

---

### Q65: How do you test split/merge/borrow logic?
**Answer:**

**Testing strategy:**

**1. Unit tests for each operation:**
```python
def test_leaf_split():
    """Test splitting a full leaf node"""
    db = Database("test.db")
    
    # Fill leaf to max (13 cells)
    for i in range(13):
        db.insert(i, f"user{i}", f"email{i}@test.com")
    
    # Trigger split
    db.insert(13, "user13", "email13@test.com")
    
    # Verify tree structure
    assert db.validate_tree() == True
    assert db.tree_height() == 2  # Should create internal node

def test_leaf_merge():
    """Test merging underflowing leaves"""
    db = Database("test.db")
    
    # Create situation with multiple leaves
    for i in range(30):
        db.insert(i, f"user{i}", f"email{i}@test.com")
    
    # Delete until underflow
    for i in range(20, 30):
        db.delete(i)
    
    # Verify merge happened
    assert db.count_leaf_nodes() < original_count

def test_internal_split():
    """Test splitting internal node (510 keys)"""
    db = Database("test.db")
    
    # Insert enough to create 510 leaf nodes
    # Each leaf can hold 13 cells
    for i in range(13 * 511):
        db.insert(i, f"user{i}", f"email{i}@test.com")
    
    # Verify internal split occurred
    assert db.tree_height() == 3
```

**2. Stress tests:**
```python
def test_random_operations():
    """Random inserts/deletes to trigger all cases"""
    db = Database("test.db")
    import random
    
    keys = set()
    for _ in range(10000):
        op = random.choice(['insert', 'delete'])
        key = random.randint(0, 100000)
        
        if op == 'insert':
            db.insert(key, f"user{key}", f"email{key}@test.com")
            keys.add(key)
        elif op == 'delete' and key in keys:
            db.delete(key)
            keys.remove(key)
        
        # Verify tree remains valid
        assert db.validate_tree() == True
```

**3. Edge cases:**
```python
def test_root_split():
    """Test splitting the root (height increase)"""
    # ...

def test_root_shrink():
    """Test shrinking when root has 1 child"""
    # ...

def test_borrow_boundary():
    """Test borrowing at min+1 threshold"""
    # ...

def test_merge_cascade():
    """Test cascading merge up tree"""
    # ...
```

**4. Persistence tests:**
```python
def test_rebalance_persistence():
    """Verify splits/merges persist"""
    db = Database("test.db")
    
    # Trigger split
    for i in range(20):
        db.insert(i, f"user{i}", f"email{i}@test.com")
    
    height_before = db.tree_height()
    db.close()
    
    # Reopen
    db = Database("test.db")
    height_after = db.tree_height()
    
    assert height_before == height_after
    assert db.validate_tree() == True
```

**See:** `test_rebalance_persistence.py` for real implementation!

---

### Q66: What bugs did you encounter with split/merge/borrow?
**Answer:**

**Bug #4: Rebalancing operations not marked dirty**

**Symptom:**
```python
# Insert triggers split
db.insert(100, "user", "email")  # OK in memory
db.close()

# Reopen
db = Database("test.db")
db.validate_tree()  # FAIL! Tree corrupted
```

**Root cause:**
```cpp
// ❌ Before fix:
void leaf_node_split_and_insert(...) {
    // Modify old_node
    *get_leaf_node_num_cells(old_node) = LEFT_SPLIT_COUNT;
    // ... copy cells ...
    
    // Modify new_node
    initialize_leaf_node(new_node);
    // ... copy cells ...
    
    // ❌ FORGOT TO MARK DIRTY!
    // mark_page_dirty(pager, old_page_num);  ← Missing!
    // mark_page_dirty(pager, new_page_num);  ← Missing!
}
```

**Impact:**
- Splits work in cache
- Not written to disk
- After restart: tree corrupted

**Fix:**
```cpp
// ✅ After fix:
void leaf_node_split_and_insert(...) {
    // ... modify nodes ...
    
    // Mark ALL modified pages dirty!
    mark_page_dirty(cursor->table->pager, cursor->page_num);
    mark_page_dirty(cursor->table->pager, new_page_num);
    
    if (!is_node_root(old_node)) {
        mark_page_dirty(cursor->table->pager, parent_page_num);
    }
}
```

**Other common bugs:**

**Bug: Forgot to update parent pointers**
```cpp
// ❌ Bad:
void internal_split(...) {
    // Move children to new node
    *get_internal_node_child(new_node, i) = child_pages[i];
    // ❌ Child still thinks old node is parent!
}

// ✅ Good:
void internal_split(...) {
    *get_internal_node_child(new_node, i) = child_pages[i];
    void* child = pager_get_page(pager, child_pages[i]);
    *get_node_parent(child) = new_page_num;  // Update!
    mark_page_dirty(pager, child_pages[i]);
}
```

**Bug: Incorrect sibling link update**
```cpp
// ❌ Bad:
void leaf_split(...) {
    *get_leaf_node_next_leaf(old_node) = new_page_num;
    // ❌ Forgot to set new_node's next!
}

// ✅ Good:
void leaf_split(...) {
    uint32_t old_next = *get_leaf_node_next_leaf(old_node);
    *get_leaf_node_next_leaf(new_node) = old_next;  // Preserve chain
    *get_leaf_node_next_leaf(old_node) = new_page_num;
}
```

---

### Q67: Why use memcpy vs memmove in split/merge?
**Answer:**

**memcpy:** Fast copy when source/dest DON'T overlap
**memmove:** Safe copy when source/dest MIGHT overlap

**In split (no overlap):**
```cpp
void leaf_node_split(...) {
    // Copy from temp array to node
    void* dest = get_leaf_node_cell(new_node, i);
    void* src = &all_cells[SPLIT_POINT + i];
    
    memcpy(dest, src, CELL_SIZE);  // ✅ OK - no overlap
}
```

**In insert-with-shift (overlap!):**
```cpp
void leaf_node_insert(...) {
    // Shift cells right
    void* dest = get_leaf_node_cell(node, cell_num + 1);
    void* src = get_leaf_node_cell(node, cell_num);
    uint32_t bytes = (num_cells - cell_num) * CELL_SIZE;
    
    memmove(dest, src, bytes);  // ✅ MUST use memmove!
    // memcpy(dest, src, bytes);  ❌ Would corrupt data!
}
```

**Visual:**

**Overlap scenario:**
```
Array: [A, B, C, D, E]
       [0, 1, 2, 3, 4]

Shift right from index 2:
  src:  [2, 3, 4]
  dest: [3, 4, 5]
        ↑  ↑  ↑
        overlap!

memcpy: Might copy [C, C, C] ❌
memmove: Correctly copies [C, D, E] ✅
```

**Performance:**
```c
memcpy:  Very fast, no overlap checks
memmove: Slightly slower, handles overlap

When possible, use memcpy for speed.
When in doubt, use memmove for safety.
```

---

## 📝 Summary - Part 4 Topics Covered

✅ **When to Split** - Capacity thresholds, trigger conditions  
✅ **leaf_node_split_and_insert()** - Algorithm, visual trace  
✅ **create_new_root()** - Tree height increase, root splitting  
✅ **internal_node_insert()** - Parent updates after split  
✅ **internal_node_split_and_insert()** - Splitting internal nodes  
✅ **When to Merge** - Underflow detection, minimum thresholds  
✅ **handle_leaf_underflow()** - Decision tree, borrow vs merge  
✅ **borrow_from_left_leaf()** - Moving cells between siblings  
✅ **merge_leaf_nodes()** - Combining underflowing nodes  
✅ **handle_internal_underflow()** - Internal node rebalancing  
✅ **Cascade Effects** - Chain reactions, tree shrinking  
✅ **Cost Analysis** - Time, space, I/O, amortized cost  
✅ **Testing Strategy** - Unit tests, stress tests, edge cases  
✅ **Common Bugs** - Missing dirty marks, parent pointers  
✅ **memcpy vs memmove** - Overlap handling  

---

**Next:** [VIVA_QUESTIONS_PART5.md](./VIVA_QUESTIONS_PART5.md) - Testing & Validation

**See Also:**
- [07_btree_split_merge_borrow.md](./07_btree_split_merge_borrow.md) - Detailed algorithms
- [CODE_WALKTHROUGH_PART2.md](./CODE_WALKTHROUGH_PART2.md) - Code traces
- [test_rebalance_persistence.py](../test_rebalance_persistence.py) - Actual tests
