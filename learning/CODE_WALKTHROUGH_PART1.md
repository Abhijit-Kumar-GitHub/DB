# 🔍 ArborDB Complete Code Walkthrough

## Part 1: Architecture Overview

### The Big Picture
```
User Input
    ↓
REPL (main loop)
    ↓
Parser → Statement
    ↓
Executor → CRUD Operations
    ↓
B-Tree Layer (insert/delete/split/merge)
    ↓
Pager (LRU cache + disk I/O)
    ↓
Binary File (pages)
```

---

## Part 2: File Structure Analysis (`db.hpp`)

### 📦 Critical Constants Explained

#### File Header (8 bytes)
```cpp
#define DB_FILE_HEADER_SIZE 8
// Byte 0-3: root_page_num (which page is the root?)
// Byte 4-7: free_head (first page in freelist)
```

**Why these two?**
- `root_page_num`: Need to know where tree starts after restart
- `free_head`: Reuse deleted pages (avoid file growth)

#### Page Size (4096 bytes)
```cpp
#define PAGE_SIZE 4096  // Why 4KB?
```

**Reasons:**
1. Matches OS page size (efficient I/O)
2. Common disk sector alignment
3. Standard in databases (SQLite, PostgreSQL)
4. Good balance between memory usage and I/O efficiency

#### Row Schema (Fixed 291 bytes on disk)
```cpp
typedef struct {
    uint32_t id;                    // 4 bytes
    char username[32 + 1];          // 33 bytes (memory)
    char email[255 + 1];            // 256 bytes (memory)
} Row;  // 293 bytes in memory

// But on disk:
#define ROW_SIZE (4 + 32 + 255)  // 291 bytes (no null terminators)
```

**Why fixed size?**
- O(1) offset calculation
- Predictable capacity (13 rows per leaf)
- Simple memory management

**Trade-off:**
- "alice" uses same space as "verylongusernamethirtytwochar"

---

### 🗂️ Data Structures Deep Dive

#### 1. Pager - The Memory Manager
```cpp
typedef struct {
    std::fstream file_stream;     // Binary file handle
    uint32_t file_length;         // Track file size
    uint32_t root_page_num;       // 🔑 Persisted in header!
    uint32_t free_head;           // 🔑 Persisted in header!
    
    // LRU Cache (100 pages = 400KB max)
    std::map<uint32_t, void*> page_cache;              // page_num → data
    std::list<uint32_t> lru_list;                      // MRU...→...LRU
    std::map<uint32_t, std::list<uint32_t>::iterator> lru_map;  // page_num → position
    std::set<uint32_t> dirty_pages;                    // Modified pages
    
    uint32_t num_pages;           // Total pages allocated
} Pager;
```

**The LRU Cache Trio:**

| Structure | Purpose | Time Complexity |
|-----------|---------|-----------------|
| `page_cache` | Store actual page data | O(log n) lookup |
| `lru_list` | Track access order | O(1) front/back |
| `lru_map` | Fast position lookup | O(log n) |

**Why three structures?**
- `page_cache` alone: Can't evict efficiently
- `lru_list` alone: Can't find page quickly
- Together: Best of both worlds!

#### 2. Table - Thin Wrapper
```cpp
typedef struct {
    Pager* pager;  // That's it!
} Table;
```

**Why so simple?**
- Pager does all the work
- Table is just a convenience wrapper
- Could extend later (e.g., schema info, indexes)

#### 3. Cursor - Tree Iterator
```cpp
typedef struct {
    Table* table;        // Which table?
    uint32_t page_num;   // Current page
    uint32_t cell_num;   // Current cell in page
    bool end_of_table;   // Past last record?
} Cursor;
```

**Used for:**
- SELECT (iterate all rows)
- FIND (position at specific key)
- RANGE (iterate between keys)

---

### 🌲 B-Tree Node Layouts

#### Common Header (6 bytes - ALL nodes)
```cpp
Byte 0: [node_type]    // 1 byte: LEAF or INTERNAL
Byte 1: [is_root]      // 1 byte: boolean
Byte 2-5: [parent]     // 4 bytes: parent page number
```

**Why common header?**
- Polymorphism without C++ virtual functions
- Can treat any page as "node" initially
- Determine type dynamically

#### Leaf Node Layout (4096 bytes total)
```
[Common: 6][num_cells: 4][next_leaf: 4] ← Header (14 bytes)
[key: 4][value: 291]                     ← Cell 0 (295 bytes)
[key: 4][value: 291]                     ← Cell 1 (295 bytes)
...
[key: 4][value: 291]                     ← Cell 12 (295 bytes)
[unused: 247 bytes]                      ← Wasted space
```

**Capacity calculation:**
```cpp
(4096 - 14) / 295 = 13.83... → floor to 13 cells max
```

**Why next_leaf pointer?**
- Enables efficient range queries
- Doubly-linked list of leaves
- Walk leaves without going up tree

#### Internal Node Layout
```
[Common: 6][num_keys: 4][right_child: 4] ← Header (14 bytes)
[child0: 4][key0: 4]                      ← Pair 0 (8 bytes)
[child1: 4][key1: 4]                      ← Pair 1 (8 bytes)
...
[child509: 4][key509: 4]                  ← Pair 509 (8 bytes)
```

**Capacity:**
```cpp
(4096 - 14) / 8 = 510.25 → 510 key-child pairs
Plus 1 right_child (stored in header) → 511 total children
```

**Key insight:** Internal nodes have MANY more keys than leaves
- Leaves: 13 keys max
- Internal: 510 keys max
- This makes the tree very **wide and short**

---

## Part 3: The Node Accessor Pattern

### Why Accessor Functions?

**Problem:** Nodes are `void*` blobs, need to read/write fields

