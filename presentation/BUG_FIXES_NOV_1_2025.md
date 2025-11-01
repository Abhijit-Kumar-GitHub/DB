# Bug Fixes - November 1, 2025

## Summary
Fixed 4 critical bugs discovered through external code review, all related to data persistence and performance.

---

## Bug #1: Missing `mark_page_dirty()` in `execute_update()`

**Issue:** Updates were modifying records in memory but not marking pages as dirty, causing updates to be lost on database restart.

**Location:** `main.cpp` line 2283

**Fix:**
```cpp
serialize_row(row_to_update, get_leaf_node_value(loc.node, loc.cursor->cell_num));
mark_page_dirty(loc.cursor->table->pager, loc.cursor->page_num); // BUG FIX: Mark page dirty after update
delete loc.cursor;
```

**Impact:** Updates now persist correctly to disk.

**Test:** `test_bug_fixes.py::test_bug_1_update_persistence()` - PASSED ✅

---

## Bug #2: Missing `mark_page_dirty()` in `leaf_node_delete()`

**Issue:** Deletes that didn't trigger underflow (no merge needed) were not marking pages as dirty, causing deletes to be lost on restart.

**Location:** `main.cpp` line 1402

**Fix:**
```cpp
*get_leaf_node_num_cells(node) -= 1;
mark_page_dirty(cursor->table->pager, cursor->page_num); // BUG FIX: Mark page dirty after delete

// Check for underflow (only if not root)
if (!is_node_root(node) && *get_leaf_node_num_cells(node) < LEAF_NODE_MIN_CELLS) {
    handle_leaf_underflow(cursor->table, cursor->page_num);
}
```

**Impact:** All deletes now persist correctly to disk, not just those that trigger merges.

**Test:** `test_bug_fixes.py::test_bug_2_delete_persistence()` - PASSED ✅

---

## Bug #3: O(n²) Bubble Sort in Merge Operations

**Issue:** When merging leaf nodes during underflow handling, the code used O(n²) bubble sort to sort the merged keys. This was inefficient and unnecessary.

**Locations:** 
- `main.cpp` lines 1593-1622 (merge with left sibling)
- `main.cpp` lines 1697-1726 (merge with right sibling)

**Root Cause:** Borrowing operations could leave nodes unsorted (borrow from right appends to end, borrow from left prepends to beginning).

**Fix:** Replaced bubble sort with O(n log n) sort using `std::sort`:
```cpp
// BUG FIX #3: Replace O(n²) bubble sort with O(n log n) sort
uint32_t total_cells = left_cells + current_cells;
if (total_cells > 1) {
    // Create vector of (key, cell_index) pairs
    std::vector<std::pair<uint32_t, uint32_t>> key_indices;
    key_indices.reserve(total_cells);
    for (uint32_t i = 0; i < total_cells; i++) {
        key_indices.push_back({*get_leaf_node_key(node, i), i});
    }
    
    // Sort by key (O(n log n))
    std::sort(key_indices.begin(), key_indices.end());
    
    // Create temporary buffer and rearrange cells
    char* temp_buffer = new char[total_cells * LEAF_NODE_CELL_SIZE];
    for (uint32_t i = 0; i < total_cells; i++) {
        uint32_t old_index = key_indices[i].second;
        memcpy(temp_buffer + i * LEAF_NODE_CELL_SIZE, 
               get_leaf_node_cell(node, old_index), LEAF_NODE_CELL_SIZE);
    }
    
    // Copy sorted cells back to node
    for (uint32_t i = 0; i < total_cells; i++) {
        memcpy(get_leaf_node_cell(node, i), 
               temp_buffer + i * LEAF_NODE_CELL_SIZE, LEAF_NODE_CELL_SIZE);
    }
    
    delete[] temp_buffer;
}
```

**Impact:** Merge operations now run in O(n log n) instead of O(n²), improving performance for large merges.

**Test:** `test_bug_fixes.py::test_bug_3_merge_performance()` - PASSED ✅

---

## Bug #4: Missing `mark_page_dirty()` in All Rebalancing Operations

**Issue:** None of the B-Tree rebalancing operations (split, merge, borrow) were marking modified pages as dirty. This meant that whenever a tree operation caused nodes to split, merge, or redistribute keys, those changes were only made in memory and never persisted to disk, leading to catastrophic database corruption on restart.

**Affected Functions:**
- `internal_node_insert()` - Adds new child/key to parent (line 1120)
- `internal_node_split_and_insert()` - Splits internal nodes (line 1187)
- `handle_leaf_underflow()` - Borrow from right sibling (line 1510)
- `handle_leaf_underflow()` - Borrow from left sibling (line 1563)
- `handle_leaf_underflow()` - Merge with left sibling (line 1667)
- `handle_leaf_underflow()` - Merge with right sibling (line 1768)
- `handle_internal_underflow()` - Borrow from right sibling (line 1899)
- `handle_internal_underflow()` - Borrow from left sibling (line 1949)
- `handle_internal_underflow()` - Merge with left sibling (line 2002)
- `handle_internal_underflow()` - Merge with right sibling (line 2076)

