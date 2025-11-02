# 🎓 ArborDB Viva/Interview Questions - Part 2: Architecture & Implementation

## Overview
This part covers Pager, LRU cache, node structures, and memory management details.

---

## 📦 Section 2: Pager & Memory Management (15 Questions)

### Q21: What is the Pager and why is it needed?
**Answer:**

**Pager = Memory Manager + Cache + Disk I/O Handler**

**Why needed?**

**Problem without Pager:**
```cpp
// Naive approach - load entire database into RAM
Row* all_data = load_entire_file();  // ❌ 1GB file = 1GB RAM!
```

**Solution with Pager:**
```cpp
// Smart approach - load only needed pages
void* page = pager_get_page(pager, page_num);  // ✅ Only 4KB loaded
```

**Key Functions:**
1. **Load pages on demand** - Lazy loading
2. **Cache hot pages** - LRU cache (100 pages)
3. **Track modifications** - Dirty page set
4. **Write only changed pages** - Efficiency
5. **Manage freelist** - Page recycling

**Benefits:**
- ✅ Constant memory usage (400 KB cache)
- ✅ Fast access to hot data
- ✅ Minimal disk I/O
- ✅ Scalable to large datasets

---

### Q22: Explain the LRU Cache implementation in detail.
**Answer:**

**LRU = Least Recently Used eviction policy**

**Data Structures (3 working together):**

```cpp
struct Pager {
    // 1. Cache storage: page_num → page_data
    std::map<uint32_t, void*> page_cache;
    
    // 2. LRU order: most recent at back
    std::list<uint32_t> lru_list;
    
    // 3. Quick lookup: page_num → iterator in lru_list
    std::map<uint32_t, std::list<uint32_t>::iterator> lru_map;
    
    // 4. Dirty tracking: modified pages
    std::set<uint32_t> dirty_pages;
};
```

**Why 3 structures?**

| Structure | Purpose | Time |
|-----------|---------|------|
| `page_cache` | Store actual page data | O(log n) |
| `lru_list` | Maintain access order | O(1) move |
| `lru_map` | Find page in list quickly | O(log n) |
| `dirty_pages` | Track modifications | O(log n) |

**Operations:**

**1. Cache Hit (page in cache):**
```cpp
void* pager_get_page(Pager* pager, uint32_t page_num) {
    auto it = pager->page_cache.find(page_num);
    if (it != pager->page_cache.end()) {
        // Found! Move to back (most recent)
        pager->lru_list.erase(pager->lru_map[page_num]);
        pager->lru_list.push_back(page_num);
        pager->lru_map[page_num] = --pager->lru_list.end();
        return it->second;
    }
    // ... cache miss logic ...
}
```

**2. Cache Miss (page not in cache):**
```cpp
// Check if cache full
if (pager->page_cache.size() >= MAX_PAGES) {
    // Evict LRU page (front of list)
    uint32_t lru_page = pager->lru_list.front();
    
    // If dirty, write to disk first
    if (pager->dirty_pages.count(lru_page)) {
        pager_flush_page(pager, lru_page);
    }
    
    // Remove from all structures
    free(pager->page_cache[lru_page]);
    pager->page_cache.erase(lru_page);
    pager->lru_list.pop_front();
    pager->lru_map.erase(lru_page);
}

// Load from disk
void* page = malloc(PAGE_SIZE);
fseek(pager->file, page_offset(page_num), SEEK_SET);
fread(page, PAGE_SIZE, 1, pager->file);

// Add to cache
pager->page_cache[page_num] = page;
pager->lru_list.push_back(page_num);
pager->lru_map[page_num] = --pager->lru_list.end();
```

**Time Complexity:**
- Get page: O(log n) cache lookup + O(1) LRU update
- Mark dirty: O(log n) set insertion
- Evict: O(1) list operation + O(log n) map erase

**Space Complexity:**
- O(100) for cache (constant!)
- 100 pages × 4KB = 400 KB memory

---

### Q23: What is dirty page tracking and why is it critical?
**Answer:**

**Dirty Page = Page modified in memory but not yet written to disk**

**Problem without dirty tracking:**
```cpp
// ❌ BAD: Flush ALL pages on exit
for (auto& page : cache) {
    write_to_disk(page);  // Wastes I/O!
}
```

**Solution with dirty tracking:**
```cpp
// ✅ GOOD: Flush only modified pages
for (auto page_num : dirty_pages) {
    write_to_disk(page_num);  // Minimal I/O!
}
```

