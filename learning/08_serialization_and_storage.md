# 🎓 Topic #8: Serialization & Page-Based Storage

## Why This Matters for ArborDB
Converting in-memory structs to disk bytes (and back) is how your database persists data. Understanding this is critical for debugging and optimization.

---

## The Problem

**In memory:**
```cpp
Row row = {.id = 42, .username = "alice", .email = "alice@example.com"};
```

**On disk (binary):**
```
[00 00 00 2A] [61 6C 69 63 65 00 ...32 bytes...] [61 6C 69 63 65 40 ...255 bytes...]
  ↑ id=42        ↑ username="alice"                 ↑ email="alice@example.com"
```

---

## Row Serialization

### Memory Layout
```cpp
typedef struct {
    uint32_t id;                    // 4 bytes at offset 0
    char username[32 + 1];          // 33 bytes at offset 4
    char email[255 + 1];            // 256 bytes at offset 37
} Row;  // Total: 293 bytes in memory
```

### Disk Layout (Fixed 291 bytes)
```cpp
#define ID_SIZE 4
#define USERNAME_SIZE 32   // Without null terminator
#define EMAIL_SIZE 255     // Without null terminator
#define ROW_SIZE 291       // 4 + 32 + 255

#define ID_OFFSET 0
#define USERNAME_OFFSET 4
#define EMAIL_OFFSET 36
```

### Serialize (Struct → Bytes)
```cpp
void serialize_row(Row* source, void* destination) {
    // Copy ID (4 bytes)
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    
    // Copy username (32 bytes, no null terminator on disk)
    memcpy(destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
    
    // Copy email (255 bytes, no null terminator on disk)
    memcpy(destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}
```

### Deserialize (Bytes → Struct)
```cpp
void deserialize_row(void* source, Row* destination) {
    // Read ID
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    
    // Read username
    memcpy(destination->username, source + USERNAME_OFFSET, USERNAME_SIZE);
    destination->username[USERNAME_SIZE] = '\0';  // Add null terminator
    
    // Read email
    memcpy(destination->email, source + EMAIL_OFFSET, EMAIL_SIZE);
    destination->email[EMAIL_SIZE] = '\0';  // Add null terminator
}
```

**Key difference:** Disk format excludes null terminators to save space!

---

## Node Serialization

### Leaf Node Format (4096 bytes)

**Header (14 bytes):**
```
Offset 0: [node_type: 1][is_root: 1][parent: 4]  ← Common header (6 bytes)
Offset 6: [num_cells: 4][next_leaf: 4]           ← Leaf header (8 bytes)
```

**Body (up to 13 cells):**
```
Offset 14: [key: 4][value: 291]  ← Cell 0 (295 bytes)
Offset 309: [key: 4][value: 291]  ← Cell 1 (295 bytes)
...
Offset 3854: [key: 4][value: 291]  ← Cell 12 (295 bytes)
Offset 4149: [unused: 247 bytes]
```

### Internal Node Format

**Header (14 bytes):**
```
Offset 0: [node_type: 1][is_root: 1][parent: 4]    ← Common (6 bytes)
Offset 6: [num_keys: 4][right_child: 4]            ← Internal header (8 bytes)
```

**Body (up to 510 child-key pairs):**
```
Offset 14: [child0: 4][key0: 4]     ← Pair 0 (8 bytes)
Offset 22: [child1: 4][key1: 4]     ← Pair 1 (8 bytes)
...
Offset 4094: [child509: 4][key509: 4]  ← Pair 509 (8 bytes)
```

---

## Accessor Functions (Critical Pattern!)

**Instead of direct struct access, use pointer arithmetic:**

### Reading Node Header
```cpp
NodeType get_node_type(void* node) {
    uint8_t value = *static_cast<uint8_t*>(node);
    return static_cast<NodeType>(value);
}

bool is_node_root(void* node) {
    uint8_t value = *(static_cast<uint8_t*>(node) + IS_ROOT_OFFSET);
    return static_cast<bool>(value);
}

uint32_t* get_node_parent(void* node) {
    return reinterpret_cast<uint32_t*>(
        static_cast<char*>(node) + PARENT_POINTER_OFFSET
    );
}
```

