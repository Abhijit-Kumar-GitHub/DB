# 🎓 Topic #6: LRU Cache Implementation

## Why This Matters for ArborDB
Your cache manages 100 pages in memory (400KB) with O(1) eviction. This is a classic interview question and production pattern.

---

## The Problem

**Database needs:**
- Store pages in memory (cache)
- Limit cache size (can't store all pages)
- Evict pages when full
- Choose which page to evict (policy)

**LRU Policy:** Evict the **Least Recently Used** page
- Intuition: Recently used pages likely to be used again
- Locality of reference

---

## Your LRU Implementation

**Three data structures working together:**
```cpp
#define PAGER_CACHE_SIZE 100

std::map<uint32_t, void*> page_cache;              // page_num → data (O(log n) lookup)
std::list<uint32_t> lru_list;                      // MRU...→...LRU (O(1) front/back)
std::map<uint32_t, std::list<uint32_t>::iterator> lru_map;  // page_num → list position (O(log n))
```

**List ordering:**
```
Front (MRU)                           Back (LRU)
    ↓                                      ↓
  [Page 42] ⟷ [Page 17] ⟷ [Page 5] ⟷ [Page 9]
    ↑                                      ↑
  Just accessed                      Next to evict
```

---

## Cache Hit - Move to Front

**Scenario:** Accessing page 17 (already in cache)

```cpp
if (pager->page_cache.count(page_num)) {
    // 1. Remove from current position
    auto iter = pager->lru_map[page_num];
    pager->lru_list.erase(iter);
    
    // 2. Add to front (MRU)
    pager->lru_list.push_front(page_num);
    
    // 3. Update position map
    pager->lru_map[page_num] = pager->lru_list.begin();
    
    return pager->page_cache[page_num];
}
```

**Before:**
```
[42] ⟷ [17] ⟷ [5] ⟷ [9]
```

**After:**
```
[17] ⟷ [42] ⟷ [5] ⟷ [9]
```

---

## Cache Miss - Evict LRU

**Scenario:** Loading page 100 (not in cache, cache is full)

```cpp
// 1. Check if cache is full
if (pager->page_cache.size() >= PAGER_CACHE_SIZE) {
    // 2. Get LRU page (back of list)
    uint32_t lru_page_num = pager->lru_list.back();
    void* lru_page_data = pager->page_cache[lru_page_num];
    
    // 3. Flush if dirty (optimization: only write modified pages)
    if (pager->dirty_pages.count(lru_page_num)) {
        pager_flush(pager, lru_page_num);
        pager->dirty_pages.erase(lru_page_num);
    }
    
    // 4. Remove from all structures
    pager->lru_list.pop_back();
    pager->lru_map.erase(lru_page_num);
    delete[] static_cast<char*>(lru_page_data);
    pager->page_cache.erase(lru_page_num);
}

// 5. Load new page from disk
void* new_page = new char[PAGE_SIZE];
pager->file_stream.seekg(get_page_file_offset(page_num));
pager->file_stream.read(static_cast<char*>(new_page), PAGE_SIZE);

// 6. Add to cache (MRU position)
pager->page_cache[page_num] = new_page;
pager->lru_list.push_front(page_num);
pager->lru_map[page_num] = pager->lru_list.begin();

return new_page;
```

---

## Dirty Page Optimization

**Problem:** Evicting means writing to disk (slow!)

**Solution:** Track which pages are modified
```cpp
std::set<uint32_t> dirty_pages;
```

**Mark dirty on modification:**
```cpp
void mark_page_dirty(Pager* pager, uint32_t page_num) {
    pager->dirty_pages.insert(page_num);
}

// Call after any modification:
mark_page_dirty(pager, page_num);
```

**Only flush dirty pages:**
```cpp
if (pager->dirty_pages.count(lru_page_num)) {
    pager_flush(pager, lru_page_num);  // Write to disk
    pager->dirty_pages.erase(lru_page_num);
}
// Clean pages: just discard (already on disk!)
```

**Impact:** ~90% I/O reduction (most pages are read-only)

---

## Why This Design?

**Requirement: All operations must be fast**

| Operation | Required | Naive | Your Design |
|-----------|----------|-------|-------------|
| Check if cached | O(1) | O(n) search list | O(log n) map lookup |
| Get page data | O(1) | O(n) search | O(log n) map |
| Move to MRU | O(1) | O(n) find + move | O(log n) + O(1) |
| Evict LRU | O(1) | O(1) | O(1) pop_back |

**The trick:** `lru_map` stores iterators
```cpp
// Without lru_map: O(n) to find page in list
for (auto it = lru_list.begin(); it != lru_list.end(); ++it) {
    if (*it == page_num) {
        lru_list.erase(it);  // Found it!
        break;
    }
}

// With lru_map: O(1) to get iterator
auto it = lru_map[page_num];  // Direct access!
lru_list.erase(it);
```

---

## Memory Management

**Each cached page:**
- 4096 bytes of page data
- Pointers in map/list (overhead)

**Total cache memory:**
- 100 pages × 4KB = 400KB
- Plus STL overhead ≈ 50KB
- **Total ≈ 450KB** (reasonable for a database)

**Allocation:**
```cpp
void* new_page = new char[PAGE_SIZE];
```

**Deallocation:**
```cpp
delete[] static_cast<char*>(lru_page_data);
```

**Critical:** Must cast back to `char*` before delete!

---

## Complete Operation Trace

**Initial state (cache size = 3 for example):**
```
Cache: {}
LRU List: []
```

**Access page 10 (miss):**
```
Cache: {10}
LRU List: [10]
```

**Access page 20 (miss):**
```
Cache: {10, 20}
LRU List: [20, 10]
```

**Access page 30 (miss):**
```
Cache: {10, 20, 30}
LRU List: [30, 20, 10]
```

**Access page 10 (hit):**
```
Cache: {10, 20, 30}
LRU List: [10, 30, 20]  ← 10 moved to front
```

**Access page 40 (miss, cache full):**
```
Evict 20 (LRU)
Cache: {10, 30, 40}
LRU List: [40, 10, 30]
```

---

## Alternative Policies (Not Used)

**FIFO (First In First Out):**
- Evict oldest page
- Simpler but worse hit rate

**LFU (Least Frequently Used):**
- Evict page with fewest accesses
- More complex, needs counters

**Random:**
- Evict random page
- Simple but unpredictable

**Why LRU?**
- Good hit rate (80-90% for databases)
- Reasonable complexity (O(log n) or O(1))
- Standard in production systems

---

## Quick Self-Test

1. What's at the back of `lru_list`?
2. Why store iterators in `lru_map`?
3. When is a page marked dirty?
4. What happens on cache hit?
5. Why limit cache to 100 pages?

**Answers:**
1. The least recently used page (next to evict)
2. For O(1) access to page position in list
3. After any modification (insert/update/delete)
4. Move to front of LRU list (mark as MRU)
5. Memory limit + predictable performance

---

**Ready for Topic #7: B-Tree Split/Merge/Borrow?** 🚀
