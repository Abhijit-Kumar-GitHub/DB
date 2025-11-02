# 🎓 Topic #3: File I/O & Binary Format

## Why This Matters for ArborDB
Your database stores everything in a single binary file with a header and fixed-size pages. Understanding file I/O is critical for persistence.

---

## Key Concepts

### 1. Binary File Structure

**Your DB file layout:**
```
[Header: 8 bytes][Page 0: 4096 bytes][Page 1: 4096 bytes][Page 2: 4096 bytes]...

Header format:
  [root_page_num: 4 bytes][free_head: 4 bytes]
```

**Why 8-byte header?**
- `root_page_num` (uint32_t): Which page is the B-Tree root
- `free_head` (uint32_t): Head of freelist (0 = no free pages)

---

### 2. Opening Files with fstream

```cpp
Pager* pager_open(const string& filename) {
    Pager* pager = new Pager();
    
    // Try to open existing file
    pager->file_stream.open(filename, ios::in | ios::out | ios::binary);
    
    if (!pager->file_stream.is_open()) {
        // File doesn't exist, create new one
        pager->file_stream.open(filename, 
            ios::in | ios::out | ios::binary | ios::trunc);
        
        // Initialize new file
        pager->root_page_num = 0;
        pager->free_head = 0;
        pager->num_pages = 1;
        
        // Write header
        pager->file_stream.seekp(0);
        pager->file_stream.write(
            reinterpret_cast<char*>(&pager->root_page_num), 4);
        pager->file_stream.write(
            reinterpret_cast<char*>(&pager->free_head), 4);
        
        // Write initial root page (all zeros)
        char zero_page[PAGE_SIZE] = {0};
        pager->file_stream.write(zero_page, PAGE_SIZE);
        pager->file_stream.flush();
        
    } else {
        // File exists, read header
        pager->file_stream.seekg(0);
        pager->file_stream.read(
            reinterpret_cast<char*>(&pager->root_page_num), 4);
        pager->file_stream.read(
            reinterpret_cast<char*>(&pager->free_head), 4);
        
        // Calculate file size and page count
        pager->file_stream.seekg(0, ios::end);
        pager->file_length = pager->file_stream.tellg();
        pager->num_pages = (pager->file_length - 8) / 4096;
    }
    
    return pager;
}
```

**Key flags:**
- `ios::in` - Read mode
- `ios::out` - Write mode
- `ios::binary` - Binary mode (no text translation)
- `ios::trunc` - Truncate existing file

---

### 3. Calculating File Offsets

**Each page is at a specific byte offset:**
```cpp
uint64_t get_page_file_offset(uint32_t page_num) {
    return DB_FILE_HEADER_SIZE + (uint64_t)page_num * PAGE_SIZE;
}

// Examples:
// Page 0: 8 + (0 * 4096) = 8
// Page 1: 8 + (1 * 4096) = 4104
// Page 5: 8 + (5 * 4096) = 20488
```

---

### 4. Reading Pages from Disk

```cpp
void* pager_get_page(Pager* pager, uint32_t page_num) {
    // ... cache check first ...
    
    // Allocate new page in memory
    void* new_page = new char[PAGE_SIZE];
    memset(new_page, 0, PAGE_SIZE);
    
    uint64_t file_offset = get_page_file_offset(page_num);
    
    // Read from disk if page exists
    if (page_num < pager->num_pages) {
        pager->file_stream.seekg(file_offset);  // Position read pointer
        pager->file_stream.read(static_cast<char*>(new_page), PAGE_SIZE);
        
        if (pager->file_stream.fail()) {
            cout << "Error reading page " << page_num << endl;
            delete[] static_cast<char*>(new_page);
            return nullptr;
        }
    }
    
    // Add to cache
    pager->page_cache[page_num] = new_page;
    // ... LRU bookkeeping ...
    
    return new_page;
}
```

**Key operations:**
- `seekg()` - Position the **read** pointer (g = "get")
- `read()` - Read binary data
- `fail()` - Check for read errors

---

### 5. Writing Pages to Disk

