# 🎓 ArborDB Viva/Interview Questions - Part 3: B-Tree Operations

## Overview
This part covers search, insert, delete operations, cursors, and tree traversal.

---

## 🔍 Section 3: Core B-Tree Operations (20 Questions)

### Q36: Explain how search works in a B-Tree.
**Answer:**

**High-level algorithm:**
```
1. Start at root
2. While current node is internal:
   - Binary search for correct child
   - Follow child pointer
3. Reached leaf → binary search for key
4. Return cursor pointing to position
```

**Detailed code flow:**

**Step 1: Find leaf node**
```cpp
Cursor* table_find(Table* table, uint32_t key) {
    uint32_t page_num = table->pager->root_page_num;
    void* node = pager_get_page(table->pager, page_num);
    
    // Traverse internal nodes
    while (get_node_type(node) == NODE_INTERNAL) {
        uint32_t num_keys = *get_internal_node_num_keys(node);
        
        // Binary search for correct child
        uint32_t min_index = 0;
        uint32_t max_index = num_keys;
        
        while (min_index != max_index) {
            uint32_t index = (min_index + max_index) / 2;
            uint32_t key_to_compare = *get_internal_node_key(node, index);
            
            if (key <= key_to_compare) {
                max_index = index;
            } else {
                min_index = index + 1;
            }
        }
        
        // Get child page number
        if (min_index == num_keys) {
            page_num = *get_internal_node_right_child(node);
        } else {
            page_num = *get_internal_node_child(node, min_index);
        }
        
        node = pager_get_page(table->pager, page_num);
    }
    
    // Now at leaf, do final binary search
    return leaf_node_find(table, page_num, key);
}
```

**Step 2: Binary search in leaf**
```cpp
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
    void* node = pager_get_page(table->pager, page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    
    Cursor* cursor = new Cursor();
    cursor->table = table;
    cursor->page_num = page_num;
    
    // Binary search
    uint32_t min_index = 0;
    uint32_t max_index = num_cells;
    
    while (min_index != max_index) {
        uint32_t index = (min_index + max_index) / 2;
        uint32_t key_at_index = *get_leaf_node_key(node, index);
        
        if (key == key_at_index) {
            cursor->cell_num = index;
            cursor->end_of_table = false;
            return cursor;
        }
        
        if (key < key_at_index) {
            max_index = index;
        } else {
            min_index = index + 1;
        }
    }
    
    // Not found - cursor at insertion point
    cursor->cell_num = min_index;
    cursor->end_of_table = (cursor->cell_num == num_cells);
    return cursor;
}
```

**Example trace:**
```
Tree:
          Internal [50, 100]
         /         |         \
    Leaf[1-40]  Leaf[50-80]  Leaf[100-200]

Search for key = 75:

1. Start at root (Internal)
2. Binary search in [50, 100]
   - 75 > 50 ✓
   - 75 < 100 ✓
   - Follow middle child
3. Reached Leaf[50-80]
4. Binary search in [50,55,60,65,70,75,80]
   - Found 75 at index 5
5. Return cursor(page_num=leaf, cell_num=5)
```

**Time Complexity:**
- Internal node traversal: O(log n) levels × O(log M) binary search
- Leaf search: O(log M)
- **Total: O(log n)** where n = total records

---

### Q37: How does insert work when there's space in the leaf?
**Answer:**

**Precondition:** Leaf has < 13 cells (not full)

**Algorithm:**
```
1. Find insertion position using binary search
2. Shift cells right to make space
3. Insert new cell
4. Increment cell count
5. Mark page dirty
```

**Code:**
```cpp
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    
    // Shift cells right if inserting in middle
    if (cursor->cell_num < num_cells) {
        void* destination = get_leaf_node_cell(node, cursor->cell_num + 1);
        void* source = get_leaf_node_cell(node, cursor->cell_num);
        uint32_t bytes_to_move = (num_cells - cursor->cell_num) * LEAF_NODE_CELL_SIZE;
        memmove(destination, source, bytes_to_move);  // ✅ Use memmove for overlap!
    }
    
    // Insert new cell
    *get_leaf_node_num_cells(node) += 1;
    *get_leaf_node_key(node, cursor->cell_num) = key;
    serialize_row(value, get_leaf_node_value(node, cursor->cell_num));
    
    // CRITICAL: Mark page dirty!
    mark_page_dirty(cursor->table->pager, cursor->page_num);
}
```

**Visual example:**

**Before: Insert key=25**
```
Leaf: [10, 20, 30, 40]  ← 4 cells
              ↑ Insert at index 2
```

**After memmove:**
```
Leaf: [10, 20, ??, 30, 40]  ← Shifted right
              ↑ Space created
```

**After insert:**
```
Leaf: [10, 20, 25, 30, 40]  ← 5 cells
```

**Why memmove instead of memcpy?**
```cpp
// Source and destination overlap!
// [10, 20, 30, 40]
//        src ────→
//           dest ──→

// memcpy: UNDEFINED (might corrupt data)
// memmove: SAFE (handles overlap correctly)
```

