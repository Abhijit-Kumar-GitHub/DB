# 🎓 ArborDB Viva/Interview Questions - Part 6: Bug Fixes & Lessons

## Overview
This part covers the 4 major bugs discovered, their root causes, fixes, and lessons learned.

---

## 🐛 Section 6: Bug Fixes & Debugging (10 Questions)

### Q80: What were the 4 major bugs found in ArborDB?
**Answer:**

**Summary table:**

| Bug # | Operation | Symptom | Root Cause | Fix |
|-------|-----------|---------|------------|-----|
| **#1** | UPDATE | Updates lost after restart | Missing `mark_page_dirty()` | Add dirty marking |
| **#2** | DELETE | Memory corruption | `memcpy()` with overlapping memory | Change to `memmove()` |
| **#3** | SELECT | Slow O(n²) sort | Bubble sort in multi-leaf traversal | Use `std::sort()` |
| **#4** | REBALANCE | Split/merge lost after restart | Missing dirty marks in rebalancing | Mark all modified pages |

**Discovery timeline:**
1. Bug #1 & #2: Found during basic persistence testing
2. Bug #3: Found during performance profiling
3. Bug #4: Found during rebalance persistence testing

**Impact severity:**
- Bug #1: **Critical** - Data loss
- Bug #2: **Critical** - Memory corruption / crashes
- Bug #3: **Medium** - Performance degradation
- Bug #4: **Critical** - Tree corruption

---

### Q81: Explain Bug #1 (Update persistence) in detail.
**Answer:**

**Bug:** UPDATE operation doesn't persist to disk

**Symptom:**
```python
db = Database("test.db")
db.insert(1, "Alice", "alice@test.com")
db.update(1, "Bob", "bob@test.com")
db.close()

db = Database("test.db")
row = db.find(1)
# Expected: (1, "Bob", "bob@test.com")
# Actual:   (1, "Alice", "alice@test.com")  ← Update lost!
```

**Root cause:**

**Before fix:**
```cpp
ExecuteResult execute_update(Statement* statement, Table* table) {
    // Find key
    Cursor* cursor = table_find(table, key);
    void* node = pager_get_page(table->pager, cursor->page_num);
    
    // ... check if key exists ...
    
    // Update row in memory
    serialize_row(row_to_update, get_leaf_node_value(node, cursor->cell_num));
    
    // ❌ FORGOT TO MARK PAGE DIRTY!
    // Update visible in cache, but never written to disk!
    
    delete cursor;
    return EXECUTE_SUCCESS;
}
```

**Why it fails:**
```
1. Update modifies page in LRU cache
2. mark_page_dirty() NOT called
3. Page stays clean (dirty=false)
4. On flush, clean pages skipped
5. Update never reaches disk
6. After restart, old data loaded from disk
```

**Fix:**
```cpp
ExecuteResult execute_update(Statement* statement, Table* table) {
    // ... find and update ...
    
    serialize_row(row_to_update, get_leaf_node_value(node, cursor->cell_num));
    
    // ✅ MARK PAGE DIRTY!
    mark_page_dirty(table->pager, cursor->page_num);
    
    delete cursor;
    return EXECUTE_SUCCESS;
}
```

**Lesson learned:**
> **Every memory modification MUST be paired with mark_page_dirty()**

**Similar bugs prevented:**
- After fix #1, audited ALL modification operations
- Found and fixed same issue in several places
- Added validation: "If serialize_row() called, must mark dirty"

---

### Q82: Explain Bug #2 (Delete memory corruption) in detail.
**Answer:**

**Bug:** DELETE corrupts memory, causes crashes

**Symptom:**
```python
db = Database("test.db")
for i in range(20):
    db.insert(i, f"user{i}", f"email{i}")

# Delete middle element
db.delete(10)

# Sometimes crashes here!
db.validate_tree()  # Segmentation fault
```

**Root cause: memcpy() with overlapping memory**

**Before fix:**
```cpp
void leaf_node_delete(Cursor* cursor) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    
    // Shift cells left to fill gap
    for (uint32_t i = cursor->cell_num; i < num_cells - 1; i++) {
        void* dest = get_leaf_node_cell(node, i);
        void* src = get_leaf_node_cell(node, i + 1);
        
        // ❌ WRONG! memcpy with overlapping regions!
        memcpy(dest, src, LEAF_NODE_CELL_SIZE);
    }
    
    *get_leaf_node_num_cells(node) -= 1;
    mark_page_dirty(cursor->table->pager, cursor->page_num);
}
```