```cpp
PagerResult pager_flush(Pager* pager, uint32_t page_num) {
    if (pager->page_cache.count(page_num) == 0) {
        return PAGER_NULL_PAGE;  // Not in cache
    }
    
    void* page_data = pager->page_cache[page_num];
    
    // Position write pointer
    pager->file_stream.seekp(get_page_file_offset(page_num));
    
    // Write entire page
    pager->file_stream.write(static_cast<char*>(page_data), PAGE_SIZE);
    
    if (pager->file_stream.fail()) {
        return PAGER_DISK_ERROR;
    }
    
    // Update file length if needed
    uint64_t expected_length = get_page_file_offset(page_num) + PAGE_SIZE;
    if (expected_length > pager->file_length) {
        pager->file_length = expected_length;
    }
    
    return PAGER_SUCCESS;
}
```

**Key operations:**
- `seekp()` - Position the **write** pointer (p = "put")
- `write()` - Write binary data
- `fail()` - Check for write errors

---

### 6. Closing and Persisting

```cpp
void db_close(Table* table) {
    Pager* pager = table->pager;
    
    // Flush all dirty pages
    for (uint32_t page_num : pager->dirty_pages) {
        if (pager->page_cache.count(page_num)) {
            pager_flush(pager, page_num);
        }
    }
    
    // Update header (root and freelist)
    pager->file_stream.seekp(0);
    pager->file_stream.write(
        reinterpret_cast<char*>(&pager->root_page_num), 4);
    pager->file_stream.write(
        reinterpret_cast<char*>(&pager->free_head), 4);
    
    // Free all cached pages
    for (auto& [page_num, page_data] : pager->page_cache) {
        delete[] static_cast<char*>(page_data);
    }
    
    pager->file_stream.close();
    delete pager;
    delete table;
}
```

---

## Critical Patterns

### Pattern 1: Dirty Page Tracking
**Only flush modified pages (90% I/O reduction!):**

```cpp
// Mark page as dirty when modified
void mark_page_dirty(Pager* pager, uint32_t page_num) {
    pager->dirty_pages.insert(page_num);
}

// During LRU eviction
if (pager->dirty_pages.count(lru_page_num)) {
    pager_flush(pager, lru_page_num);  // Write to disk
    pager->dirty_pages.erase(lru_page_num);
}
```

### Pattern 2: Error Handling
```cpp
void* page = pager_get_page(pager, page_num);
if (page == nullptr) {
    return EXECUTE_DISK_ERROR;
}
```

### Pattern 3: Persistent Metadata
**Header stores critical metadata:**
```cpp
// On every close, persist:
// - root_page_num (where the tree starts)
// - free_head (which pages are reusable)

// On open, restore:
pager->file_stream.read(&pager->root_page_num, 4);
pager->file_stream.read(&pager->free_head, 4);
```

---

## Why This Design?

### Fixed-Size Pages
**Benefits:**
- Simple offset calculation
- No fragmentation
- Predictable I/O performance
- Matches OS page size (4KB)

### Page-Based I/O
**Benefits:**
- Read/write in page units (disk-efficient)
- Cache entire pages (spatial locality)
- B-Tree naturally works with pages

### Binary Format
**Benefits:**
- Fast (no parsing)
- Compact (no text overhead)
- Direct memory mapping possible

**Trade-offs:**
- Not human-readable
- Platform-dependent (endianness)
- No built-in versioning

---

## Crash Safety

**Your DB is crash-safe because:**

1. **Atomic header updates** - Write header last during close
2. **Freelist persistence** - Free pages tracked in header
3. **Page atomicity** - OS guarantees 4KB writes are atomic on most systems
4. **No partial writes** - Always write full pages

**What survives a crash:**
- All flushed dirty pages
- Header from last successful close
- Freelist chain

**What gets lost:**
- Unflushed dirty pages (still in cache)
- In-progress operations

---

## Quick Self-Test

1. What's stored in the 8-byte file header?
2. Why do we use `ios::binary` mode?
3. What's the byte offset of page 10 in the file?
4. What's the difference between `seekg()` and `seekp()`?
5. Why do we only flush dirty pages?

**Answers:**
1. root_page_num (4 bytes) + free_head (4 bytes)
2. To prevent text translation (newline conversion, etc.)
3. 8 + (10 * 4096) = 40968
4. seekg = read position, seekp = write position
5. Performance optimization - avoid unnecessary disk I/O

---

**Ready for Topic #4: STL Containers (map, list, set)?** 🚀
