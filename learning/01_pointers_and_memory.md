# 🎓 Topic #1: Pointers & Memory Management

## Why This Matters for ArborDB
Your database stores everything as raw bytes (`void*`), uses pointer arithmetic for field access, and manages a page cache with dynamic allocation.

---

## Key Concepts for Your Project

### 1. `void*` - Generic Page Storage
Your DB returns pages as `void*` because they can be leaf nodes, internal nodes, or freed pages:

```cpp
void* pager_get_page(Pager* pager, uint32_t page_num) {
    void* new_page = new char[PAGE_SIZE];  // 4096 bytes
    return new_page;
}
```

**Must cast before using:**
```cpp
void* node = pager_get_page(pager, 0);
uint32_t* num_cells = reinterpret_cast<uint32_t*>(
    static_cast<char*>(node) + LEAF_NODE_NUM_CELLS_OFFSET
);
```

---

### 2. Pointer Arithmetic - Accessing Struct Fields in Binary Data
When you serialize data, fields are at fixed offsets. You calculate addresses using pointer arithmetic:

```cpp
// Row: id(4) + username(32) + email(255) = 291 bytes
#define ID_OFFSET 0
#define USERNAME_OFFSET 4
#define EMAIL_OFFSET 36

void serialize_row(Row* source, void* destination) {
    memcpy(destination + ID_OFFSET, &(source->id), 4);
    memcpy(destination + USERNAME_OFFSET, source->username, 32);
    memcpy(destination + EMAIL_OFFSET, source->email, 255);
}
```

**Key insight:** `ptr + n` moves `n * sizeof(type)` bytes
- `char* + 1` moves 1 byte
- `uint32_t* + 1` moves 4 bytes

---

### 3. Type Casting - Two Types You Use

**`static_cast<T>` - Safe pointer type conversion**
```cpp
void* page = pager_get_page(pager, 0);
char* bytes = static_cast<char*>(page);  // Convert to char* for arithmetic
```

**`reinterpret_cast<T>` - Treat bytes as different type**
```cpp
// Read 4 bytes as uint32_t
uint32_t* num = reinterpret_cast<uint32_t*>(bytes + offset);
*num = 42;  // Write integer
```

**Your typical pattern:**
```cpp
uint32_t* get_leaf_node_num_cells(void* node) {
    return reinterpret_cast<uint32_t*>(
        static_cast<char*>(node) + LEAF_NODE_NUM_CELLS_OFFSET
    );
}
// Step 1: void* -> char* (so we can do arithmetic)
// Step 2: char* + offset -> char* (move to the field)
// Step 3: char* -> uint32_t* (treat 4 bytes as integer)
```

---

### 4. Dynamic Memory - Page Cache Management

**Allocation:**
```cpp
void* new_page = new char[PAGE_SIZE];  // Allocate 4096 bytes
memset(new_page, 0, PAGE_SIZE);        // Zero it out
pager->page_cache[page_num] = new_page;
```

**Deallocation (during LRU eviction):**
```cpp
void* lru_page_data = pager->page_cache[lru_page_num];
delete[] static_cast<char*>(lru_page_data);  // Free memory
pager->page_cache.erase(lru_page_num);
```

**Critical:** Use `delete[]` for arrays, `delete` for single objects.

---

### 5. nullptr Checks - Defensive Programming

Your DB returns `nullptr` on errors:

```cpp
void* pager_get_page(Pager* pager, uint32_t page_num) {
    if (page_num >= TABLE_MAX_PAGES) {
        cout << "Error: Page out of bounds" << endl;
        return nullptr;  // Signal failure
    }
    // ...
}

// Caller must check
void* page = pager_get_page(pager, page_num);
if (page == nullptr) {
    return EXECUTE_PAGE_OUT_OF_BOUNDS;
}
```

---

## Common Patterns in ArborDB

### Pattern 1: Accessing Node Headers
```cpp
// All nodes have this 6-byte common header:
// [node_type:1][is_root:1][parent_ptr:4]

NodeType get_node_type(void* node) {
    uint8_t value = *static_cast<uint8_t*>(node);
    return static_cast<NodeType>(value);
}

uint32_t* get_node_parent(void* node) {
    return reinterpret_cast<uint32_t*>(
        static_cast<char*>(node) + PARENT_POINTER_OFFSET
    );
}
```

### Pattern 2: Accessing Leaf Node Cells
```cpp
// Leaf node: [header:14][cell0][cell1]...[cell12]
// Each cell: [key:4][value:291] = 295 bytes

void* get_leaf_node_cell(void* node, uint32_t cell_num) {
    return static_cast<char*>(node) + 
           LEAF_NODE_HEADER_SIZE + 
           cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* get_leaf_node_key(void* node, uint32_t cell_num) {
    void* cell = get_leaf_node_cell(node, cell_num);
    return reinterpret_cast<uint32_t*>(cell);
}
```

### Pattern 3: Memory in Cache
```cpp
// Cache structure
std::map<uint32_t, void*> page_cache;  // page_num -> raw page data

// Get from cache
if (pager->page_cache.count(page_num)) {
    return pager->page_cache[page_num];  // Return cached void*
}

// Evict and free
void* lru_page = pager->page_cache[lru_page_num];
if (pager->dirty_pages.count(lru_page_num)) {
    pager_flush(pager, lru_page_num);  // Write to disk
}
delete[] static_cast<char*>(lru_page);  // Free memory
```

---

## Critical Concepts Summary

1. **`void*` = generic pointer** - Must cast before use
2. **Pointer arithmetic** - Calculate field offsets in binary data
3. **`static_cast`** - Change pointer type (void* → char*)
4. **`reinterpret_cast`** - Treat bytes as different type (char* → uint32_t*)
5. **`new`/`delete[]`** - Heap allocation for pages
6. **`nullptr` checks** - Error handling pattern

---

## Quick Self-Test

Before moving on, make sure you understand:

1. Why does your DB use `void*` instead of a specific type?
2. What's the difference between `static_cast` and `reinterpret_cast`?
3. Why do you cast to `char*` before doing pointer arithmetic?
4. What happens if you forget `delete[]` when evicting a page?
5. How do you calculate the address of the 5th cell in a leaf node?

---

**Ready for Topic #2: Structs & Memory Layout?** 🚀