**Why memcpy() fails:**

**Memory layout:**
```
Cells: [A][B][C][D][E]
        0  1  2  3  4

Delete cell 1 (B):
  i=1: copy [C] from offset 2 to offset 1
       dest = [1]
       src  = [2]
       ✓ No overlap
  
  i=2: copy [D] from offset 3 to offset 2
       dest = [2]
       src  = [3]
       ✓ No overlap
  
But if we shift all at once:
  dest = [1,2,3,4]
  src  = [2,3,4,5]
       ↑↑↑ OVERLAP! memcpy undefined behavior!
```

**Better approach (bulk shift):**
```cpp
// ❌ This is what happened:
void* dest = get_leaf_node_cell(node, cursor->cell_num);
void* src = get_leaf_node_cell(node, cursor->cell_num + 1);
uint32_t bytes = (num_cells - cursor->cell_num - 1) * LEAF_NODE_CELL_SIZE;
memcpy(dest, src, bytes);  // OVERLAP! Corrupts data!
```

**Overlap visual:**
```
Array: [A, B, C, D, E]
       [0, 1, 2, 3, 4]

Delete B (index 1), shift left:
  src:  [C, D, E] at indices [2, 3, 4]
  dest: [B, C, D] at indices [1, 2, 3]
                       ↑  ↑
                       Same memory!

memcpy: Might copy [C, C, C] ❌
        Or [C, D, C] ❌
        Undefined behavior!

memmove: Correctly copies [C, D, E] ✅
```

**Fix:**
```cpp
void leaf_node_delete(Cursor* cursor) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    
    // Shift cells left
    void* dest = get_leaf_node_cell(node, cursor->cell_num);
    void* src = get_leaf_node_cell(node, cursor->cell_num + 1);
    uint32_t bytes = (num_cells - cursor->cell_num - 1) * LEAF_NODE_CELL_SIZE;
    
    // ✅ Use memmove for overlapping memory!
    memmove(dest, src, bytes);
    
    *get_leaf_node_num_cells(node) -= 1;
    mark_page_dirty(cursor->table->pager, cursor->page_num);
    
    // Handle underflow...
}
```

**Lesson learned:**
> **Use memmove() for ANY potential overlap, memcpy() only when certain no overlap**

**Rule of thumb:**
```cpp
// Same buffer? Use memmove!
if (dest_buffer == src_buffer) {
    memmove(dest, src, size);
}

// Different buffers? memcpy is faster
else {
    memcpy(dest, src, size);
}
```

---

### Q83: Explain Bug #3 (SELECT O(n²) complexity) in detail.
**Answer:**

**Bug:** SELECT with multiple leaves is very slow

**Symptom:**
```python
db = Database("test.db")

# Insert 10,000 records
import time
start = time.time()
for i in range(10000):
    db.insert(i, f"user{i}", f"email{i}")
elapsed_insert = time.time() - start
print(f"Insert: {elapsed_insert:.2f}s")  # ~1.5s

# SELECT all
start = time.time()
rows = db.select_all()
elapsed_select = time.time() - start
print(f"Select: {elapsed_select:.2f}s")  # ~45s (!!)

# Expected: O(n), Actual: O(n²)!
```

**Root cause: Bubble sort per leaf**

**Before fix:**
```cpp
ExecuteResult execute_select(Statement* statement, Table* table) {
    Cursor* cursor = table_start(table);
    std::vector<Row> rows;
    
    // Collect all rows
    while (!cursor->end_of_table) {
        Row row;
        deserialize_row(cursor_value(cursor), &row);
        rows.push_back(row);
        cursor_advance(cursor);
    }
    
    // ❌ BUBBLE SORT! O(n²)
    for (size_t i = 0; i < rows.size(); i++) {
        for (size_t j = i + 1; j < rows.size(); j++) {
            if (rows[i].id > rows[j].id) {
                std::swap(rows[i], rows[j]);
            }
        }
    }
    
    // Print sorted rows
    for (const auto& row : rows) {
        print_row(&row);
    }
    
    delete cursor;
    return EXECUTE_SUCCESS;
}
```