### Reading Leaf Node Fields
```cpp
uint32_t* get_leaf_node_num_cells(void* node) {
    return reinterpret_cast<uint32_t*>(
        static_cast<char*>(node) + LEAF_NODE_NUM_CELLS_OFFSET
    );
}

void* get_leaf_node_cell(void* node, uint32_t cell_num) {
    return static_cast<char*>(node) + 
           LEAF_NODE_HEADER_SIZE + 
           cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* get_leaf_node_key(void* node, uint32_t cell_num) {
    void* cell = get_leaf_node_cell(node, cell_num);
    return reinterpret_cast<uint32_t*>(cell);
}

void* get_leaf_node_value(void* node, uint32_t cell_num) {
    void* cell = get_leaf_node_cell(node, cell_num);
    return static_cast<char*>(cell) + LEAF_NODE_KEY_SIZE;
}
```

### Writing to Nodes
```cpp
// Read/modify/write pattern
uint32_t* num_cells = get_leaf_node_num_cells(node);
*num_cells = 5;  // Update the value
mark_page_dirty(pager, page_num);  // Mark for flush!
```

---

## Why This Design?

### Fixed-Size Everything

**Benefits:**
1. **O(1) offset calculation** - No searching
2. **Predictable capacity** - Know exactly how many cells fit
3. **Simple memory management** - All pages same size
4. **Direct binary I/O** - Read/write entire pages

**Trade-offs:**
1. **Wasted space** - Short strings still take full space
2. **Size limits** - Username max 32, email max 255
3. **No variable-length data** - Can't store large text

### Why Separate Serialize/Deserialize?

**Could use direct struct memcpy:**
```cpp
// DON'T DO THIS!
Row* row = ...;
memcpy(destination, row, sizeof(Row));  // ❌ Includes padding, null terminators
```

**Problems:**
- Struct padding (compiler adds extra bytes)
- Null terminators waste space
- Not portable (endianness, alignment)
- No version control

**Your approach:**
- Explicit field-by-field copy
- Control exact disk format
- Can add versioning later
- Platform-independent

---

## Page Write/Read Cycle

### Complete Flow

**Writing data:**
```cpp
// 1. Get page from cache
void* node = pager_get_page(pager, page_num);

// 2. Modify node
uint32_t* num_cells = get_leaf_node_num_cells(node);
*num_cells += 1;

// 3. Mark dirty
mark_page_dirty(pager, page_num);

// 4. (Later) Flush to disk
pager_flush(pager, page_num);
```

**Reading data:**
```cpp
// 1. Get page from cache (loads from disk if needed)
void* node = pager_get_page(pager, page_num);

// 2. Read fields
uint32_t num_cells = *get_leaf_node_num_cells(node);
uint32_t key = *get_leaf_node_key(node, 0);
```

---

## memcpy vs Assignment

**When to use each:**

```cpp
// ✅ memcpy for byte blocks
memcpy(dest, src, 291);  // Copy entire row

// ✅ Assignment for simple types
uint32_t* ptr = get_node_parent(node);
*ptr = 42;  // Assign single value

// ❌ DON'T assign to arrays
char* str = get_leaf_node_value(node, 0);
str = "hello";  // ❌ Only changes pointer, doesn't copy data!

// ✅ DO use memcpy for arrays
memcpy(str, "hello", 6);  // ✅ Copies data
```

---

## Endianness (Advanced)

**Problem:** Different CPUs store multi-byte integers differently

**Little-endian (x86):**
```
uint32_t value = 0x12345678;
Memory: [78 56 34 12]  ← Least significant byte first
```

**Big-endian (some ARM):**
```
uint32_t value = 0x12345678;
Memory: [12 34 56 78]  ← Most significant byte first
```

**Your DB:**
- Assumes one endianness (not portable between architectures)
- Could add byte-swapping for portability
- Production DBs often use fixed format (e.g., always big-endian)

---

## Quick Self-Test

1. Why serialize manually instead of `memcpy(struct)`?
2. What's at byte offset 6 in a leaf node?
3. Why exclude null terminators in disk format?
4. How do you read the 3rd cell's key?
5. What happens if you forget to mark a page dirty?

**Answers:**
1. Control format, avoid padding, save space, portability
2. num_cells field (first 4 bytes of leaf-specific header)
3. Save 2 bytes per row (strings still work in memory)
4. `*get_leaf_node_key(node, 2)` or `*(node + 14 + 2*295)`
5. Changes lost when page evicted (not written to disk!)

---

**Ready for Topic #9: Tree Validation & Testing?** 🚀
