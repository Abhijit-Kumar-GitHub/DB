# 🎓 Topic #4: STL Containers (map, list, set)

## Why This Matters for ArborDB
Your LRU cache implementation uses three containers working together: `map` for O(1) lookups, `list` for LRU ordering, and `set` for dirty page tracking.

---

## The LRU Cache Structure

```cpp
std::map<uint32_t, void*> page_cache;              // page_num → page_data
std::list<uint32_t> lru_list;                      // MRU at front, LRU at back
std::map<uint32_t, std::list<uint32_t>::iterator> lru_map;  // page_num → position in list
std::set<uint32_t> dirty_pages;                    // Modified pages needing flush
```

---

## 1. std::map - O(log n) Ordered Key-Value Store

**Your usage:**
```cpp
// Store page in cache
pager->page_cache[page_num] = new_page;

// Check if page in cache
if (pager->page_cache.count(page_num)) {
    return pager->page_cache[page_num];
}

// Remove from cache
pager->page_cache.erase(lru_page_num);

// Get cache size
if (pager->page_cache.size() >= PAGER_CACHE_SIZE) {
    // Evict LRU page
}
```

**Key operations:**
- `map[key] = value` - Insert/update (O(log n))
- `map.count(key)` - Check existence (O(log n))
- `map.erase(key)` - Remove (O(log n))
- `map.size()` - Get count (O(1))

**Why map not unordered_map?**
- Predictable iteration order
- Slightly better worst-case performance
- Simpler for debugging

---

## 2. std::list - Doubly-Linked List

**Your usage for LRU ordering:**
```cpp
// Add to front (MRU position)
pager->lru_list.push_front(page_num);

// Remove from back (LRU position)
uint32_t lru_page = pager->lru_list.back();
pager->lru_list.pop_back();

// Remove from middle (when accessing cached page)
pager->lru_list.erase(pager->lru_map[page_num]);
```

**Key properties:**
- Doubly-linked: can traverse both directions
- O(1) insert/delete at any position (if you have iterator)
- O(n) search (but you avoid this with lru_map)

**List structure:**
```
Front (MRU)                      Back (LRU)
    ↓                                ↓
  [100] ⟷ [42] ⟷ [17] ⟷ [5]
  
After accessing page 17:
  [17] ⟷ [100] ⟷ [42] ⟷ [5]
```

---

## 3. std::set - Ordered Unique Elements

**Your usage for dirty pages:**
```cpp
// Mark page as dirty
pager->dirty_pages.insert(page_num);

// Check if dirty
if (pager->dirty_pages.count(page_num)) {
    pager_flush(pager, page_num);
}

// Clear dirty flag
pager->dirty_pages.erase(page_num);

// Flush all dirty pages
for (uint32_t page_num : pager->dirty_pages) {
    pager_flush(pager, page_num);
}
```

**Key properties:**
- Automatically sorted
- No duplicates
- O(log n) insert/delete/search

---

## The LRU Cache Algorithm

**On cache hit (page already in cache):**
```cpp
if (pager->page_cache.count(page_num)) {
    // 1. Remove from current position in LRU list
    pager->lru_list.erase(pager->lru_map[page_num]);
    
    // 2. Add to front (mark as most recently used)
    pager->lru_list.push_front(page_num);
    
    // 3. Update position map
    pager->lru_map[page_num] = pager->lru_list.begin();
    
    return pager->page_cache[page_num];
}
```

**On cache miss (need to load from disk):**
```cpp
// 1. Check if cache is full
if (pager->page_cache.size() >= PAGER_CACHE_SIZE) {
    // 2. Get LRU page (back of list)
    uint32_t lru_page_num = pager->lru_list.back();
    void* lru_page_data = pager->page_cache[lru_page_num];
    
    // 3. Flush if dirty
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
// ... read from file ...

// 6. Add to cache (MRU position)
pager->page_cache[page_num] = new_page;
pager->lru_list.push_front(page_num);
pager->lru_map[page_num] = pager->lru_list.begin();
```

---

## Why This Design is Clever

**Problem:** LRU cache needs:
1. Fast lookup: "Is page X in cache?" → O(1) desired
2. Fast eviction: "Remove least recently used" → O(1) desired
3. Fast update: "Mark page X as recently used" → O(1) desired

**Solution:**
- `page_cache` map → Fast lookup by page number
- `lru_list` → Fast access to LRU page (back of list)
- `lru_map` → Fast access to any page's position in list
- Combined: All operations are O(1) or O(log n)

**Without lru_map:**
```cpp
// Would need to search entire list - O(n)!
for (auto it = lru_list.begin(); it != lru_list.end(); ++it) {
    if (*it == page_num) {
        lru_list.erase(it);
        break;
    }
}
```

**With lru_map:**
```cpp
// Direct access - O(1)!
pager->lru_list.erase(pager->lru_map[page_num]);
```

---

## Iterators - The Bridge Between Containers

**What's an iterator?**
Think of it as a "pointer" into a container:

```cpp
std::list<uint32_t> my_list = {1, 2, 3, 4, 5};
std::list<uint32_t>::iterator it = my_list.begin();

// Dereference to get value
cout << *it;  // Prints: 1

// Move to next element
++it;
cout << *it;  // Prints: 2

// Erase at current position
my_list.erase(it);  // Removes 2
```

**Your critical pattern:**
```cpp
// Store iterator for fast access later
pager->lru_map[page_num] = pager->lru_list.begin();

// Use stored iterator to erase in O(1)
pager->lru_list.erase(pager->lru_map[page_num]);
```

---

## Range-Based For Loops

**Modern C++ way to iterate:**
```cpp
// Iterate through all dirty pages
for (uint32_t page_num : pager->dirty_pages) {
    pager_flush(pager, page_num);
}

// Iterate through cache with key-value pairs
for (auto& [page_num, page_data] : pager->page_cache) {
    delete[] static_cast<char*>(page_data);
}
```

**Equivalent to:**
```cpp
for (auto it = pager->dirty_pages.begin(); 
     it != pager->dirty_pages.end(); ++it) {
    uint32_t page_num = *it;
    pager_flush(pager, page_num);
}
```

---

## Quick Self-Test

1. What's the time complexity of `map.count(key)`?
2. Why do you need both `lru_list` and `lru_map`?
3. What's at `lru_list.back()`?
4. Why use `set` instead of `vector` for dirty pages?
5. What does `pager->lru_map[page_num]` store?

**Answers:**
1. O(log n) - map is a binary search tree
2. list for ordering, map for O(1) access to positions
3. The least recently used page number
4. Set prevents duplicates and makes lookup O(log n) vs O(n)
5. An iterator pointing to the page's position in lru_list

---

**Ready for Topic #5: Binary Search Trees (BST)?** 🚀