**Why bubble sort was there:**
```
Original assumption: Keys might be unsorted in leaves
Reality: Keys are ALWAYS sorted within each leaf!
Problem: Multiple leaves might be in wrong order globally

Example:
  Leaf1: [10, 20, 30]    ← Sorted
  Leaf2: [5, 25, 35]     ← Sorted
  Leaf3: [15, 40, 50]    ← Sorted
  
  Sequential read: [10, 20, 30, 5, 25, 35, 15, 40, 50]
  Globally: NOT sorted!
```

**Why O(n²) is terrible:**

| Records | O(n log n) ops | O(n²) ops | Ratio |
|---------|----------------|-----------|-------|
| 100 | 664 | 10,000 | 15× |
| 1,000 | 9,966 | 1,000,000 | 100× |
| 10,000 | 132,877 | 100,000,000 | 752× |
| 100,000 | 1,660,964 | 10,000,000,000 | 6,020× |

**Fix:**
```cpp
ExecuteResult execute_select(Statement* statement, Table* table) {
    Cursor* cursor = table_start(table);
    std::vector<Row> rows;
    
    // Collect all rows (already mostly sorted!)
    while (!cursor->end_of_table) {
        Row row;
        deserialize_row(cursor_value(cursor), &row);
        rows.push_back(row);
        cursor_advance(cursor);
    }
    
    // ✅ Use std::sort - O(n log n)
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.id < b.id;
    });
    
    // Print sorted rows
    for (const auto& row : rows) {
        print_row(&row);
    }
    
    delete cursor;
    return EXECUTE_SUCCESS;
}
```

**Performance improvement:**
```
Before: 45 seconds for 10,000 records
After:  0.15 seconds for 10,000 records
Speedup: 300×!
```

**Even better fix (exploit sorted leaves):**
```cpp
// Since leaves are sorted, use k-way merge!
ExecuteResult execute_select_optimized(Statement* statement, Table* table) {
    // Collect all leaf pages
    std::vector<LeafCursor> leaf_cursors;
    // ... initialize cursors for each leaf ...
    
    // K-way merge (like mergesort)
    while (any_cursor_active) {
        // Find cursor with minimum key
        uint32_t min_key = UINT32_MAX;
        LeafCursor* min_cursor = nullptr;
        
        for (auto& cursor : leaf_cursors) {
            if (cursor.active && cursor.current_key < min_key) {
                min_key = cursor.current_key;
                min_cursor = &cursor;
            }
        }
        
        // Print minimum, advance that cursor
        print_row(min_cursor->current_row);
        min_cursor->advance();
    }
    
    // No sort needed! O(k × n) = O(n) where k = leaf count
}
```

**Lesson learned:**
> **Profile before optimizing, but also: exploit data structures' inherent properties**

---

### Q84: Explain Bug #4 (Rebalance persistence) in detail.
**Answer:**

**Bug:** Tree rebalancing (split/merge) doesn't persist

**Symptom:**
```python
db = Database("test.db")

# Trigger split
for i in range(20):
    db.insert(i, f"user{i}", f"email{i}")

# Tree should have height 2
assert db.tree_height() == 2  # ✓ OK in memory

db.close()

# Reopen
db = Database("test.db")
assert db.tree_height() == 2  # ✗ FAIL! Height is 1

# Tree corrupted!
assert db.validate_tree() == True  # ✗ FAIL!
```

**Root cause: Missing mark_page_dirty() in rebalancing operations**

**Before fix (leaf_node_split_and_insert):**
```cpp
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* old_node = pager_get_page(cursor->table->pager, cursor->page_num);
    uint32_t new_page_num = pager_get_unused_page_num(cursor->table->pager);
    void* new_node = pager_get_page(cursor->table->pager, new_page_num);
    
    // ... split cells between old_node and new_node ...
    
    *get_leaf_node_num_cells(old_node) = LEFT_SPLIT_COUNT;
    *get_leaf_node_num_cells(new_node) = RIGHT_SPLIT_COUNT;
    
    // ❌ FORGOT TO MARK DIRTY!
    // Both nodes modified but not written to disk!
    
    // Update parent
    if (is_node_root(old_node)) {
        create_new_root(cursor->table, new_page_num);
    } else {
        internal_node_insert(cursor->table, parent_page, new_page_num);
    }
}
```