---

### Q38: What is a Cursor and why is it needed?
**Answer:**

**Cursor = Iterator pointing to a position in the B-Tree**

**Structure:**
```cpp
struct Cursor {
    Table* table;           // Which table
    uint32_t page_num;      // Which page (leaf node)
    uint32_t cell_num;      // Which cell in page
    bool end_of_table;      // At end?
};
```

**Why needed?**

**Problem without cursor:**
```cpp
// How to iterate through tree?
// How to remember position for multi-step operations?
// How to handle modifications during iteration?
```

**Solution with cursor:**
```cpp
// Start at beginning
Cursor* cursor = table_start(table);

// Iterate
while (!cursor->end_of_table) {
    Row row;
    deserialize_row(cursor_value(cursor), &row);
    print_row(&row);
    cursor_advance(cursor);  // Move to next
}
```

**Key operations:**

**1. Create cursor at key:**
```cpp
Cursor* cursor = table_find(table, 42);
// Points to key 42 or insertion position
```

**2. Create cursor at start:**
```cpp
Cursor* table_start(Table* table) {
    Cursor* cursor = table_find(table, 0);  // Find smallest key
    
    void* node = pager_get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    cursor->end_of_table = (num_cells == 0);
    
    return cursor;
}
```

**3. Advance cursor:**
```cpp
void cursor_advance(Cursor* cursor) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    
    cursor->cell_num += 1;
    if (cursor->cell_num >= *get_leaf_node_num_cells(node)) {
        // Move to next leaf (sibling link!)
        uint32_t next_page = *get_leaf_node_next_leaf(node);
        if (next_page == 0) {
            cursor->end_of_table = true;
        } else {
            cursor->page_num = next_page;
            cursor->cell_num = 0;
        }
    }
}
```

**4. Get value at cursor:**
```cpp
void* cursor_value(Cursor* cursor) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    return get_leaf_node_value(node, cursor->cell_num);
}
```

**Use cases:**
- ✅ SELECT (full table scan)
- ✅ RANGE queries (start at min, advance to max)
- ✅ INSERT (find position, insert at cursor)
- ✅ DELETE (find key, delete at cursor)
- ✅ UPDATE (find key, modify at cursor)

---

### Q39: How does SELECT work (full table scan)?
**Answer:**

**Algorithm:**
```
1. Create cursor at leftmost leaf
2. While not end_of_table:
   - Read row at cursor
   - Print row
   - Advance cursor to next cell/page
3. Print total count
```

**Code:**
```cpp
ExecuteResult execute_select(Statement* statement, Table* table) {
    Cursor* cursor = table_start(table);
    Row row;
    uint32_t count = 0;
    
    while (!cursor->end_of_table) {
        // Get current page
        void* leaf_node = pager_get_page(table->pager, cursor->page_num);
        uint32_t num_cells = *get_leaf_node_num_cells(leaf_node);
        
        // Read all cells in current page
        while (cursor->cell_num < num_cells) {
            void* value = get_leaf_node_value(leaf_node, cursor->cell_num);
            deserialize_row(value, &row);
            print_row(&row);
            cursor->cell_num++;
            count++;
        }
        
        // Move to next page
        uint32_t next_leaf = *get_leaf_node_next_leaf(leaf_node);
        if (next_leaf == 0) {
            cursor->end_of_table = true;
        } else {
            cursor->page_num = next_leaf;
            cursor->cell_num = 0;
        }
    }
    
    cout << "Total rows: " << count << endl;
    delete cursor;
    return EXECUTE_SUCCESS;
}
```

**Visual traversal:**
```
Tree:
    Root
   /    \
Leaf1 → Leaf2 → Leaf3
[1-7]   [8-14]  [15-20]

Traversal:
1. cursor = table_start() → Leaf1, cell 0
2. Read cells 0-6 in Leaf1
3. Follow next_leaf → Leaf2
4. Read cells 0-6 in Leaf2
5. Follow next_leaf → Leaf3
6. Read cells 0-5 in Leaf3
7. next_leaf = 0 → end_of_table = true
```

**Key insight:** Uses sibling links (next_leaf) for efficient sequential access!

**Time Complexity:**
- Visit each leaf: O(L) where L = number of leaves
- Read each cell: O(n) where n = total records
- **Total: O(n)** - Must read all data

**I/O Complexity:**
- Best case: All leaves in cache → 0 disk I/O
- Worst case: Cache misses → L disk reads
- With 100-page cache: Most leaves stay cached!

---

### Q40: Explain FIND operation in detail.
**Answer:**

**Purpose:** Lookup single record by primary key

**Algorithm:**
```
1. Use table_find() to locate key
2. Check if key exists at cursor position
3. If found, deserialize and print row
4. If not found, return error
```