**Bad approach:**
```cpp
// ❌ Can't do this - no struct definition!
LeafNode* leaf = (LeafNode*)node;
uint32_t num_cells = leaf->num_cells;
```

**Your solution: Pointer arithmetic accessors**

### Example: Reading num_cells from Leaf

```cpp
uint32_t* get_leaf_node_num_cells(void* node) {
    return reinterpret_cast<uint32_t*>(
        static_cast<char*>(node) + LEAF_NODE_NUM_CELLS_OFFSET
    );
}
```

**Step-by-step breakdown:**

1. **`node`** is `void*` (can't do arithmetic on void*)
2. **`static_cast<char*>(node)`** → Convert to `char*` (1-byte units)
3. **`+ LEAF_NODE_NUM_CELLS_OFFSET`** → Move 6 bytes (past common header)
4. **`reinterpret_cast<uint32_t*>(...)`** → Treat those 4 bytes as uint32_t
5. **Return pointer** → Caller dereferences: `*get_leaf_node_num_cells(node)`

**Visual:**
```
node (void*)
  ↓
[0][1][2][3][4][5][6][7][8][9][10]...
 ↑           ↑     ↑
 type        root  parent
             ↑
             └─ OFFSET 6: num_cells (4 bytes)
```

### Pattern Used Throughout

**For reading:**
```cpp
uint32_t num_cells = *get_leaf_node_num_cells(node);
```

**For writing:**
```cpp
uint32_t* num_cells_ptr = get_leaf_node_num_cells(node);
*num_cells_ptr = 5;  // Update value
mark_page_dirty(pager, page_num);  // ⚠️ CRITICAL!
```

---

## Part 4: The Pager Implementation

### Opening a Database File

```cpp
Pager* pager_open(const string& filename) {
    Pager* pager = new Pager();
    
    // Try to open existing file
    pager->file_stream.open(filename, ios::in | ios::out | ios::binary);
    
    if (!pager->file_stream.is_open()) {
        // NEW FILE: Create and initialize
        pager->file_stream.open(filename, 
            ios::in | ios::out | ios::binary | ios::trunc);
        
        pager->root_page_num = 0;  // Root at page 0
        pager->free_head = 0;       // No free pages
        pager->num_pages = 1;       // Just root
        
        // Write header
        pager->file_stream.seekp(0);
        pager->file_stream.write(&pager->root_page_num, 4);
        pager->file_stream.write(&pager->free_head, 4);
        
        // Allocate root page (all zeros)
        char zero_page[4096] = {0};
        pager->file_stream.write(zero_page, 4096);
        
    } else {
        // EXISTING FILE: Read header
        pager->file_stream.seekg(0);
        pager->file_stream.read(&pager->root_page_num, 4);
        pager->file_stream.read(&pager->free_head, 4);
        
        // Calculate file size and page count
        pager->file_stream.seekg(0, ios::end);
        pager->file_length = pager->file_stream.tellg();
        pager->num_pages = (pager->file_length - 8) / 4096;
    }
    
    return pager;
}
```

**Key decisions:**
- New file gets 8-byte header + 1 empty page
- Existing file reads header to restore state
- Root page number is **persisted** (survives restart)

---

### The LRU Cache Algorithm

**Goal:** Keep 100 pages in memory, evict least recently used when full

#### Operation 1: Cache Hit
```cpp
if (pager->page_cache.count(page_num)) {
    // Step 1: Remove from current position in LRU list
    auto it = pager->lru_map[page_num];
    pager->lru_list.erase(it);
    
    // Step 2: Add to front (most recently used)
    pager->lru_list.push_front(page_num);
    
    // Step 3: Update map with new position
    pager->lru_map[page_num] = pager->lru_list.begin();
    
    return pager->page_cache[page_num];
}
```

**Effect:** Page moves to front of LRU list

**Before:** `[42] ⟷ [17] ⟷ [5] ⟷ [9]` (accessing 17)  
**After:** `[17] ⟷ [42] ⟷ [5] ⟷ [9]`

#### Operation 2: Cache Miss + Eviction
```cpp
// Cache is full
if (pager->page_cache.size() >= 100) {
    // Get LRU page (back of list)
    uint32_t lru_page_num = pager->lru_list.back();
    void* lru_page_data = pager->page_cache[lru_page_num];
    
    // Flush if dirty (OPTIMIZATION!)
    if (pager->dirty_pages.count(lru_page_num)) {
        pager_flush(pager, lru_page_num);
        pager->dirty_pages.erase(lru_page_num);
    }
    
    // Remove from all structures
    pager->lru_list.pop_back();
    pager->lru_map.erase(lru_page_num);
    delete[] static_cast<char*>(lru_page_data);
    pager->page_cache.erase(lru_page_num);
}

// Load new page from disk
void* new_page = new char[4096];
pager->file_stream.seekg(offset);
pager->file_stream.read(new_page, 4096);

// Add to cache (MRU position)
pager->page_cache[page_num] = new_page;
pager->lru_list.push_front(page_num);
pager->lru_map[page_num] = pager->lru_list.begin();
```

**The dirty page optimization:**
- Only flush modified pages to disk
- Clean pages can be discarded (already on disk!)
- **Saves ~90% of writes** in read-heavy workloads

---

## 🎯 What's Next?

This is Part 1 of the code walkthrough. We've covered:
✅ Architecture overview
✅ Data structures (Pager, Table, Cursor)
✅ Node layouts (leaf & internal)
✅ Accessor pattern
✅ LRU cache implementation

**Next sections to explore:**
- Part 2: B-Tree Operations (insert, search, delete)
- Part 3: Split/Merge/Borrow algorithms
- Part 4: Serialization & persistence
- Part 5: Tree validation & testing

**Which part would you like me to explain next?**