**Impact cascade:**
```
1. Split modifies old_node, new_node, parent
2. None marked dirty
3. Changes stay in cache only
4. On close: flush only dirty pages (none!)
5. On reopen: old data loaded
6. Tree structure inconsistent
7. validate_tree() fails
```

**What should be marked dirty:**

**In leaf_node_split_and_insert:**
- ✅ old_node (cell count changed, cells moved)
- ✅ new_node (newly created, cells added)
- ✅ parent (new child added, keys updated)

**In internal_node_split_and_insert:**
- ✅ old_node (keys moved)
- ✅ new_node (newly created, keys added)
- ✅ ALL children of new_node (parent pointer updated)
- ✅ parent (new child added)

**In merge_leaf_nodes:**
- ✅ left_node (cells added from right)
- ✅ right_node (will be freed)
- ✅ parent (child removed, keys updated)

**Fix (complete):**
```cpp
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* old_node = pager_get_page(cursor->table->pager, cursor->page_num);
    uint32_t new_page_num = pager_get_unused_page_num(cursor->table->pager);
    void* new_node = pager_get_page(cursor->table->pager, new_page_num);
    
    // ... split cells ...
    
    *get_leaf_node_num_cells(old_node) = LEFT_SPLIT_COUNT;
    *get_leaf_node_num_cells(new_node) = RIGHT_SPLIT_COUNT;
    
    // ✅ Mark both nodes dirty!
    mark_page_dirty(cursor->table->pager, cursor->page_num);
    mark_page_dirty(cursor->table->pager, new_page_num);
    
    // Update parent
    if (is_node_root(old_node)) {
        create_new_root(cursor->table, new_page_num);
        // create_new_root marks root dirty internally
    } else {
        uint32_t parent_page = *get_node_parent(old_node);
        internal_node_insert(cursor->table, parent_page, new_page_num);
        // internal_node_insert marks parent dirty
    }
}

void internal_node_split_and_insert(Table* table, uint32_t page_num, 
                                   uint32_t child_page_num) {
    // ... split ...
    
    // ✅ Mark split nodes dirty
    mark_page_dirty(table->pager, page_num);
    mark_page_dirty(table->pager, new_page_num);
    
    // ✅ Mark ALL moved children dirty (parent pointer changed!)
    for (uint32_t i = 0; i <= *get_internal_node_num_keys(new_node); i++) {
        uint32_t child = (i == *get_internal_node_num_keys(new_node)) ?
            *get_internal_node_right_child(new_node) :
            *get_internal_node_child(new_node, i);
        
        mark_page_dirty(table->pager, child);
    }
    
    // Update grandparent...
}
```

**Checklist added after Bug #4:**
```cpp
// Dirty marking checklist:
// [ ] Node whose content changed (cells/keys modified)
// [ ] Node whose header changed (cell_count, num_keys)
// [ ] Node whose pointers changed (parent, next_leaf, children)
// [ ] Parent if child was added/removed
// [ ] Children if their parent pointer changed
// [ ] Any node involved in split/merge/borrow
```

**Lesson learned:**
> **Complex operations touching multiple pages need careful dirty tracking audit**

---

### Q85: How were these bugs discovered?
**Answer:**

**Discovery methods:**

**Bug #1 (Update persistence):**
```python
# Discovered by: Basic persistence test
def test_update_persistence():
    db = Database("test.db")
    db.insert(1, "Alice", "alice@test.com")
    db.update(1, "Bob", "bob@test.com")
    db.close()
    
    db = Database("test.db")
    row = db.find(1)
    assert row[1] == "Bob"  # ✗ FAIL! Still "Alice"
```

**Discovery process:**
1. Wrote test for update
2. Test passed (in-memory worked)
3. Added persistence test
4. Persistence test failed
5. Debugged: added print statements to flush()
6. Saw update page never flushed (clean!)
7. Found missing mark_page_dirty()