**Code:**
```cpp
ExecuteResult execute_find(Statement* statement, Table* table) {
    uint32_t key_to_find = statement->row_to_insert.id;
    
    // Find key position
    Cursor* cursor = table_find(table, key_to_find);
    void* node = pager_get_page(table->pager, cursor->page_num);
    
    // Check if key exists
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *get_leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key_to_find) {
            // Found!
            Row row;
            deserialize_row(get_leaf_node_value(node, cursor->cell_num), &row);
            print_row(&row);
            delete cursor;
            return EXECUTE_SUCCESS;
        }
    }
    
    // Not found
    delete cursor;
    return EXECUTE_RECORD_NOT_FOUND;
}
```

**Example:**

**Tree:**
```
Leaf: [10, 20, 30, 40, 50]
```

**Find key=30:**
```
1. Binary search → cursor at index 2
2. key_at_index = 30
3. 30 == 30 ✓
4. Return row at index 2
```

**Find key=25 (not exists):**
```
1. Binary search → cursor at index 2 (insertion point)
2. key_at_index = 30
3. 25 != 30 ✗
4. Return RECORD_NOT_FOUND
```

**Time Complexity:**
- Search: O(log n)
- Deserialize: O(1)
- **Total: O(log n)**

**Comparison to SELECT:**
| Operation | Time | Reads All? |
|-----------|------|------------|
| FIND | O(log n) | No, only target |
| SELECT | O(n) | Yes, all records |

---

### Q41: How does RANGE query work?
**Answer:**

**Purpose:** Get all records where `start_key <= key <= end_key`

**Algorithm:**
```
1. Find cursor at start_key
2. Advance cursor until key > end_key
3. Print matching records
4. Use sibling links for efficient traversal
```

**Code:**
```cpp
ExecuteResult execute_range(Statement* statement, Table* table) {
    uint32_t start_key = statement->range_start;
    uint32_t end_key = statement->range_end;
    
    // Start at first key >= start_key
    Cursor* cursor = table_find(table, start_key);
    Row row;
    uint32_t count = 0;
    
    while (!cursor->end_of_table) {
        void* leaf_node = pager_get_page(table->pager, cursor->page_num);
        uint32_t num_cells = *get_leaf_node_num_cells(leaf_node);
        
        while (cursor->cell_num < num_cells) {
            uint32_t current_key = *get_leaf_node_key(leaf_node, cursor->cell_num);
            
            // Stop if past end
            if (current_key > end_key) {
                cout << "Total rows in range: " << count << endl;
                delete cursor;
                return EXECUTE_SUCCESS;
            }
            
            // Print if in range
            if (current_key >= start_key && current_key <= end_key) {
                void* value = get_leaf_node_value(leaf_node, cursor->cell_num);
                deserialize_row(value, &row);
                print_row(&row);
                count++;
            }
            
            cursor->cell_num++;
        }
        
        // Move to next leaf
        uint32_t next_leaf = *get_leaf_node_next_leaf(leaf_node);
        if (next_leaf == 0) {
            cursor->end_of_table = true;
        } else {
            cursor->page_num = next_leaf;
            cursor->cell_num = 0;
        }
    }
    
    cout << "Total rows in range: " << count << endl;
    delete cursor;
    return EXECUTE_SUCCESS;
}
```

**Example:**

**Tree:**
```
Leaf1: [10, 20, 30]
Leaf2: [40, 50, 60]
Leaf3: [70, 80, 90]
```

**Range query [25, 65]:**
```
Step 1: Find 25
- Binary search → cursor at Leaf1, cell 2 (position of 30)

Step 2: Advance and print
- Read 30 ✓ (in range)
- Next cell → end of Leaf1
- Follow next_leaf → Leaf2
- Read 40 ✓ (in range)
- Read 50 ✓ (in range)
- Read 60 ✓ (in range)
- Next cell → end of Leaf2
- Follow next_leaf → Leaf3
- Read 70 ✗ (> 65, stop)

Result: [30, 40, 50, 60]
```

**Time Complexity:**
- Find start: O(log n)
- Traverse k results: O(k)
- **Total: O(log n + k)**

**Why efficient?**
- ✅ Direct jump to start (no full scan)
- ✅ Sequential leaf traversal (cache-friendly)
- ✅ Early termination when past end

---

### Q42: How does DELETE work?
**Answer:**

**High-level flow:**
```
1. Find key using table_find()
2. Check if key exists
3. If found, call leaf_node_delete()
4. Handle underflow if necessary
```

**Code:**
```cpp
ExecuteResult execute_delete(Statement* statement, Table* table) {
    uint32_t key_to_delete = statement->row_to_insert.id;
    
    // Find key
    Cursor* cursor = table_find(table, key_to_delete);
    void* node = pager_get_page(table->pager, cursor->page_num);
    
    // Check if exists
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *get_leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key_to_delete) {
            // Found! Delete it
            leaf_node_delete(cursor);
            delete cursor;
            return EXECUTE_SUCCESS;
        }
    }
    
    // Not found
    delete cursor;
    return EXECUTE_RECORD_NOT_FOUND;
}
```

