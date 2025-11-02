# 🎓 Topic #2: Structs & Memory Layout

## Why This Matters for ArborDB
Your DB serializes structs to disk as raw bytes. Understanding memory layout is critical for calculating offsets and reading/writing binary data.

---

## Key Concepts

### 1. Struct Memory Layout
Structs are stored as contiguous bytes in memory with potential padding:

```cpp
struct Row {
    uint32_t id;                // 4 bytes (offset 0)
    char username[32 + 1];      // 33 bytes (offset 4)
    char email[255 + 1];        // 256 bytes (offset 37)
};  // Total: 293 bytes (no padding in this case)
```

**Your DB constants:**
```cpp
#define ID_SIZE sizeof(uint32_t)        // 4
#define USERNAME_SIZE 32                // 32
#define EMAIL_SIZE 255                  // 255
#define ROW_SIZE (4 + 32 + 255)        // 291 (without null terminators)

#define ID_OFFSET 0
#define USERNAME_OFFSET (ID_OFFSET + ID_SIZE)    // 4
#define EMAIL_OFFSET (USERNAME_OFFSET + USERNAME_SIZE)  // 36
```

---

### 2. Binary Serialization
Converting struct to bytes for disk storage:

```cpp
void serialize_row(Row* source, void* destination) {
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}

void deserialize_row(void* source, Row* destination) {
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(destination->username, source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(destination->email, source + EMAIL_OFFSET, EMAIL_SIZE);
}
```

**Why not just `memcpy(&struct, ...)`?**
- Control over what gets serialized
- Portability (endianness, padding)
- Version compatibility

---

### 3. Node Header Layouts

**Common Node Header (6 bytes):**
```cpp
#define NODE_TYPE_SIZE sizeof(uint8_t)           // 1 byte
#define NODE_TYPE_OFFSET 0

#define IS_ROOT_SIZE sizeof(uint8_t)             // 1 byte
#define IS_ROOT_OFFSET NODE_TYPE_SIZE            // 1

#define PARENT_POINTER_SIZE sizeof(uint32_t)     // 4 bytes
#define PARENT_POINTER_OFFSET (IS_ROOT_OFFSET + IS_ROOT_SIZE)  // 2

#define COMMON_NODE_HEADER_SIZE 6
```

**Leaf Node Header (14 bytes total):**
```cpp
// Common header (6) + Leaf-specific (8)
#define LEAF_NODE_NUM_CELLS_SIZE sizeof(uint32_t)  // 4 bytes
#define LEAF_NODE_NUM_CELLS_OFFSET 6

#define LEAF_NODE_NEXT_LEAF_SIZE sizeof(uint32_t)  // 4 bytes
#define LEAF_NODE_NEXT_LEAF_OFFSET 10

#define LEAF_NODE_HEADER_SIZE 14
```

**Memory visualization:**
```
Byte:  [0][1][2-5][6-9][10-13][14...]
Field: [T][R][Par][Num][Next ][Cells...]
       └─┘ └─┘ └──┘ └──┘ └───┘
        |   |   |    |     └─ Next leaf pointer
        |   |   |    └─ Cell count
        |   |   └─ Parent page number
        |   └─ Is root flag
        └─ Node type
```

---

### 4. Cell Layout in Leaf Nodes

**Leaf cell = Key + Value:**
```cpp
#define LEAF_NODE_KEY_SIZE sizeof(uint32_t)      // 4 bytes
#define LEAF_NODE_VALUE_SIZE ROW_SIZE            // 291 bytes
#define LEAF_NODE_CELL_SIZE 295                  // 4 + 291

// Maximum cells that fit in a page
#define LEAF_NODE_SPACE_FOR_CELLS (PAGE_SIZE - LEAF_NODE_HEADER_SIZE)  // 4082
#define LEAF_NODE_MAX_CELLS (LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE)  // 13
```

**Accessing cells:**
```cpp
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

---

### 5. Internal Node Layout

**Internal nodes store child pointers + separator keys:**
```cpp
#define INTERNAL_NODE_HEADER_SIZE 14  // Same as leaf (common + specific)
#define INTERNAL_NODE_CHILD_SIZE sizeof(uint32_t)  // 4 bytes
#define INTERNAL_NODE_KEY_SIZE sizeof(uint32_t)    // 4 bytes
#define INTERNAL_NODE_CELL_SIZE 8                  // child + key pair

#define INTERNAL_NODE_MAX_KEYS 510  // (4096 - 14) / 8
```

**Memory layout:**
```
[Header:14][Child0:4][Key0:4][Child1:4][Key1:4]...[RightChild:4]
```

---

### 6. Page Structure

**A page (4096 bytes) contains:**
```cpp
#define PAGE_SIZE 4096  // Standard OS page size

// For leaf node:
// [Header: 14 bytes][Cell0: 295][Cell1: 295]...[Cell12: 295]
// Remaining unused space: 4096 - 14 - (13 * 295) = 247 bytes

// For internal node:
// [Header: 14 bytes][Child0+Key0: 8][Child1+Key1: 8]...[Child510+Key510: 8]
```

---

## Critical Patterns in Your Code

### Pattern 1: Fixed-Size Offsets
```cpp
// You pre-calculate ALL offsets as constants
// This makes field access O(1) with pointer arithmetic
#define LEAF_NODE_NUM_CELLS_OFFSET 6
uint32_t* num_cells = reinterpret_cast<uint32_t*>(
    static_cast<char*>(node) + 6
);
```

### Pattern 2: Array Access in Nodes
```cpp
// Cells are stored as a contiguous array
// Cell N starts at: HEADER_SIZE + N * CELL_SIZE
void* cell_3 = static_cast<char*>(node) + 14 + (3 * 295);
```

### Pattern 3: memcpy for Binary I/O
```cpp
// Writing to file
pager->file_stream.write(static_cast<char*>(page), PAGE_SIZE);

// Reading from file
pager->file_stream.read(static_cast<char*>(page), PAGE_SIZE);
```

---

## Why Fixed-Size Matters

**Your DB uses fixed-size everything:**
- Fixed Row size (291 bytes)
- Fixed Page size (4096 bytes)
- Fixed Cell sizes (295 for leaf, 8 for internal)

**Benefits:**
1. **Fast offset calculation** - No searching, just arithmetic
2. **Predictable capacity** - Know exactly how many cells fit
3. **Simple memory management** - All pages same size
4. **Easy serialization** - Direct binary read/write

**Trade-off:**
- Wasted space (short usernames still take 32 bytes)
- Maximum lengths enforced (email can't exceed 255 chars)

---

## Quick Self-Test

1. What's the offset of the `email` field in a serialized Row?
2. How many leaf cells fit in one page and why?
3. What's at byte offset 6 in a leaf node?
4. Why do you need to cast to `char*` before adding offsets?
5. How do you calculate the address of the 7th cell in a leaf node?

**Answers:**
1. 36 (4 + 32)
2. 13 cells: (4096 - 14) / 295 = 13.83... → floor to 13
3. The num_cells field (first byte after common header)
4. Because `char` is 1 byte, so `char* + n` moves exactly n bytes
5. `node + 14 + (7 * 295)`

---

**Ready for Topic #3: File I/O & Binary Format?** 🚀