**Bug #2 (Delete corruption):**
```python
# Discovered by: Segmentation fault in test
def test_multiple_deletes():
    db = Database("test.db")
    for i in range(20):
        db.insert(i, f"user{i}", f"email{i}")
    
    for i in range(5, 15):
        db.delete(i)  # Randomly crashes here!
```

**Discovery process:**
1. Test crashed with segfault
2. Ran under gdb debugger
3. Backtrace showed corruption in cell data
4. Added memory sanitizer: detected overlap
5. Found memcpy() in delete
6. Realized source/dest overlap
7. Changed to memmove()

**Bug #3 (O(n²) sort):**
```python
# Discovered by: Performance profiling
import cProfile

cProfile.run('db.select_all()')

# Output:
#   99% time spent in execute_select
#     95% in nested loops (sort)
```

**Discovery process:**
1. User complaint: "SELECT is very slow"
2. Profiled with cProfile
3. 95% time in sort
4. Looked at sort algorithm: bubble sort!
5. Changed to std::sort()
6. 300× speedup

**Bug #4 (Rebalance persistence):**
```python
# Discovered by: Dedicated rebalance test
def test_split_persistence():
    db = Database("test.db")
    for i in range(20):
        db.insert(i, f"user{i}", f"email{i}")
    height_before = db.tree_height()
    db.close()
    
    db = Database("test.db")
    height_after = db.tree_height()
    assert height_before == height_after  # ✗ FAIL!
```

**Discovery process:**
1. Bug #1 made us suspicious
2. Created rebalance persistence tests
3. Tests failed immediately
4. Added dirty tracking debug prints
5. Saw split pages not marked dirty
6. Fixed all rebalancing operations

---

### Q86: What testing strategy prevents these bugs?
**Answer:**

**Prevention strategies:**

**1. Persistence testing pattern:**
```python
def test_operation_persistence():
    """Every operation needs persistence test"""
    # Phase 1: Execute operation
    db = Database("test.db")
    state_before = capture_state(db)
    db.operation(...)
    db.close()
    
    # Phase 2: Verify persistence
    db = Database("test.db")
    state_after = capture_state(db)
    assert state_before == state_after  # Must be identical!
```

**2. Validation after every operation:**
```python
def test_with_validation():
    db = Database("test.db")
    
    for i in range(100):
        db.insert(i, f"user{i}", f"email{i}")
        assert db.validate_tree() == True  # Catch corruption early!
```

**3. Memory sanitizers:**
```bash
# Compile with AddressSanitizer
g++ -fsanitize=address -g db.cpp -o db

# Run tests
./run_tests

# Output if bug:
# ERROR: AddressSanitizer: heap-buffer-overflow
# WRITE of size 291 at 0x... (memcpy with overlap)
```

**4. Profiling before deployment:**
```bash
# Profile with perf
perf record ./db
perf report

# Look for hot spots, unexpected O(n²) loops
```

**5. Fuzzing:**
```python
def test_random_operations():
    """Random operations to find edge cases"""
    db = Database("test.db")
    
    for _ in range(10000):
        op = random.choice(['insert', 'delete', 'update', 'find'])
        key = random.randint(0, 1000)
        
        try:
            execute_op(db, op, key)
            assert db.validate_tree() == True
        except Exception as e:
            print(f"Bug found! op={op}, key={key}")
            raise
```

**6. Code review checklist:**
```
Before committing code that modifies tree:
[ ] Every page modification followed by mark_page_dirty()?
[ ] Using memmove() for potential overlaps?
[ ] Persistence test added?
[ ] Validation test added?
[ ] Performance profiled?
[ ] Edge cases covered?
```

---

### Q87: What lessons were learned from debugging?
**Answer:**

**Key lessons:**

**1. Test persistence early and often**
```
❌ Write code → test in memory → ship
✅ Write code → test in memory → test persistence → ship
```

**2. Dirty tracking is critical**
```cpp
// Simple rule:
if (you_modified_page_memory) {
    mark_page_dirty(pager, page_num);  // ALWAYS!
}
```

**3. Memory operations need care**
```cpp
// Decision tree:
if (source and dest might overlap) {
    memmove(dest, src, size);  // Safe
} else {
    memcpy(dest, src, size);   // Fast
}

// When in doubt: use memmove!
```