**Leaf node delete (simple case - no underflow):**
```cpp
void leaf_node_delete(Cursor* cursor) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    
    // Shift cells left to fill gap
    for (uint32_t i = cursor->cell_num; i < num_cells - 1; i++) {
        void* dest = get_leaf_node_cell(node, i);
        void* src = get_leaf_node_cell(node, i + 1);
        memcpy(dest, src, LEAF_NODE_CELL_SIZE);
    }
    
    *get_leaf_node_num_cells(node) -= 1;
    mark_page_dirty(cursor->table->pager, cursor->page_num);
    
    // Check for underflow
    if (!is_node_root(node) && *get_leaf_node_num_cells(node) < LEAF_NODE_MIN_CELLS) {
        handle_leaf_underflow(cursor->table, cursor->page_num);
    }
}
```

**Visual example:**

**Before: Delete key=30**
```
Leaf: [10, 20, 30, 40, 50]  ← 5 cells
                  ↑ Delete index 2
```

**After shift left:**
```
Leaf: [10, 20, 40, 50, ??]  ← Shifted left
```

**After decrement:**
```
Leaf: [10, 20, 40, 50]  ← 4 cells
```

**Underflow scenario:**
```
Before delete: [10, 20, 30, 40, 50, 60, 70]  ← 7 cells (OK)
Delete 3 keys: [10, 20, 70]                  ← 3 cells (UNDERFLOW! MIN=6)
Action: handle_leaf_underflow() → borrow or merge
```

---

### Q43: How does UPDATE work?
**Answer:**

**Algorithm:**
```
1. Find key using table_find()
2. Check if key exists
3. If found, overwrite row data in place
4. Mark page dirty (CRITICAL!)
```

**Code:**
```cpp
ExecuteResult execute_update(Statement* statement, Table* table) {
    uint32_t key_to_update = statement->row_to_insert.id;
    
    // Find key
    Cursor* cursor = table_find(table, key_to_update);
    void* node = pager_get_page(table->pager, cursor->page_num);
    
    // Check if exists
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *get_leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key_to_update) {
            // Found! Update in place
            Row* row_to_update = &(statement->row_to_insert);
            serialize_row(row_to_update, get_leaf_node_value(node, cursor->cell_num));
            
            // CRITICAL: Mark page dirty!
            mark_page_dirty(cursor->table->pager, cursor->page_num);
            
            delete cursor;
            return EXECUTE_SUCCESS;
        }
    }
    
    // Not found
    delete cursor;
    return EXECUTE_RECORD_NOT_FOUND;
}
```

**Why update is simple:**
- ✅ Fixed-size rows → can overwrite in place
- ✅ No tree restructuring needed
- ✅ Just modify bytes and mark dirty

**Bug #1 (Before fix):**
```cpp
// ❌ Missing mark_page_dirty()
serialize_row(row, get_leaf_node_value(node, cursor->cell_num));
// Bug: Update visible in cache, lost after restart!
```

**After fix:**
```cpp
// ✅ Correct
serialize_row(row, get_leaf_node_value(node, cursor->cell_num));
mark_page_dirty(cursor->table->pager, cursor->page_num);
// Update persists to disk!
```

**Visual:**

**Before update:**
```
Cell: [key=42, username="alice", email="alice@test.com"]
```

**After update (key=42, username="bob", email="bob@test.com"):**
```
Cell: [key=42, username="bob", email="bob@test.com"]
```

**Note:** Key stays same (primary key constraint)

---

### Q44: What is table_start() and why is it needed?
**Answer:**

**Purpose:** Create cursor at leftmost position (smallest key)

**Why needed?**
- SELECT starts here
- RANGE queries may start here
- Any full table scan needs leftmost position

**Algorithm:**
```
1. Start at root
2. Follow leftmost child pointers down
3. Reach leftmost leaf
4. Return cursor at cell 0
```

**Code:**
```cpp
Cursor* table_start(Table* table) {
    // Find leftmost leaf (follow path for key 0)
    Cursor* cursor = table_find(table, 0);
    
    void* node = pager_get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    cursor->end_of_table = (num_cells == 0);
    
    return cursor;
}
```

**Why use table_find(0)?**
```cpp
// table_find(0) always goes left:
// - In internal nodes, 0 < all keys → leftmost child
// - In leaf, 0 < all keys → cursor at cell 0
```

**Visual:**
```
Tree:
          Root[50, 100]
         /      |       \
    [1-40]  [50-80]  [100-200]
     ↑ Leftmost

table_start():
1. Start at root
2. 0 < 50 → follow left child
3. Reach leftmost leaf
4. Return cursor(page_num=left_leaf, cell_num=0)
```

**Edge case - empty tree:**
```cpp
// Empty tree:
Root (leaf, 0 cells)

table_start():
- cursor->page_num = 0 (root)
- cursor->cell_num = 0
- cursor->end_of_table = true  ← Immediately at end!
```