**Root Cause:** The original bug fixes (#1, #2) only added `mark_page_dirty()` to the main CRUD operations (insert, update, delete). The reviewer then discovered that ALL rebalancing operations also needed these calls, as they modify pages but don't mark them dirty.

**Example Scenario (Catastrophic):**
1. Insert 15 records → triggers `leaf_node_split_and_insert`
2. Creates new root (page 1) and new sibling (page 2)
3. Code forgets to mark pages 0, 1, 2 as dirty
4. User runs `.exit` → only dirty pages are flushed
5. Database file on disk is now corrupt (thinks page 0 is full leaf, pages 1-2 don't exist)

**Fix:** Added `mark_page_dirty()` calls to all rebalancing operations:

```cpp
// Example: internal_node_insert
void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
    // ... existing code ...
    *get_internal_node_num_keys(parent) += 1;
    
    // BUG FIX #4: Mark parent page as dirty after modifying it
    mark_page_dirty(table->pager, parent_page_num);
}

// Example: handle_leaf_underflow (borrow from right)
if (right_sibling_cells > LEAF_NODE_MIN_CELLS) {
    // ... borrow logic ...
    
    // BUG FIX #4: Mark all modified pages as dirty (node, sibling, parent)
    mark_page_dirty(table->pager, page_num);
    mark_page_dirty(table->pager, right_sibling_page_num);
    mark_page_dirty(table->pager, parent_page_num);
    
    return;
}
```

**Impact:** All B-Tree rebalancing operations now persist correctly to disk. The database can now handle splits, merges, and borrows without data loss on restart.

**Test:** `test_rebalance_persistence.py` - 5 comprehensive tests:
- Leaf split persistence ✅
- Internal split persistence ✅
- Borrow operation persistence ✅
- Merge operation persistence ✅
- Complex rebalancing scenario ✅

---

## Additional Changes

1. **Added `#include <algorithm>` to `db.hpp`** (line 30) for `std::sort` support

2. **Created comprehensive bug validation test suite** (`test_bug_fixes.py`) with 4 tests:
   - Bug #1: Update persistence
   - Bug #2: Delete persistence  
   - Bug #3: Merge sort correctness
   - Combined scenario (all bugs)

3. **Created rebalancing persistence test suite** (`test_rebalance_persistence.py`) with 5 tests:
   - Leaf split persistence
   - Internal split persistence
   - Borrow operation persistence
   - Merge operation persistence
   - Complex rebalancing scenario

---

## Test Results

**Before Fixes:**
- Updates lost on restart ❌
- Deletes lost on restart ❌
- Keys unsorted after merge (potential correctness issues) ❌
- Tree structure lost on restart (splits, merges, borrows not persisted) ❌

**After Fixes:**
- All 31 core tests: PASSED ✅
- All 4 bug validation tests: PASSED ✅
- All 5 rebalancing tests: PASSED ✅
- **Total: 40/40 tests passing (100%)**

---

## Code Review Notes

These bugs were discovered through external code review on November 1, 2025. The reviewer noted:

1. "Missing mark_page_dirty in execute_update - updates won't persist"
2. "leaf_node_delete only marks dirty if underflow happens - regular deletes lost"
3. "Merge operations use bubble sort (O(n²)) - should be O(n) merge since both sides sorted"
4. **"CRITICAL: None of your B-Tree rebalancing functions call mark_page_dirty() - this will lead to catastrophic database corruption"**

All bugs have been fixed and validated with comprehensive tests.

---

## Files Modified

- `main.cpp` - 4 critical bug fixes:
  - Bug #1: Line 2283 (execute_update)
  - Bug #2: Line 1402 (leaf_node_delete)
  - Bug #3: Lines 1593-1622, 1697-1726 (merge sort optimization)
  - Bug #4: Lines 1120, 1187, 1293, 1306, 1510, 1563, 1667, 1768, 1899, 1949, 2002, 2076 (all rebalancing ops)
- `db.hpp` - Added `#include <algorithm>` (line 30)
- `test_bug_fixes.py` - Bug validation test suite (4 tests)
- `test_rebalance_persistence.py` - Rebalancing persistence test suite (5 tests)
- `README.md` - Updated test counts and features
- `presentation/QUICK_REFERENCE.md` - Updated stats
- `presentation/PRESENTATION_GUIDE.md` - Updated test counts
- `presentation/SPEAKER_NOTES.txt` - Added bug fix talking points
- `presentation/BUG_FIXES_NOV_1_2025.md` - This document

---

**Date:** November 1, 2025  
**Status:** All bugs fixed and validated ✅  
**Test Coverage:** 40/40 tests passing (31 core + 4 bug validation + 5 rebalancing)  
**Next Steps:** Ready for presentation on November 2, 2025