**4. Profile before optimizing, but also after implementing**
```
Don't assume performance.
Measure it!
```

**5. Write tests that mimic production**
```python
# ❌ Bad test: Only in-memory
def test_insert():
    db.insert(1, "user", "email")
    assert db.find(1) is not None

# ✅ Good test: Like real usage
def test_insert():
    db.insert(1, "user", "email")
    db.close()  # Like user closing program
    db = Database("test.db")  # Like user reopening
    assert db.find(1) is not None
```

**6. Validation is your friend**
```cpp
// After ANY tree modification:
if (!validate_tree(table)) {
    std::cerr << "VALIDATION FAILED!" << std::endl;
    std::cerr << "Last operation: " << last_op << std::endl;
    abort();  // Fail fast!
}
```

**7. Complex operations need extra scrutiny**
```
Simple operation (find): 1 page touched
Complex operation (split): 3+ pages touched, parent pointers updated

More complexity = more bugs
→ More testing needed!
```

**8. Document discovered bugs**
```
Create regression tests in test_bug_fixes.py
Include:
  - What the bug was
  - How to reproduce
  - What the fix was
  - Test that it doesn't regress
```

---

### Q88: How do you prevent regressions?
**Answer:**

**Regression prevention:**

**1. Dedicated regression test file:**
```python
# test_bug_fixes.py
def test_bug_1_update_persistence():
    """
    Bug #1: Updates didn't persist
    Fixed: Added mark_page_dirty() in execute_update()
    """
    db = Database("test.db")
    db.insert(1, "Alice", "alice@test.com")
    db.update(1, "Bob", "bob@test.com")
    db.close()
    
    db = Database("test.db")
    row = db.find(1)
    assert row == (1, "Bob", "bob@test.com"), \
        "Bug #1 regression: Update not persisting!"
    db.close()
```

**2. Run all tests before commit:**
```bash
# Pre-commit hook
#!/bin/bash
echo "Running test suite..."
pytest test.py test_rebalance_persistence.py test_bug_fixes.py

if [ $? -ne 0 ]; then
    echo "Tests failed! Commit aborted."
    exit 1
fi

echo "All tests passed!"
```

**3. Continuous Integration:**
```yaml
# .github/workflows/test.yml
name: Test Suite
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install dependencies
        run: pip install pytest
      - name: Run tests
        run: pytest --verbose
```

**4. Test coverage monitoring:**
```bash
# Check coverage didn't drop
coverage run -m pytest
coverage report

# Fail if coverage < 95%
coverage report --fail-under=95
```

**5. Static analysis:**
```bash
# Catch common bugs automatically
cppcheck db.hpp --enable=all

# Warnings:
# [db.hpp:1234]: (warning) Possible null pointer dereference
# [db.hpp:2345]: (warning) Memory leak: node
```

**6. Code review focus:**
```
Reviewer checklist:
[ ] Does this modify tree pages?
[ ] Is mark_page_dirty() called?
[ ] Persistence test included?
[ ] Memory operations safe (memmove)?
[ ] Validate_tree() called in test?
```

**7. Documentation:**
```cpp
// In code:
// BUG FIX #1: Must mark page dirty after update!
// See: test_bug_fixes.py::test_bug_1_update_persistence
mark_page_dirty(pager, page_num);
```

**8. Performance regression tests:**
```python
def test_select_performance():
    """Ensure Bug #3 doesn't regress"""
    db = Database("test.db")
    
    # Insert 10,000 records
    for i in range(10000):
        db.insert(i, f"user{i}", f"email{i}")
    
    # Select should be fast
    start = time.time()
    db.select_all()
    elapsed = time.time() - start
    
    # Should be < 1 second (was 45s with bug!)
    assert elapsed < 1.0, f"SELECT too slow: {elapsed:.2f}s"
```

---

### Q89: What debugging tools did you use?
**Answer:**

**Debugging toolkit:**

**1. Print debugging:**
```cpp
void mark_page_dirty(Pager* pager, uint32_t page_num) {
    // Debug: Log every dirty mark
    std::cout << "[DEBUG] Marked page " << page_num << " dirty" << std::endl;
    
    pager->dirty_pages[page_num] = true;
}

void pager_flush(Pager* pager) {
    std::cout << "[DEBUG] Flushing dirty pages..." << std::endl;
    for (auto& [page_num, is_dirty] : pager->dirty_pages) {
        if (is_dirty) {
            std::cout << "[DEBUG]   Writing page " << page_num << std::endl;
            // ... write ...
        }
    }
}
```