---

### Q45: How do you check for duplicate keys before insert?
**Answer:**

**Requirement:** Primary key must be unique

**Algorithm:**
```
1. Use table_find() to locate key position
2. Check if key already exists at that position
3. If exists, reject with DUPLICATE_KEY error
4. If not exists, proceed with insert
```

**Code (in execute_insert):**
```cpp
ExecuteResult execute_insert(Statement* statement, Table* table) {
    Row* row_to_insert = &(statement->row_to_insert);
    uint32_t key = row_to_insert->id;
    
    // Find position for key
    Cursor* cursor = table_find(table, key);
    void* node = pager_get_page(table->pager, cursor->page_num);
    
    // Check if key already exists
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *get_leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key) {
            // DUPLICATE!
            delete cursor;
            return EXECUTE_DUPLICATE_KEY;
        }
    }
    
    // Not duplicate, check if split needed
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        leaf_node_split_and_insert(cursor, key, row_to_insert);
    } else {
        leaf_node_insert(cursor, key, row_to_insert);
    }
    
    delete cursor;
    return EXECUTE_SUCCESS;
}
```

**Why this works:**

**Case 1: Key exists**
```
Leaf: [10, 20, 30, 40]
Insert key=20:

table_find(20):
- Binary search → cursor at index 1
- key_at_index = 20
- 20 == 20 → DUPLICATE!
```

**Case 2: Key doesn't exist**
```
Leaf: [10, 20, 30, 40]
Insert key=25:

table_find(25):
- Binary search → cursor at index 2 (between 20 and 30)
- key_at_index = 30
- 25 != 30 → OK, insert at index 2
```

**Performance:**
- Duplicate check: O(log n)
- No additional overhead! (reuse search result)

---

### Q46: Explain cursor_advance() in detail.
**Answer:**

**Purpose:** Move cursor to next cell/page

**Algorithm:**
```
1. Increment cell_num
2. If at end of current leaf:
   - Follow next_leaf pointer
   - Reset cell_num to 0
3. If next_leaf is 0, set end_of_table
```

**Code:**
```cpp
void cursor_advance(Cursor* cursor) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    
    cursor->cell_num += 1;
    
    if (cursor->cell_num >= *get_leaf_node_num_cells(node)) {
        // Reached end of current leaf
        uint32_t next_page = *get_leaf_node_next_leaf(node);
        
        if (next_page == 0) {
            // No more leaves
            cursor->end_of_table = true;
        } else {
            // Move to next leaf
            cursor->page_num = next_page;
            cursor->cell_num = 0;
        }
    }
}
```

**Visual trace:**

**Tree:**
```
Leaf1[1,2,3] → Leaf2[4,5,6] → Leaf3[7,8,9] → NULL
```

**Advance sequence:**
```
Initial: cursor(Leaf1, cell=0) → value 1

Advance #1: cursor(Leaf1, cell=1) → value 2
Advance #2: cursor(Leaf1, cell=2) → value 3
Advance #3: cursor(Leaf1, cell=3) → cell_num >= 3
            Follow next_leaf → cursor(Leaf2, cell=0) → value 4
Advance #4: cursor(Leaf2, cell=1) → value 5
...
Advance #8: cursor(Leaf3, cell=2) → value 9
Advance #9: cursor(Leaf3, cell=3) → cell_num >= 3
            next_leaf = 0 → end_of_table = true
```

**Key insight:** Sibling links enable O(1) advancement!

**Without sibling links:**
```cpp
// ❌ Would need to traverse from root each time!
void cursor_advance_slow(Cursor* cursor) {
    uint32_t next_key = current_key + 1;
    cursor = table_find(table, next_key);  // O(log n) each time!
}
```

**With sibling links:**
```cpp
// ✅ O(1) advancement
cursor->cell_num++;  // Just increment!
```

---

### Q47: What is the time complexity of each operation?
**Answer:**

**Summary table:**

