# 🎓 Topic #5: Binary Search Trees (BST) → B-Trees

## Why This Matters for ArborDB
B-Trees are generalized BSTs. Understanding BST properties and operations is essential before tackling B-Tree split/merge/borrow logic.

---

## BST Recap (Foundation)

**BST Property:** For every node:
- All left descendants < node
- All right descendants > node

**Example:**
```
       10
      /  \
     5    15
    / \   / \
   3   7 12  20
```

**Key operations:**
- Search: O(log n) average, O(n) worst case
- Insert: O(log n) average
- Delete: O(log n) average

**Problem with BSTs for databases:**
- Can become unbalanced → O(n) operations
- Each node = 1 disk read (expensive!)
- Height grows quickly (log₂ n levels)

---

## B-Trees: Multi-Way Search Trees

**Key differences from BST:**
- **Multiple keys per node** (not just 1)
- **Multiple children per node** (not just 2)
- **Guaranteed balance** (all leaves at same level)
- **Higher branching factor** → shorter trees

**Your B-Tree:**
- Leaf nodes: up to 13 keys
- Internal nodes: up to 510 keys
- Min 50% full (except root)

---

## B-Tree Structure

**Internal node with 3 keys:**
```
[child0 | key0 | child1 | key1 | child2 | key2 | child3]
        ↑              ↑              ↑
      < 10          < 20           < 30          ≥ 30
```

**Example B-Tree:**
```
             [10, 20, 30]
            /    |    |   \
         /       |    |      \
    [2,5,8]  [12,15] [22,25] [35,40,50]
```

**Key properties:**
1. All leaves at same level
2. Internal node with k keys has k+1 children
3. Keys sorted within each node
4. Child₀ < key₀ ≤ child₁ < key₁ ≤ child₂ ...

---

## Your B-Tree Node Types

### Leaf Nodes
```cpp
#define LEAF_NODE_MAX_CELLS 13   // Maximum keys
#define LEAF_NODE_MIN_CELLS 6    // Minimum keys (50%)

// Structure:
// [Header: 14 bytes][Cell 0][Cell 1]...[Cell 12]
// Each cell: [key: 4 bytes][value: 291 bytes]
```

**Why 13?**
- (4096 - 14) / 295 = 13.83... → floor to 13
- Leaves space: 4096 - 14 - (13 * 295) = 247 bytes unused

### Internal Nodes
```cpp
#define INTERNAL_NODE_MAX_KEYS 510
#define INTERNAL_NODE_MIN_KEYS 255

// Structure:
// [Header: 14 bytes][Child0:4][Key0:4][Child1:4][Key1:4]...[RightChild:4]
```

**Why 510?**
- (4096 - 14) / 8 = 510.25 → floor to 510
- Each entry: child pointer (4) + key (4) = 8 bytes
- Plus 1 rightmost child pointer (stored separately)

---

## B-Tree Operations

### 1. Search
```
To find key K:
1. Start at root
2. Binary search within node for key or correct child
3. If key found → return
4. If leaf reached and not found → key doesn't exist
5. Otherwise, recursively search child pointer
```

**Your implementation:**
```cpp
Cursor* table_find(Table* table, uint32_t key) {
    uint32_t root_page_num = table->pager->root_page_num;
    void* root_node = pager_get_page(table->pager, root_page_num);
    
    if (get_node_type(root_node) == NODE_LEAF) {
        return leaf_node_find(table, root_page_num, key);
    } else {
        return internal_node_find(table, root_page_num, key);
    }
}
```

### 2. Insert (with split)
```
To insert key K:
1. Find correct leaf node
2. If leaf has space → insert key in sorted order
3. If leaf is full → split into two nodes:
   - Create new node
   - Move half the keys to new node
   - Insert new key in appropriate node
   - Add separator key to parent
4. If parent full → recursively split parent
5. If root splits → create new root (tree grows)
```

### 3. Delete (with merge/borrow)
```
To delete key K:
1. Find and remove key from leaf
2. If leaf has enough keys (≥ min) → done
3. If leaf underflows → try to borrow from sibling
4. If can't borrow → merge with sibling
5. Update parent (may cascade upward)
```

---

## Split Operation (Critical!)

**When:** Node exceeds MAX capacity

**Your leaf split:**
```cpp
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
    // 1. Get old (full) node
    void* old_node = pager_get_page(cursor->table->pager, cursor->page_num);
    
    // 2. Allocate new node
    uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
    void* new_node = pager_get_page(cursor->table->pager, new_page_num);
    initialize_leaf_node(new_node);
    
    // 3. Redistribute keys: old gets SPLIT_LEFT, new gets SPLIT_RIGHT
    // (7 keys in old, 7 keys in new, with new key that's 14 total)
    
    // 4. Update parent with separator key
    uint32_t new_max_key = get_node_max_key(pager, new_node);
    
    // 5. If old node was root → create new root
    // Otherwise → insert into parent
}
```

---

## Merge/Borrow Operations

**When:** Node drops below MIN capacity after delete

**Option 1: Borrow from sibling**
```
Before (delete 12):
Left: [5, 10]        Right: [20, 25, 30]
Parent: [15]

After:
Left: [5, 10, 20]    Right: [25, 30]
Parent: [22]  (updated separator)
```

**Option 2: Merge siblings**
```
Before (delete 10):
Left: [5]            Right: [20, 25]
Parent: [15]

After:
Merged: [5, 15, 20, 25]
Parent: (15 removed)
```

---

## Why B-Trees for Databases?

**Disk efficiency:**
- 1 node = 1 disk read
- More keys per node → fewer disk reads
- Height grows slowly (log_m n where m = branching factor)

**Your tree with 1 million rows:**
- Leaf nodes: ~77,000 pages (1,000,000 / 13)
- Height ≈ 3 (log₅₁₀ 77,000)
- **Only 3 disk reads** to find any key!

**vs BST:**
- 1 key per node
- Height ≈ 20 (log₂ 1,000,000)
- **20 disk reads** for same operation

---

## Key Invariants Your Code Maintains

1. **All leaves at same level** (balanced tree)
2. **Keys sorted within nodes** (binary search works)
3. **Min fill factor** (≥50% except root)
4. **Parent keys updated** (separators reflect child max keys)
5. **Sibling links in leaves** (for range queries)

---

## Quick Self-Test

1. What's the maximum height of your B-Tree with 10,000 rows?
2. Why split at 50% instead of moving just 1 key?
3. What triggers a borrow vs merge operation?
4. Why store multiple keys per node?
5. What happens when the root splits?

**Answers:**
1. ~2-3 levels (log₅₁₀ 770 ≈ 1.5, plus leaf level)
2. Maintains balance and avoids immediate re-split
3. Borrow if sibling has > MIN keys, merge if sibling at MIN
4. Reduces tree height → fewer disk I/O operations
5. New root created with 1 key, tree height increases by 1

---

**Ready for Topic #6: LRU Cache Deep Dive?** 🚀