**2. GDB (GNU Debugger):**
```bash
# Compile with debug symbols
g++ -g db.cpp -o db

# Run under debugger
gdb ./db

# When crash occurs:
(gdb) run
...
Program received signal SIGSEGV, Segmentation fault.

(gdb) backtrace
#0  leaf_node_delete (...) at db.hpp:1234
#1  execute_delete (...) at db.hpp:2345
#2  main () at main.cpp:56

(gdb) print cursor->cell_num
$1 = 5

(gdb) print num_cells
$2 = 10

# Examine memory
(gdb) x/32x node
0x7fff1234: 0x00000001 0x00000000 0x0000000a ...
```

**3. AddressSanitizer:**
```bash
# Compile with sanitizer
g++ -fsanitize=address -g db.cpp -o db

# Run
./db

# Output if memory error:
=================================================================
==12345==ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 291 at 0x7f1234567890
    #0 in memcpy
    #1 in leaf_node_delete db.hpp:1234
    
0x7f1234567890 is located 0 bytes inside of 4096-byte region
Allocated at:
    #0 in malloc
    #1 in pager_get_page db.hpp:456
=================================================================
```

**4. Valgrind:**
```bash
# Check for memory leaks
valgrind --leak-check=full ./db

# Output:
==12345== HEAP SUMMARY:
==12345==     in use at exit: 4,096 bytes in 1 blocks
==12345==   total heap usage: 100 allocs, 99 frees, 409,600 bytes allocated
==12345== 
==12345== 4,096 bytes in 1 blocks are definitely lost
==12345==    at malloc
==12345==    by pager_get_page (db.hpp:456)
==12345==    by table_open (db.hpp:789)
```

**5. Python debugging:**
```python
import pdb

def test_with_debug():
    db = Database("test.db")
    
    for i in range(20):
        db.insert(i, f"user{i}", f"email{i}")
    
    # Drop into debugger
    pdb.set_trace()
    
    # Can now:
    # (Pdb) print db.tree_height()
    # (Pdb) print db.count_nodes()
    # (Pdb) db.print_tree()
```

**6. Profiling:**
```python
import cProfile
import pstats

profiler = cProfile.Profile()
profiler.enable()

# Code to profile
db.select_all()

profiler.disable()

# Print stats
stats = pstats.Stats(profiler)
stats.sort_stats('cumulative')
stats.print_stats(10)  # Top 10 functions
```

**7. Custom validation:**
```cpp
#define DEBUG_VALIDATION 1

void leaf_node_insert(...) {
    // ... insert logic ...
    
    #ifdef DEBUG_VALIDATION
    if (!validate_tree(table)) {
        std::cerr << "ERROR: Tree invalid after insert!" << std::endl;
        print_tree(table);
        abort();
    }
    #endif
}
```

---

## 📝 Summary - Part 6 Topics Covered

✅ **Bug Overview** - 4 major bugs, severity, discovery  
✅ **Bug #1** - Update persistence (missing dirty mark)  
✅ **Bug #2** - Delete corruption (memcpy vs memmove)  
✅ **Bug #3** - SELECT O(n²) (bubble sort)  
✅ **Bug #4** - Rebalance persistence (missing dirty marks)  
✅ **Bug Discovery** - How each bug was found  
✅ **Prevention Strategy** - Persistence tests, validation  
✅ **Lessons Learned** - Dirty tracking, memory operations  
✅ **Regression Prevention** - Test files, CI, coverage  
✅ **Debugging Tools** - GDB, sanitizers, profiling  

---

**Next:** [VIVA_QUESTIONS_PART7.md](./VIVA_QUESTIONS_PART7.md) - Performance & Advanced Topics

**See Also:**
- [BUG_FIXES_NOV_1_2025.md](../presentation/BUG_FIXES_NOV_1_2025.md) - Detailed bug documentation
- [test_bug_fixes.py](../test_bug_fixes.py) - Regression tests