| Operation | Time Complexity | Disk I/O | Notes |
|-----------|----------------|----------|-------|
| **INSERT** | O(log n) avg | O(log n) | May cause split |
| **FIND** | O(log n) | O(log n) | Binary search at each level |
| **DELETE** | O(log n) | O(log n) | May cause merge |
| **UPDATE** | O(log n) | O(log n) | Find + modify in place |
| **SELECT** | O(n) | O(L) | L = leaf count |
| **RANGE** | O(log n + k) | O(log n + L') | k = results, L' = leaves touched |
| **Split** | O(M) | O(1) | M = max keys per node |
| **Merge** | O(M) | O(1) | M = max keys per node |

**Detailed analysis:**

**INSERT:**
```
Find position: O(h × log M)
  where h = tree height = O(log_M n)
  M = branching factor (13 for leaf, 510 for internal)
Insert into leaf: O(M) for shift
Mark dirty: O(log n)
Total: O(log n)

With split:
  Split cost: O(M)
  Update parent: O(log M)
  Total still: O(log n + M) = O(log n)
```

**SELECT:**
```
Visit all leaves: O(L)
  where L = ⌈n / M⌉ = number of leaves
Read all cells: O(n)
Total: O(n)

Disk I/O:
  Best: 0 (all in cache)
  Worst: L reads
  Typical: Few reads (cache hit rate ~90%)
```

**RANGE [a, b]:**
```
Find start: O(log n)
Traverse k results: O(k)
Total: O(log n + k)

If k = n: O(n) (degenerates to SELECT)
If k = 1: O(log n) (like FIND)
```

**Why O(log n) is good:**

| n (records) | log₁₃ n (height) | Disk reads |
|-------------|------------------|------------|
| 100 | ~2 | 2-3 |
| 1,000 | ~3 | 3-4 |
| 10,000 | ~4 | 4-5 |
| 100,000 | ~5 | 5-6 |
| 1,000,000 | ~6 | 6-7 |
| 10,000,000 | ~7 | 7-8 |

**Even 10M records = only 7-8 disk reads!**

---

### Q48: How do you get the maximum key in a node?
**Answer:**

**Purpose:** Used for parent separator keys

**For leaf nodes:**
```cpp
uint32_t get_node_max_key(Pager* pager, void* node) {
    if (get_node_type(node) == NODE_LEAF) {
        uint32_t num_cells = *get_leaf_node_num_cells(node);
        return *get_leaf_node_key(node, num_cells - 1);  // Last key
    } else {
        // Internal node - recursively get from rightmost child
        uint32_t right_child = *get_internal_node_right_child(node);
        void* right_child_node = pager_get_page(pager, right_child);
        return get_node_max_key(pager, right_child_node);  // Recurse
    }
}
```

**Why needed?**

**Internal node structure:**
```
Internal: [child0, key0, child1, key1, ..., childN, right_child]

Where:
  key[i] = max key in subtree rooted at child[i]
```

**Example:**
```
Internal: [child0, 50, child1, 100, right_child]

Meaning:
  - child0 subtree: keys ≤ 50
  - child1 subtree: keys ≤ 100
  - right_child subtree: keys > 100
```

**After inserting into child0:**
```
Before:
  child0 max key = 40
  key0 = 40

After insert key=50 into child0:
  child0 max key = 50
  key0 = 50  ← Must update!

Code:
  uint32_t new_max = get_node_max_key(pager, child0);
  *get_internal_node_key(parent, 0) = new_max;
```

**Recursive example:**

**Tree:**
```
          Root (Internal)
         /                \
    Child0 (Internal)   Child1 (Leaf)
    /              \       [90, 95, 100]
Leaf1 [10,20]   Leaf2 [30,40,50]
```

**Get max key of root:**
```
get_node_max_key(root):
  → root is internal
  → right_child = Child1
  → get_node_max_key(Child1):
    → Child1 is leaf
    → return 100
  → return 100
```

**Get max key of Child0:**
```
get_node_max_key(Child0):
  → Child0 is internal
  → right_child = Leaf2
  → get_node_max_key(Leaf2):
    → Leaf2 is leaf
    → return 50
  → return 50
```

---

### Q49: How do internal nodes store keys and children?
**Answer:**

**Layout:**
```
Internal Node (4096 bytes):
  [Common Header: 6 bytes]
  [Internal Header: 8 bytes]
  [Key-Child Pairs: up to 510]
  
Structure:
  child0, key0, child1, key1, ..., child509, key509, right_child
```

**Why this layout?**

**Invariant:**
```
All keys in subtree(child[i]) ≤ key[i]
All keys in subtree(child[i+1]) > key[i]
```

**Example:**
```
Internal: [5, 20, 10, 40, 15, 60, 20]
          ↑   ↑   ↑   ↑   ↑   ↑   ↑
         c0  k0  c1  k1  c2  k2  rc

Meaning:
  child0 (page 5): keys ≤ 20
  child1 (page 10): keys ≤ 40 (and > 20)
  child2 (page 15): keys ≤ 60 (and > 40)
  right_child (page 20): keys > 60
```

**Accessor functions:**
```cpp
uint32_t* get_internal_node_num_keys(void* node) {
    return (uint32_t*)((char*)node + 6);
}

uint32_t* get_internal_node_right_child(void* node) {
    return (uint32_t*)((char*)node + 10);
}

uint32_t* get_internal_node_child(void* node, uint32_t child_num) {
    uint32_t offset = INTERNAL_NODE_HEADER_SIZE + 
                     (child_num * INTERNAL_NODE_CELL_SIZE);
    return (uint32_t*)((char*)node + offset);
}

uint32_t* get_internal_node_key(void* node, uint32_t key_num) {
    uint32_t offset = INTERNAL_NODE_HEADER_SIZE + 
                     (key_num * INTERNAL_NODE_CELL_SIZE) +
                     sizeof(uint32_t);  // Skip child pointer
    return (uint32_t*)((char*)node + offset);
}
```

**Search in internal node:**
```cpp
// Given key K, find correct child
uint32_t num_keys = *get_internal_node_num_keys(node);

// Binary search
uint32_t min = 0, max = num_keys;
while (min != max) {
    uint32_t mid = (min + max) / 2;
    if (K <= *get_internal_node_key(node, mid)) {
        max = mid;
    } else {
        min = mid + 1;
    }
}

// Get child pointer
uint32_t child_page;
if (min == num_keys) {
    child_page = *get_internal_node_right_child(node);
} else {
    child_page = *get_internal_node_child(node, min);
}
```

---

### Q50: What is the sibling link (next_leaf) used for?
**Answer:**

**Purpose:** Link leaf nodes in sorted order for sequential access

**Structure:**
```
Leaf nodes form a linked list:
Leaf1 → Leaf2 → Leaf3 → NULL
```

**Stored in leaf header:**
```cpp
struct LeafHeader {
    uint32_t num_cells;   // Cell count
    uint32_t next_leaf;   // Page number of next sibling (0 = none)
};
```

**Why needed?**

**Without sibling links:**
```cpp
// ❌ Expensive: Must traverse from root for each next record
Cursor* cursor = table_start(table);
for (int i = 0; i < 1000; i++) {
    Row row = cursor_value(cursor);
    // To advance: Must search from root!
    cursor = table_find(table, row.id + 1);  // O(log n) each time!
}
// Total: O(n log n) for sequential scan!
```

**With sibling links:**
```cpp
// ✅ Efficient: Follow linked list
Cursor* cursor = table_start(table);
for (int i = 0; i < 1000; i++) {
    Row row = cursor_value(cursor);
    cursor_advance(cursor);  // O(1) - just follow next_leaf!
}
// Total: O(n) for sequential scan!
```

**Operations using sibling links:**

**1. SELECT (full table scan):**
```cpp
// Start at leftmost leaf
Cursor* cursor = table_start(table);

while (!cursor->end_of_table) {
    // Read current leaf
    // ...
    
    // Move to next leaf via sibling link
    uint32_t next = *get_leaf_node_next_leaf(current_node);
    if (next == 0) {
        end_of_table = true;
    } else {
        page_num = next;  // Follow link!
    }
}
```

**2. RANGE query:**
```cpp
// Start at first key in range
Cursor* cursor = table_find(table, start_key);

while (current_key <= end_key) {
    // Read current record
    // ...
    
    // Advance via sibling link
    cursor_advance(cursor);  // Uses next_leaf
}
```

**Maintaining sibling links:**

**During split:**
```cpp
void leaf_node_split_and_insert(...) {
    // Old node keeps its next pointer initially
    uint32_t old_next = *get_leaf_node_next_leaf(old_node);
    
    // New node points to old node's successor
    *get_leaf_node_next_leaf(new_node) = old_next;
    
    // Old node now points to new node
    *get_leaf_node_next_leaf(old_node) = new_page_num;
    
    // Result: old → new → old_next
}
```

**Visual:**

**Before split:**
```
Leaf A → Leaf C
```

**After splitting Leaf A into A and B:**
```
Leaf A → Leaf B → Leaf C
       ↑ New link
```

**Benefit summary:**
- ✅ O(n) sequential scans (vs O(n log n))
- ✅ Cache-friendly access pattern
- ✅ No tree traversal needed
- ✅ Simple to maintain (only during splits)

---

### Q51: How do you find a child's index in its parent?
**Answer:**

**Purpose:** Needed for delete, merge, borrow operations

**Algorithm:**
```
1. Get parent node
2. Iterate through children
3. Find matching page number
4. Return index
```

**Code:**
```cpp
int32_t find_child_index_in_parent(void* parent, uint32_t child_page_num) {
    uint32_t num_keys = *get_internal_node_num_keys(parent);
    
    // Check regular children
    for (uint32_t i = 0; i < num_keys; i++) {
        uint32_t child = *get_internal_node_child(parent, i);
        if (child == child_page_num) {
            return i;
        }
    }
    
    // Check right child
    uint32_t right = *get_internal_node_right_child(parent);
    if (right == child_page_num) {
        return num_keys;  // Virtual index for right child
    }
    
    return -1;  // Not found (error!)
}
```

**Why needed?**

**Example: Delete causing underflow**
```cpp
void handle_leaf_underflow(Table* table, uint32_t page_num) {
    void* node = pager_get_page(table->pager, page_num);
    uint32_t parent_page = *get_node_parent(node);
    void* parent = pager_get_page(table->pager, parent_page);
    
    // Need to know which child this is!
    int32_t child_index = find_child_index_in_parent(parent, page_num);
    
    // Now can find siblings
    if (child_index > 0) {
        // Has left sibling at index child_index - 1
        uint32_t left_sibling = *get_internal_node_child(parent, child_index - 1);
    }
    
    if (child_index < num_keys - 1) {
        // Has right sibling at index child_index + 1
        uint32_t right_sibling = *get_internal_node_child(parent, child_index + 1);
    }
}
```

**Visual:**
```
Parent: [child0, key0, child1, key1, child2, right_child]
         page5         page7         page9    page11

To find index of page 7:
  Iterate: child0=5 (no), child1=7 (yes!)
  Return: index = 1

To find index of page 11:
  Iterate: child0=5 (no), child1=7 (no), child2=9 (no)
  Check right_child=11 (yes!)
  Return: index = 3 (num_keys)
```

**Time Complexity:**
- O(M) where M = max keys per internal node
- In practice: M ≤ 510, so very fast

**Why not store index in child?**
- ❌ Would need to update on every rebalance
- ✅ Searching is fast enough (510 iterations max)

---

### Q52: Explain parent pointer updates during operations.
**Answer:**

**Every node stores its parent page number:**
```cpp
struct CommonHeader {
    uint8_t node_type;
    uint8_t is_root;
    uint32_t parent;  // ← Parent page number
};
```

**Why needed?**
- Find siblings during underflow
- Traverse up tree
- Update parent keys

**Critical operations requiring parent updates:**

**1. Split (new node needs parent):**
```cpp
void leaf_node_split_and_insert(...) {
    // New node inherits same parent
    *get_node_parent(new_node) = *get_node_parent(old_node);
    mark_page_dirty(pager, new_page_num);
    
    // If root split, both children get new parent
    if (is_node_root(old_node)) {
        create_new_root(table, new_page_num);
        // Inside create_new_root:
        *get_node_parent(old_node) = new_root_page;
        *get_node_parent(new_node) = new_root_page;
    }
}
```

**2. Merge (update remaining node):**
```cpp
void handle_leaf_underflow(...) {
    // After merging left into current:
    // Current node's parent unchanged
    
    // But if parent underflows and merges:
    // Children of merged parent need new parent!
    for (uint32_t i = 0; i < num_children; i++) {
        void* child = pager_get_page(pager, child_pages[i]);
        *get_node_parent(child) = new_parent_page;
        mark_page_dirty(pager, child_pages[i]);
    }
}
```

**3. Borrow (borrowed child needs new parent):**
```cpp
void handle_internal_underflow(...) {
    // Borrow child from sibling
    uint32_t borrowed_child = ...;
    
    // Update borrowed child's parent pointer!
    void* borrowed_child_node = pager_get_page(pager, borrowed_child);
    *get_node_parent(borrowed_child_node) = current_page_num;
    mark_page_dirty(pager, borrowed_child);
}
```

**4. Root change (new root has no parent):**
```cpp
void create_new_root(...) {
    *get_node_parent(new_root) = 0;  // Root has no parent
    set_node_root(new_root, true);
    
    // Old root now has parent
    *get_node_parent(old_root) = new_root_page;
    set_node_root(old_root, false);
}
```

**Common bug pattern:**
```cpp
// ❌ BAD: Forgot to update parent pointer
leaf_node_split(...);
// new_node created but parent not set!
// Later operations will fail!

// ✅ GOOD: Always update parent
*get_node_parent(new_node) = parent_page;
mark_page_dirty(pager, new_page_num);
```

**Validation check:**
```cpp
// In validate_tree_node():
for (child in node.children) {
    void* child_node = pager_get_page(pager, child);
    uint32_t child_parent = *get_node_parent(child_node);
    
    if (child_parent != current_page_num) {
        cout << "ERROR: Parent pointer mismatch!" << endl;
        return false;
    }
}
```

---

## 📝 Summary - Part 3 Topics Covered

✅ **Search Algorithm** - Tree traversal, binary search  
✅ **Insert (Simple)** - Leaf insert without split  
✅ **Cursor** - Iterator pattern, purpose, operations  
✅ **SELECT** - Full table scan with sibling links  
✅ **FIND** - Single record lookup  
✅ **RANGE** - Efficient range queries  
✅ **DELETE** - Remove with shift left  
✅ **UPDATE** - In-place modification  
✅ **table_start()** - Leftmost position  
✅ **Duplicate Check** - Primary key enforcement  
✅ **cursor_advance()** - O(1) advancement  
✅ **Time Complexity** - All operations analyzed  
✅ **get_node_max_key()** - Recursive max finding  
✅ **Internal Node Layout** - Key-child pairs  
✅ **Sibling Links** - Sequential access optimization  
✅ **find_child_index** - Locating child in parent  
✅ **Parent Pointers** - Bidirectional links  

---

**Next:** [VIVA_QUESTIONS_PART4.md](./VIVA_QUESTIONS_PART4.md) - Split, Merge, Borrow (Most Complex!)

**See Also:**
- [CODE_WALKTHROUGH_PART2.md](./CODE_WALKTHROUGH_PART2.md) - Detailed operations
- [07_btree_split_merge_borrow.md](./07_btree_split_merge_borrow.md) - Algorithm deep dive