**When pages marked dirty:**
```cpp
// Every modification MUST call this:
void mark_page_dirty(Pager* pager, uint32_t page_num) {
    pager->dirty_pages.insert(page_num);
}

// Examples:
leaf_node_insert(...);
mark_page_dirty(pager, page_num);  // ✅

leaf_node_split(...);
mark_page_dirty(pager, old_page);  // ✅
mark_page_dirty(pager, new_page);  // ✅
mark_page_dirty(pager, parent_page);  // ✅
```

**Critical Bug Example (Bug #1):**
```cpp
// ❌ BEFORE FIX
void execute_update(...) {
    serialize_row(row, page);
    // Missing: mark_page_dirty(pager, page_num);
}
// Result: Update visible in session, lost after restart!

// ✅ AFTER FIX
void execute_update(...) {
    serialize_row(row, page);
    mark_page_dirty(pager, page_num);  // CRITICAL!
}
// Result: Update persists to disk
```

**Performance Impact:**

Scenario: 1000 inserts, 10 pages modified

| Approach | Pages Written | Disk I/O |
|----------|---------------|----------|
| No tracking | 100 (all) | 400 KB |
| Dirty tracking | 10 (only dirty) | 40 KB |
| **Speedup** | **10x faster** | **10x less I/O** |

**Why critical?**
1. ✅ **Performance** - Write only what changed
2. ✅ **Correctness** - Ensures persistence
3. ✅ **Efficiency** - Minimal disk I/O
4. ✅ **Battery life** - Mobile/laptop friendly

---

### Q24: Explain the page layout in memory.
**Answer:**

**Every page is exactly 4096 bytes:**

**Leaf Node Layout:**
```
Byte 0-5: Common Header
  [0]     node_type (1 byte) = 1 for leaf
  [1]     is_root (1 byte) = 0 or 1
  [2-5]   parent (4 bytes) = parent page number

Byte 6-13: Leaf-Specific Header
  [6-9]   num_cells (4 bytes) = cell count (0-13)
  [10-13] next_leaf (4 bytes) = next sibling page

Byte 14+: Cells (up to 13)
  Each cell: 295 bytes total
    [0-3]   key (4 bytes) = row ID
    [4-294] value (291 bytes) = Row data
      [0-3]   id (4 bytes)
      [4-35]  username (32 bytes, null-terminated)
      [36-290] email (255 bytes, null-terminated)

Total: 6 + 8 + (295 × 13) = 14 + 3835 = 3849 bytes used
Unused: 4096 - 3849 = 247 bytes padding
```

**Internal Node Layout:**
```
Byte 0-5: Common Header
  [0]     node_type (1 byte) = 0 for internal
  [1]     is_root (1 byte) = 0 or 1
  [2-5]   parent (4 bytes) = parent page number

Byte 6-13: Internal-Specific Header
  [6-9]   num_keys (4 bytes) = key count (0-510)
  [10-13] right_child (4 bytes) = rightmost child page

Byte 14+: Key-Child Pairs (up to 510)
  Each pair: 8 bytes
    [0-3]   child (4 bytes) = child page number
    [4-7]   key (4 bytes) = separator key

Total: 6 + 8 + (8 × 510) = 14 + 4080 = 4094 bytes used
Unused: 4096 - 4094 = 2 bytes padding
```

**Why this layout?**
- ✅ **Fixed offsets** - Fast field access
- ✅ **No pointers** - Disk-friendly
- ✅ **Packed data** - Minimal waste
- ✅ **Page-aligned** - OS optimization

---

### Q25: How do you access fields within a page?
**Answer:**

**Accessor Function Pattern:**

Instead of:
```cpp
// ❌ BAD: Direct pointer arithmetic everywhere
uint32_t* num_cells = (uint32_t*)(node + 6);
```

Use:
```cpp
// ✅ GOOD: Accessor function
uint32_t* get_leaf_node_num_cells(void* node) {
    return (uint32_t*)((char*)node + LEAF_NODE_NUM_CELLS_OFFSET);
}
```

**All Accessors (Leaf):**
```cpp
// Common header
NodeType* get_node_type(void* node) {
    return (NodeType*)((char*)node + 0);
}

uint8_t* get_is_root(void* node) {
    return (uint8_t*)((char*)node + 1);
}

uint32_t* get_node_parent(void* node) {
    return (uint32_t*)((char*)node + 2);
}

// Leaf-specific
uint32_t* get_leaf_node_num_cells(void* node) {
    return (uint32_t*)((char*)node + 6);
}

uint32_t* get_leaf_node_next_leaf(void* node) {
    return (uint32_t*)((char*)node + 10);
}

// Cell access
void* get_leaf_node_cell(void* node, uint32_t cell_num) {
    return (char*)node + LEAF_NODE_HEADER_SIZE + 
           (cell_num * LEAF_NODE_CELL_SIZE);
}

uint32_t* get_leaf_node_key(void* node, uint32_t cell_num) {
    void* cell = get_leaf_node_cell(node, cell_num);
    return (uint32_t*)cell;
}

void* get_leaf_node_value(void* node, uint32_t cell_num) {
    void* cell = get_leaf_node_cell(node, cell_num);
    return (char*)cell + LEAF_NODE_KEY_SIZE;
}
```

**Why accessors?**
1. ✅ **Type safety** - Compiler checks
2. ✅ **Readability** - Self-documenting
3. ✅ **Maintainability** - Change offsets once
4. ✅ **Debugging** - Easy to set breakpoints

**Usage example:**
```cpp
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = pager_get_page(pager, cursor->page_num);
    
    // Clear and readable!
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    *get_leaf_node_key(node, cursor->cell_num) = key;
    serialize_row(value, get_leaf_node_value(node, cursor->cell_num));
    *get_leaf_node_num_cells(node) += 1;
}
```

---

### Q26: What is the freelist and how does it work?
**Answer:**

**Freelist = Linked list of deleted pages available for reuse**

**Why needed?**
```
Without freelist:
- Insert 1000 rows → Use pages 0-100
- Delete 500 rows → Pages freed but wasted
- Insert 500 more → Use pages 101-150
- Result: File grows forever! (space leak)

With freelist:
- Insert 1000 rows → Use pages 0-100
- Delete 500 rows → Pages added to freelist
- Insert 500 more → Reuse freed pages
- Result: Efficient storage! (no growth)
```

**Structure:**
```
File Header:
  [0-3] root_page_num
  [4-7] free_head ← Points to first free page

Each free page:
  [0-3] next_free ← Points to next free page
  [4+]  unused (garbage data, ignored)

Example:
free_head = 5
Page 5: [next = 8, ...]
Page 8: [next = 12, ...]
Page 12: [next = 0, ...]  ← End of list
```

**Operations:**

**1. Free a page:**
```cpp
void free_page(Pager* pager, uint32_t page_num) {
    void* page = pager_get_page(pager, page_num);
    
    // Link into freelist
    *reinterpret_cast<uint32_t*>(page) = pager->free_head;
    pager->free_head = page_num;
    
    mark_page_dirty(pager, page_num);
}
```

**2. Allocate a page:**
```cpp
uint32_t get_unused_page_num(Pager* pager) {
    if (pager->free_head != 0) {
        // Reuse from freelist
        uint32_t page_num = pager->free_head;
        void* page = pager_get_page(pager, page_num);
        pager->free_head = *reinterpret_cast<uint32_t*>(page);
        return page_num;
    }
    
    // Allocate new page
    return pager->num_pages++;
}
```

**Benefits:**
- ✅ Space reuse (no file growth after deletes)
- ✅ O(1) allocation
- ✅ O(1) freeing
- ✅ No fragmentation tracking needed

---

### Q27: Why use `void*` pointers throughout the code?
**Answer:**

**`void*` = Generic pointer to raw memory (no type info)**

**Reasons:**

**1. Work with binary data:**
```cpp
// Page is just bytes, no specific type
void* page = malloc(4096);  // Raw bytes
```

**2. Support multiple node types:**
```cpp
void* node = pager_get_page(pager, page_num);

// Could be leaf or internal, check at runtime
if (get_node_type(node) == NODE_LEAF) {
    // Interpret as leaf
    uint32_t num_cells = *get_leaf_node_num_cells(node);
} else {
    // Interpret as internal
    uint32_t num_keys = *get_internal_node_num_keys(node);
}
```

**3. Pointer arithmetic:**
```cpp
void* get_leaf_node_cell(void* node, uint32_t cell_num) {
    // Need char* for byte-level arithmetic
    return (char*)node + offset;
}
```

**Safety concerns:**
```cpp
// ❌ DANGER: No type checking
void* node = ...;
SomeStruct* s = (SomeStruct*)node;  // Compiler can't verify!

// ✅ BETTER: Use accessor functions (centralize casts)
uint32_t* get_leaf_node_num_cells(void* node) {
    return (uint32_t*)((char*)node + OFFSET);
}
```

**Alternative approaches:**

**Option A: Unions (type-safe)**
```cpp
union Node {
    LeafNode leaf;
    InternalNode internal;
};
// Problem: Wastes space (max size of both)
```

**Option B: Templates (C++)**
```cpp
template<typename T>
T* get_page(Pager* pager, uint32_t page_num);
// Problem: Templates for binary data is overkill
```

**Option C: void* (ArborDB choice)**
```cpp
void* page = pager_get_page(...);
// Simple, efficient, works well with binary I/O
```

---

### Q28: Explain `static_cast` vs `reinterpret_cast`.
**Answer:**

**`static_cast` = Compile-time type conversion (safe)**
```cpp
// Example: Related types
int x = 42;
double y = static_cast<double>(x);  // ✅ Safe, compiler checks

// Example: Pointer upcasting
Base* b = static_cast<Base*>(derived_ptr);  // ✅ Safe
```

**`reinterpret_cast` = Raw bit reinterpretation (unsafe)**
```cpp
// Example: Treat bytes as different type
void* page = malloc(4096);
uint32_t* num = reinterpret_cast<uint32_t*>(page);  // ⚠️ Trust programmer
*num = 42;  // Write 4 bytes at start of page
```

**In ArborDB:**

```cpp
// Reading binary data from page
void* node = pager_get_page(pager, page_num);

// Need to interpret bytes as uint32_t
uint32_t* get_leaf_node_num_cells(void* node) {
    // reinterpret_cast: "Trust me, these bytes are a uint32_t"
    return reinterpret_cast<uint32_t*>(
        static_cast<char*>(node) + LEAF_NODE_NUM_CELLS_OFFSET
    );
}
```

**Why both?**
1. `static_cast<char*>(node)` - Safe conversion void* → char*
2. `reinterpret_cast<uint32_t*>(...)` - Unsafe reinterpretation char* → uint32_t*

**Safety rules:**
```cpp
// ✅ SAFE: Aligned, known offset
uint32_t* ptr = reinterpret_cast<uint32_t*>(page + 0);  // Aligned

// ⚠️ DANGEROUS: Unaligned
uint32_t* ptr = reinterpret_cast<uint32_t*>(page + 1);  // Crash on some CPUs!

// ✅ SAFE: Read with memcpy (works even unaligned)
uint32_t value;
memcpy(&value, page + 1, sizeof(uint32_t));
```

**When to use:**
- `static_cast`: Normal C++ conversions, related types
- `reinterpret_cast`: Binary data, raw memory, system programming
- `const_cast`: Removing const (rare, usually bad)
- `dynamic_cast`: Runtime type checking (polymorphic classes)

---

### Q29: How is the Row structure serialized?
**Answer:**

**Row Structure:**
```cpp
struct Row {
    uint32_t id;           // 4 bytes
    char username[32];     // 32 bytes (null-terminated)
    char email[255];       // 255 bytes (null-terminated)
};
// Total: 291 bytes
```

**Serialization (struct → bytes):**
```cpp
void serialize_row(Row* source, void* destination) {
    // Copy ID (4 bytes)
    memcpy((char*)destination + ID_OFFSET, &(source->id), ID_SIZE);
    
    // Copy username (32 bytes)
    strncpy((char*)destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
    
    // Copy email (255 bytes)
    strncpy((char*)destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}
```

**Deserialization (bytes → struct):**
```cpp
void deserialize_row(void* source, Row* destination) {
    // Read ID (4 bytes)
    memcpy(&(destination->id), (char*)source + ID_OFFSET, ID_SIZE);
    
    // Read username (32 bytes)
    strncpy(destination->username, (char*)source + USERNAME_OFFSET, USERNAME_SIZE);
    
    // Read email (255 bytes)
    strncpy(destination->email, (char*)source + EMAIL_OFFSET, EMAIL_SIZE);
}
```

**Memory Layout:**
```
Offset  Size  Field
------  ----  -----
0-3     4     id
4-35    32    username
36-290  255   email
------  ----  -----
Total:  291   bytes
```

**Why fixed-size?**
1. ✅ **Simple arithmetic** - Calculate offsets easily
2. ✅ **No pointers** - Disk-safe
3. ✅ **Predictable** - Know exact space needed
4. ✅ **Fast access** - Direct indexing

**Drawbacks:**
- ❌ Wastes space if short strings
- ❌ String truncation if too long
- ❌ Not flexible

**Production alternatives:**
- Variable-length encoding (like SQLite)
- Compression
- Separate string storage

---

### Q30: What happens when the cache is full?
**Answer:**

**Scenario: 100 pages in cache, need to load page 101**

**Step-by-step:**

**1. Check if page in cache:**
```cpp
auto it = pager->page_cache.find(101);
if (it != pager->page_cache.end()) {
    return it->second;  // Cache hit!
}
// Cache miss, need to load from disk
```

**2. Check if cache full:**
```cpp
if (pager->page_cache.size() >= MAX_PAGES) {
    // Full! Need to evict
}
```

**3. Find LRU page:**
```cpp
uint32_t lru_page = pager->lru_list.front();  // Least recently used
```

**4. Check if LRU page is dirty:**
```cpp
if (pager->dirty_pages.count(lru_page)) {
    // Modified! Must write to disk before evicting
    pager_flush_page(pager, lru_page);
}
```

**5. Evict LRU page:**
```cpp
// Free memory
free(pager->page_cache[lru_page]);

// Remove from all structures
pager->page_cache.erase(lru_page);
pager->lru_list.pop_front();
pager->lru_map.erase(lru_page);
pager->dirty_pages.erase(lru_page);
```

**6. Load new page:**
```cpp
void* page = malloc(PAGE_SIZE);
fseek(pager->file, page_offset(101), SEEK_SET);
fread(page, PAGE_SIZE, 1, pager->file);
```

**7. Add to cache:**
```cpp
pager->page_cache[101] = page;
pager->lru_list.push_back(101);
pager->lru_map[101] = --pager->lru_list.end();
```

**Visual:**
```
BEFORE:
Cache: [1, 5, 12, 23, ..., 99, 100]  ← 100 pages full
LRU List: [1 (oldest) ... 100 (newest)]

Need page 101:

STEP 1: Evict page 1 (LRU)
STEP 2: If dirty, flush page 1 to disk
STEP 3: Load page 101 from disk
STEP 4: Add page 101 to cache

AFTER:
Cache: [5, 12, 23, ..., 99, 100, 101]  ← Still 100 pages
LRU List: [5 (oldest) ... 101 (newest)]
```

**Performance:**
- Best case: Page in cache, no eviction → O(log n)
- Worst case: Cache full, dirty page evicted → O(log n) + disk I/O

---

### Q31: How does pager_flush() work?
**Answer:**

**Called on:** Database close or explicit flush command

**Purpose:** Write all dirty pages to disk

```cpp
void pager_flush(Pager* pager) {
    // Iterate through all dirty pages
    for (uint32_t page_num : pager->dirty_pages) {
        pager_flush_page(pager, page_num);
    }
    
    // Clear dirty set
    pager->dirty_pages.clear();
    
    // Flush file header (root_page_num, free_head)
    fseek(pager->file, 0, SEEK_SET);
    fwrite(&pager->root_page_num, sizeof(uint32_t), 1, pager->file);
    fwrite(&pager->free_head, sizeof(uint32_t), 1, pager->file);
    
    // Force OS to write to disk
    fflush(pager->file);
}
```

**Individual page flush:**
```cpp
void pager_flush_page(Pager* pager, uint32_t page_num) {
    void* page = pager->page_cache[page_num];
    
    // Calculate file offset
    off_t offset = (page_num + 1) * PAGE_SIZE;  // +1 for header
    
    // Seek to position
    fseek(pager->file, offset, SEEK_SET);
    
    // Write entire page (4096 bytes)
    fwrite(page, PAGE_SIZE, 1, pager->file);
    
    // Remove from dirty set
    pager->dirty_pages.erase(page_num);
}
```

**Example:**
```
Dirty pages: {5, 12, 23}

Step 1: Flush page 5 at offset 6*4096 = 24576
Step 2: Flush page 12 at offset 13*4096 = 53248
Step 3: Flush page 23 at offset 24*4096 = 98304
Step 4: Clear dirty_pages set
Step 5: Write file header
Step 6: fflush() to force OS write
```

**Why fflush()?**
```
Without fflush():
- fwrite() → OS buffer → Disk (eventually)
- If crash, data lost!

With fflush():
- fwrite() → OS buffer → fflush() → Disk (now)
- Crash-safe (mostly, still need fsync for 100% guarantee)
```

---

### Q32: What is the difference between `memcpy` and `memmove`?
**Answer:**

**`memcpy` = Fast copy, assumes no overlap**
```cpp
void* memcpy(void* dest, const void* src, size_t n);
// Copies n bytes from src to dest
// ⚠️ UNDEFINED if src and dest overlap!
```

**`memmove` = Safe copy, handles overlap**
```cpp
void* memmove(void* dest, const void* src, size_t n);
// Copies n bytes from src to dest
// ✅ SAFE even if src and dest overlap
```

**When they differ:**

```cpp
char buffer[10] = "ABCDEFGHIJ";

// Shift right by 2 (overlapping regions)
memcpy(buffer + 2, buffer, 8);  // ❌ UNDEFINED BEHAVIOR!
// Result: unpredictable (might be "ABABABABAB")

memmove(buffer + 2, buffer, 8);  // ✅ CORRECT
// Result: "ABABCDEFGH"
```

**In ArborDB (Bug #2 discovery):**

**Before fix:**
```cpp
// Shift cells right to make room for insert
for (uint32_t i = num_cells; i > index; i--) {
    void* dest = get_leaf_node_cell(node, i);
    void* src = get_leaf_node_cell(node, i - 1);
    memcpy(dest, src, CELL_SIZE);  // ❌ OVERLAP! Bug!
}
```

**After fix:**
```cpp
// Shift cells right (OVERLAPPING!)
void* dest = get_leaf_node_cell(node, index + 1);
void* src = get_leaf_node_cell(node, index);
size_t bytes = (num_cells - index) * CELL_SIZE;
memmove(dest, src, bytes);  // ✅ SAFE
```

**Performance:**
- `memcpy`: Faster (can use optimized instructions)
- `memmove`: Slightly slower (checks for overlap)

**Rule of thumb:**
- Use `memcpy` when regions definitely don't overlap
- Use `memmove` when in doubt or overlapping

---

### Q33: How do you calculate the file offset for a page?
**Answer:**

**Formula:**
```
File structure:
[Header: 8 bytes]
[Page 0: 4096 bytes]
[Page 1: 4096 bytes]
[Page 2: 4096 bytes]
...

Offset for page N:
  offset = 8 + (N × 4096)
         = 8 + (N × PAGE_SIZE)
```

**Code:**
```cpp
off_t page_offset(uint32_t page_num) {
    return FILE_HEADER_SIZE + (page_num * PAGE_SIZE);
}

// Example offsets:
page_offset(0)  = 8 + (0 × 4096) = 8
page_offset(1)  = 8 + (1 × 4096) = 4104
page_offset(2)  = 8 + (2 × 4096) = 8200
page_offset(10) = 8 + (10 × 4096) = 40968
```

**Usage:**
```cpp
void* pager_get_page(Pager* pager, uint32_t page_num) {
    // ... cache check ...
    
    // Load from disk
    void* page = malloc(PAGE_SIZE);
    off_t offset = page_offset(page_num);
    
    fseek(pager->file, offset, SEEK_SET);
    fread(page, PAGE_SIZE, 1, pager->file);
    
    return page;
}
```

**Why 8-byte header?**
```cpp
struct FileHeader {
    uint32_t root_page_num;  // 4 bytes
    uint32_t free_head;      // 4 bytes
};
// Total: 8 bytes
```

---

### Q34: What happens when you open an existing database file?
**Answer:**

**Step-by-step:**

```cpp
Table* new_table(const char* filename) {
    Pager* pager = pager_open(filename);
    Table* table = new Table();
    table->pager = pager;
    return table;
}
```

**Inside `pager_open()`:**

**1. Try to open file:**
```cpp
FILE* file = fopen(filename, "rb+");  // Read/write binary mode

if (file == nullptr) {
    // File doesn't exist → create new database
    file = fopen(filename, "wb+");
    // ... initialize new file ...
} else {
    // File exists → load existing database
}
```

**2. Read file header:**
```cpp
fseek(file, 0, SEEK_SET);
fread(&pager->root_page_num, sizeof(uint32_t), 1, file);
fread(&pager->free_head, sizeof(uint32_t), 1, file);
```

**3. Initialize pager:**
```cpp
pager->file = file;
pager->num_pages = file_size / PAGE_SIZE;
pager->page_cache.clear();
pager->lru_list.clear();
pager->lru_map.clear();
pager->dirty_pages.clear();
```

**4. Root page loaded on first access:**
```cpp
// Lazily loaded when needed
void* root = pager_get_page(pager, pager->root_page_num);
```

**For NEW file:**
```cpp
// Initialize root as empty leaf
pager->root_page_num = 0;
pager->free_head = 0;

void* root = pager_get_page(pager, 0);
initialize_leaf_node(root);
set_node_root(root, true);

// Write header to disk
pager_flush_header(pager);
```

**Verification:**
```
Existing file: Load header → verify root_page_num valid
New file: Create header → initialize root leaf → flush
```

---

### Q35: How do you ensure data persistence across restarts?
**Answer:**

**Multi-layered approach:**

**Layer 1: Dirty Page Tracking**
```cpp
// EVERY modification MUST mark page dirty
void leaf_node_insert(...) {
    // ... modify page ...
    mark_page_dirty(pager, page_num);  // ✅ CRITICAL!
}
```

**Layer 2: Flush on Exit**
```cpp
void close_table(Table* table) {
    pager_flush(table->pager);  // Write all dirty pages
    fclose(table->pager->file);
    // ... cleanup ...
}
```

**Layer 3: Flush Header**
```cpp
void pager_flush(Pager* pager) {
    // Flush all dirty pages first
    for (auto page_num : pager->dirty_pages) {
        pager_flush_page(pager, page_num);
    }
    
    // CRITICAL: Write header last (atomic commit point)
    fseek(pager->file, 0, SEEK_SET);
    fwrite(&pager->root_page_num, 4, 1, pager->file);
    fwrite(&pager->free_head, 4, 1, pager->file);
    fflush(pager->file);
}
```

**Layer 4: File System Sync (optional):**
```cpp
#ifdef _WIN32
    _commit(fileno(pager->file));
#else
    fsync(fileno(pager->file));
#endif
```

**Testing persistence:**
```python
# Session 1: Insert data
run_db("insert 1 alice alice@test.com")
run_db(".exit")

# Session 2: Verify (NEW process)
output = run_db("select")
assert "alice" in output  # ✅ Persisted!
```

**What can go wrong?**

**Bug #1: Forgot mark_page_dirty()**
```cpp
// ❌ Update visible in session, lost after restart
execute_update(...);
serialize_row(row, page);  // Modified in cache
// Missing: mark_page_dirty()!
```

**Bug #4: Forgot mark_page_dirty() in rebalance**
```cpp
// ❌ Split works, but new pages not persisted
leaf_node_split(...);
// Modified: old_node, new_node, parent
// Missing: mark all 3 dirty!
```

**Correct:**
```cpp
leaf_node_split(...);
mark_page_dirty(pager, old_page);
mark_page_dirty(pager, new_page);
mark_page_dirty(pager, parent_page);  // ✅
```

---

## 📝 Summary - Part 2 Topics Covered

✅ **Pager Architecture** - Purpose and components  
✅ **LRU Cache** - 3-structure design, O(1) operations  
✅ **Dirty Tracking** - Why critical, Bug #1  
✅ **Page Layout** - Binary format, offsets  
✅ **Accessor Pattern** - Type-safe field access  
✅ **Freelist** - Page recycling, O(1) allocation  
✅ **void* Usage** - Why and when  
✅ **Type Casting** - static_cast vs reinterpret_cast  
✅ **Serialization** - Row encoding/decoding  
✅ **Cache Eviction** - LRU algorithm details  
✅ **Flushing** - Write-back cache, persistence  
✅ **memcpy vs memmove** - Bug #2 discovery  
✅ **File Offsets** - Calculating positions  
✅ **Database Open** - New vs existing files  
✅ **Persistence** - Multi-layer guarantees  

---

**Next:** [VIVA_QUESTIONS_PART3.md](./VIVA_QUESTIONS_PART3.md) - B-Tree Operations

**See Also:**
- [CODE_WALKTHROUGH_PART1.md](./CODE_WALKTHROUGH_PART1.md) - Detailed architecture
- [06_lru_cache_implementation.md](./06_lru_cache_implementation.md) - LRU deep dive
