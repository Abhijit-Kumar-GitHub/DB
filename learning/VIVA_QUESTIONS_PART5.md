# 🎓 ArborDB Viva/Interview Questions - Part 5: Testing & Validation

## Overview
This part covers the validation algorithm, test suite structure, and debugging strategies.

---

## ✅ Section 5: Testing & Validation (12 Questions)

### Q68: Why is validate_tree() critical?
**Answer:**

**Purpose:** Verify B-Tree invariants are maintained

**When to run:**
- After every modification operation
- In test suite
- Before/after database restart
- When debugging corruption

**What it checks:**

**1. Parent-child consistency:**
```cpp
// Every child's parent pointer must point back to parent
for (each child in node) {
    assert(child->parent == node_page_num);
}
```

**2. Key ordering:**
```cpp
// Keys must be in ascending order
for (i = 0; i < num_cells - 1; i++) {
    assert(key[i] < key[i+1]);
}
```

**3. Cell count bounds:**
```cpp
// Non-root nodes must have [min, max] cells
if (!is_root) {
    assert(num_cells >= MIN_CELLS);
    assert(num_cells <= MAX_CELLS);
}
```

**4. Internal node invariants:**
```cpp
// Keys in child[i] must be ≤ key[i]
for (each child[i]) {
    max_key_in_subtree = get_node_max_key(child[i]);
    assert(max_key_in_subtree <= key[i]);
}
```

**5. Sibling link integrity:**
```cpp
// Leaves form valid linked list
for (each leaf) {
    if (next_leaf != 0) {
        assert(next_leaf is valid page);
        assert(next_leaf contains sorted keys > current max);
    }
}
```

**Why critical:**

**Without validation:**
```python
db.insert(42, "user", "email")  # Looks OK
db.close()
db = Database("test.db")
db.find(42)  # CRASH! Corruption not detected
```

**With validation:**
```python
db.insert(42, "user", "email")
assert db.validate_tree() == True  # Catches bugs early!
```

**Real bug caught by validation:**
```python
# Bug #4: Missing mark_page_dirty in split
def test_split_persistence():
    db = Database("test.db")
    for i in range(20):
        db.insert(i, f"user{i}", f"email{i}")
    
    assert db.validate_tree() == True  # ✓ OK in memory
    db.close()
    
    db = Database("test.db")
    assert db.validate_tree() == True  # ✗ FAIL! Caught bug!
```

---

### Q69: Explain the validate_tree_node() algorithm.
**Answer:**

**Recursive DFS validation**

**Algorithm:**
```
1. Check node header validity
2. Check cell/key counts
3. Check key ordering (ascending)
4. If internal node:
   - Validate each child recursively
   - Check parent pointers
   - Check key invariants
5. If leaf node:
   - Check sibling link
   - Accumulate keys for global ordering check
6. Return success/failure
```

**Code:**

```cpp
bool validate_tree_node(Pager* pager, uint32_t page_num, 
                       uint32_t* expected_parent,
                       std::vector<uint32_t>& all_keys) {
    void* node = pager_get_page(pager, page_num);
    
    // Check parent pointer
    uint32_t actual_parent = *get_node_parent(node);
    if (expected_parent != nullptr && actual_parent != *expected_parent) {
        std::cout << "ERROR: Parent mismatch at page " << page_num << std::endl;
        return false;
    }
    
    if (get_node_type(node) == NODE_LEAF) {
        uint32_t num_cells = *get_leaf_node_num_cells(node);
        
        // Check bounds (except root)
        if (!is_node_root(node)) {
            if (num_cells < LEAF_NODE_MIN_CELLS) {
                std::cout << "ERROR: Leaf underflow at page " << page_num 
                         << " (" << num_cells << " cells)" << std::endl;
                return false;
            }
        }
        
        if (num_cells > LEAF_NODE_MAX_CELLS) {
            std::cout << "ERROR: Leaf overflow at page " << page_num 
                     << " (" << num_cells << " cells)" << std::endl;
            return false;
        }
        
        // Check key ordering
        for (uint32_t i = 0; i < num_cells; i++) {
            uint32_t key = *get_leaf_node_key(node, i);
            
            if (i > 0 && key <= all_keys.back()) {
                std::cout << "ERROR: Key ordering violation at page " 
                         << page_num << std::endl;
                return false;
            }
            
            all_keys.push_back(key);
        }
        
        // Check sibling link
        uint32_t next_leaf = *get_leaf_node_next_leaf(node);
        if (next_leaf != 0 && next_leaf >= pager->num_pages) {
            std::cout << "ERROR: Invalid sibling link at page " 
                     << page_num << std::endl;
            return false;
        }
        
    } else {  // Internal node
        uint32_t num_keys = *get_internal_node_num_keys(node);
        
        // Check bounds (except root)
        if (!is_node_root(node)) {
            if (num_keys < INTERNAL_NODE_MIN_KEYS) {
                std::cout << "ERROR: Internal underflow at page " << page_num
                         << " (" << num_keys << " keys)" << std::endl;
                return false;
            }
        }
        
        if (num_keys > INTERNAL_NODE_MAX_CELLS) {
            std::cout << "ERROR: Internal overflow at page " << page_num
                     << " (" << num_keys << " keys)" << std::endl;
            return false;
        }
        
        // Validate each child
        for (uint32_t i = 0; i < num_keys; i++) {
            uint32_t child_page = *get_internal_node_child(node, i);
            uint32_t key = *get_internal_node_key(node, i);
            
            // Recurse into child
            if (!validate_tree_node(pager, child_page, &page_num, all_keys)) {
                return false;
            }
            
            // Check that max key in child matches separator
            uint32_t child_max = all_keys.back();
            if (child_max != key) {
                std::cout << "ERROR: Separator key mismatch at page " 
                         << page_num << ", index " << i << std::endl;
                return false;
            }
        }
        
        // Validate right child
        uint32_t right_child = *get_internal_node_right_child(node);
        if (!validate_tree_node(pager, right_child, &page_num, all_keys)) {
            return false;
        }
    }
    
    return true;
}

// Entry point
bool validate_tree(Table* table) {
    std::vector<uint32_t> all_keys;
    return validate_tree_node(table->pager, 
                             table->pager->root_page_num, 
                             nullptr, 
                             all_keys);
}
```

**Visual trace:**

**Tree:**
```
         Root[50]
        /        \
    Leaf[10,20]  Leaf[50,60]
```

**Validation steps:**
```
1. validate_tree_node(Root):
   - Type: Internal
   - num_keys = 1 ✓
   - Recurse into child0 (Leaf[10,20])
   
2. validate_tree_node(Leaf[10,20]):
   - Type: Leaf
   - num_cells = 2 ✓
   - Keys in order: 10 < 20 ✓
   - Add to all_keys: [10, 20]
   - Return true
   
3. Back in Root:
   - Child0 max key = 20
   - Separator key[0] = 50
   - ✗ MISMATCH! 20 != 50
   - ERROR: Separator key wrong!
```

---

### Q70: How many tests are in the test suite?
**Answer:**

**Total: 40 tests across 3 files**

**Breakdown:**

**test.py (31 tests):**

| Category | Count | Examples |
|----------|-------|----------|
| Insert | 8 | empty table, duplicate keys, ordering, persistence |
| Find | 5 | exists, not exists, after insert, persistence |
| Delete | 5 | basic, not exists, multiple, reinsert |
| Update | 4 | basic, not exists, persistence |
| Select | 4 | empty, single, multiple, after delete |
| Range | 2 | basic, persistence |
| Edge Cases | 3 | max capacity, empty operations, large values |

**test_rebalance_persistence.py (5 tests):**
- Leaf split persistence
- Internal split persistence
- Leaf merge persistence
- Internal merge persistence
- Mixed operations

**test_bug_fixes.py (4 tests):**
- Bug #1: Update persistence fix
- Bug #2: Delete persistence fix (memcpy→memmove)
- Bug #3: Sort complexity fix (O(n²)→O(n log n))
- Bug #4: Rebalance persistence fix

**Test coverage summary:**
```
✅ Basic CRUD operations
✅ Tree rebalancing (split/merge/borrow)
✅ Persistence across restarts
✅ Edge cases (empty, full, boundaries)
✅ Concurrency (cursor iteration)
✅ Validation after each operation
✅ Bug regression tests
```

---

### Q71: What is the test structure/pattern?
**Answer:**

**Standard pattern for each test:**

```python
def test_operation():
    """
    Test description
    """
    # 1. Setup
    db = Database("test.db")
    
    # 2. Initial state
    # Insert test data, verify baseline
    
    # 3. Execute operation
    result = db.operation(...)
    
    # 4. Assertions
    assert result == expected
    assert db.validate_tree() == True
    
    # 5. Cleanup
    db.close()
    os.remove("test.db")
```

**Example - test_insert_and_find:**

```python
def test_insert_and_find():
    """Test inserting and finding a single record"""
    # Setup
    db = Database("test.db")
    
    # Execute
    result = db.insert(1, "Alice", "alice@test.com")
    assert result == "Success"
    
    # Verify
    row = db.find(1)
    assert row == (1, "Alice", "alice@test.com")
    assert db.validate_tree() == True
    
    # Cleanup
    db.close()
    os.remove("test.db")
```

**Persistence test pattern:**

```python
def test_operation_persistence():
    """Test that operation persists across restart"""
    # Phase 1: Modify database
    db = Database("test.db")
    db.operation(...)
    state_before = db.get_state()
    db.close()
    
    # Phase 2: Reopen and verify
    db = Database("test.db")
    state_after = db.get_state()
    assert state_before == state_after
    assert db.validate_tree() == True
    
    # Cleanup
    db.close()
    os.remove("test.db")
```

**Stress test pattern:**

```python
def test_large_scale_operation():
    """Test with large dataset"""
    db = Database("test.db")
    
    # Insert many records
    for i in range(10000):
        db.insert(i, f"user{i}", f"email{i}@test.com")
        
        # Periodic validation
        if i % 1000 == 0:
            assert db.validate_tree() == True
    
    # Verify all data
    for i in range(10000):
        row = db.find(i)
        assert row[0] == i
    
    db.close()
    os.remove("test.db")
```

**Edge case pattern:**

```python
def test_edge_case():
    """Test boundary condition"""
    db = Database("test.db")
    
    # Create edge condition
    setup_edge_case(db)
    
    # Execute operation that might fail
    try:
        result = db.operation(...)
        assert result == expected_for_edge_case
    except Exception as e:
        # Sometimes edge case should raise exception
        assert isinstance(e, ExpectedException)
    
    # Tree should still be valid
    assert db.validate_tree() == True
    
    db.close()
    os.remove("test.db")
```

---

### Q72: How do you test leaf node splits?
**Answer:**

**Strategy:** Force leaf to max capacity, then insert one more

**Test code:**

```python
def test_leaf_split():
    """Test splitting a full leaf node"""
    db = Database("test.db")
    
    # Step 1: Fill leaf to max (13 cells)
    for i in range(13):
        result = db.insert(i * 10, f"user{i}", f"email{i}@test.com")
        assert result == "Success"
    
    # Verify we have single leaf (root)
    assert db.tree_height() == 1
    assert db.count_nodes() == 1
    
    # Step 2: Insert 14th record - triggers split!
    result = db.insert(135, "user13", "email13@test.com")
    assert result == "Success"
    
    # Step 3: Verify split occurred
    assert db.tree_height() == 2  # Now has internal node!
    assert db.count_leaf_nodes() == 2  # Two leaves
    assert db.count_internal_nodes() == 1  # One internal (root)
    
    # Step 4: Verify all data accessible
    for i in range(14):
        key = i * 10 if i < 13 else 135
        row = db.find(key)
        assert row is not None
        assert row[0] == key
    
    # Step 5: Validate tree structure
    assert db.validate_tree() == True
    
    db.close()
    os.remove("test.db")
```

**What it checks:**
- ✅ Tree height increases (1 → 2)
- ✅ Node count increases (1 → 3)
- ✅ All data still accessible
- ✅ Tree remains valid

**Visual of what happens:**

**Before (13 cells):**
```
Root (Leaf): [0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120]
```

**Insert 135:**
```
Split triggered!
```

**After:**
```
         Root (Internal)
         [child0, 60, right_child]
         /                    \
    Left Leaf              Right Leaf
    [0-60]                 [70-135]
```

---

### Q73: How do you test internal node splits?
**Answer:**

**Strategy:** Create enough leaves to overflow internal node (510+ children)

**Challenge:** Need 13 × 511 = 6,643 records minimum!

**Test code:**

```python
def test_internal_split():
    """Test splitting an internal node (rare!)"""
    db = Database("test.db")
    
    # Calculate records needed
    # Internal node max: 510 keys = 511 children
    # Each leaf holds 13 records
    # Need 512 leaves to trigger split
    records_needed = 13 * 512
    
    # Insert records
    print(f"Inserting {records_needed} records...")
    for i in range(records_needed):
        db.insert(i, f"user{i}", f"email{i}@test.com")
        
        # Validate every 1000 records
        if i % 1000 == 0 and i > 0:
            print(f"  Inserted {i}...")
            assert db.validate_tree() == True
    
    # Verify tree grew
    height = db.tree_height()
    assert height >= 3, f"Expected height >= 3, got {height}"
    
    # Count nodes
    internal_count = db.count_internal_nodes()
    leaf_count = db.count_leaf_nodes()
    
    print(f"Tree height: {height}")
    print(f"Internal nodes: {internal_count}")
    print(f"Leaf nodes: {leaf_count}")
    
    # Verify data integrity
    print("Verifying all records...")
    for i in range(0, records_needed, 100):  # Sample every 100th
        row = db.find(i)
        assert row is not None
        assert row[0] == i
    
    assert db.validate_tree() == True
    
    db.close()
    os.remove("test.db")
```

**Why this test is important:**
- Tests deep tree structure
- Validates parent pointer updates across many nodes
- Ensures performance scales to large datasets
- Catches bugs only visible at scale

**Typical results:**
```
Inserting 6656 records...
  Inserted 1000...
  Inserted 2000...
  ...
  Inserted 6000...
Tree height: 3
Internal nodes: 2
Leaf nodes: 513
✓ All validations passed
```

---

### Q74: How do you test merge operations?
**Answer:**

**Strategy:** Create underflow by deleting records

**Test code:**

```python
def test_leaf_merge():
    """Test merging underflowing leaf nodes"""
    db = Database("test.db")
    
    # Step 1: Create multiple leaves (need split first)
    # Insert 30 records → will create ~3 leaves
    for i in range(30):
        db.insert(i, f"user{i}", f"email{i}@test.com")
    
    initial_leaves = db.count_leaf_nodes()
    assert initial_leaves >= 2, "Need multiple leaves to test merge"
    
    # Step 2: Delete records to cause underflow
    # Delete from middle leaf to trigger merge with sibling
    for i in range(15, 25):
        result = db.delete(i)
        assert result == "Success"
    
    # Step 3: Verify merge occurred
    final_leaves = db.count_leaf_nodes()
    assert final_leaves < initial_leaves, "Merge should reduce leaf count"
    
    # Step 4: Verify remaining data
    for i in range(30):
        row = db.find(i)
        if i < 15 or i >= 25:
            # Should exist
            assert row is not None
            assert row[0] == i
        else:
            # Should not exist (deleted)
            assert row is None
    
    # Step 5: Validate tree
    assert db.validate_tree() == True
    
    db.close()
    os.remove("test.db")
```

**Visual trace:**

**After inserts (30 records):**
```
         Root
        /  |  \
    L1  L2  L3
   [0-9][10-19][20-29]
```

**After deleting 15-24:**
```
L2 now has: [10, 11, 12, 13, 14]  ← 5 cells (underflow!)
L3 has: [25, 26, 27, 28, 29]       ← 5 cells

Both are below min (6), so merge:
```

**After merge:**
```
      Root
      /  \
    L1   L2+L3
   [0-9] [10-14, 25-29]
```

---

### Q75: What are the key assertions in every test?
**Answer:**

**Essential assertions:**

**1. Operation success:**
```python
result = db.insert(1, "user", "email")
assert result == "Success"  # Not "Error"
```

**2. Tree validity:**
```python
assert db.validate_tree() == True  # ALWAYS after modification!
```

**3. Data correctness:**
```python
row = db.find(42)
assert row == (42, "expected_username", "expected_email")
assert row[0] == 42  # Check specific fields
```

**4. State consistency:**
```python
# Before close
count_before = db.count_records()

db.close()

# After reopen
db = Database("test.db")
count_after = db.count_records()
assert count_before == count_after
```

**5. Error conditions:**
```python
# Duplicate key
db.insert(1, "user1", "email1")
result = db.insert(1, "user2", "email2")
assert result == "Duplicate key"

# Not found
row = db.find(999)
assert row is None
```

**6. Edge cases:**
```python
# Empty database
rows = db.select_all()
assert len(rows) == 0

# After delete all
for key in keys:
    db.delete(key)
assert db.count_records() == 0
assert db.validate_tree() == True  # Tree still valid!
```

**7. Ordering:**
```python
rows = db.select_all()
for i in range(len(rows) - 1):
    assert rows[i][0] < rows[i+1][0]  # Keys in ascending order
```

**Assertion checklist:**
```python
def comprehensive_test():
    db = Database("test.db")
    
    # 1. Operation result ✓
    result = db.operation(...)
    assert result == expected
    
    # 2. Tree validity ✓
    assert db.validate_tree() == True
    
    # 3. Data correctness ✓
    actual = db.get_data()
    assert actual == expected_data
    
    # 4. Node structure ✓
    assert db.tree_height() == expected_height
    assert db.count_nodes() == expected_count
    
    # 5. Persistence ✓
    db.close()
    db = Database("test.db")
    assert db.get_data() == expected_data
    
    # 6. Error handling ✓
    with pytest.raises(ExpectedException):
        db.invalid_operation()
    
    db.close()
```

---

### Q76: How do you test persistence?
**Answer:**

**Pattern:** Modify → Close → Reopen → Verify

**Basic persistence test:**

```python
def test_insert_persistence():
    """Test that inserts persist across database restart"""
    
    # Phase 1: Insert data
    db = Database("test.db")
    for i in range(10):
        db.insert(i, f"user{i}", f"email{i}@test.com")
    db.close()
    
    # Phase 2: Reopen and verify
    db = Database("test.db")
    for i in range(10):
        row = db.find(i)
        assert row is not None
        assert row == (i, f"user{i}", f"email{i}@test.com")
    
    assert db.validate_tree() == True
    db.close()
    os.remove("test.db")
```

**Advanced persistence test (rebalancing):**

```python
def test_split_persistence():
    """Test that tree splits persist to disk"""
    
    # Phase 1: Trigger split
    db = Database("test.db")
    
    # Fill to cause split
    for i in range(20):
        db.insert(i, f"user{i}", f"email{i}@test.com")
    
    # Capture state
    height_before = db.tree_height()
    nodes_before = db.count_nodes()
    keys_before = [row[0] for row in db.select_all()]
    
    assert height_before == 2  # Split should have occurred
    
    db.close()
    
    # Phase 2: Reopen and verify structure maintained
    db = Database("test.db")
    
    height_after = db.tree_height()
    nodes_after = db.count_nodes()
    keys_after = [row[0] for row in db.select_all()]
    
    # Structure should be identical
    assert height_after == height_before
    assert nodes_after == nodes_before
    assert keys_after == keys_before
    
    # Tree should be valid
    assert db.validate_tree() == True
    
    db.close()
    os.remove("test.db")
```

**Persistence bug detection:**

```python
def test_update_persistence():
    """Test Bug #1: Update persistence fix"""
    
    # Phase 1: Insert then update
    db = Database("test.db")
    db.insert(1, "Alice", "alice@test.com")
    db.update(1, "Bob", "bob@test.com")
    db.close()
    
    # Phase 2: Verify update persisted
    db = Database("test.db")
    row = db.find(1)
    
    # Bug #1 symptom: Row would still be Alice after restart
    # Fix: mark_page_dirty() in execute_update()
    assert row == (1, "Bob", "bob@test.com"), \
        "Update did not persist! Missing mark_page_dirty()?"
    
    db.close()
    os.remove("test.db")
```

**What persistence tests catch:**
- ✅ Missing `mark_page_dirty()` calls
- ✅ Incorrect flush logic
- ✅ Cache-only modifications
- ✅ Corrupted page writes
- ✅ Incomplete transactions

---

### Q77: How do you debug test failures?
**Answer:**

**Debugging toolkit:**

**1. Add verbose output:**
```python
def test_with_debug():
    db = Database("test.db", verbose=True)  # Enable debug mode
    
    db.insert(1, "user", "email")
    # Prints:
    #   [DEBUG] Inserted key=1 at page=0, cell=0
    #   [DEBUG] Marked page 0 dirty
    #   [DEBUG] Tree valid: True
```

**2. Inspect tree structure:**
```python
def test_with_inspection():
    db = Database("test.db")
    
    # ... operations ...
    
    # Print tree structure
    db.print_tree()
    # Output:
    #   Root (Internal, page 0):
    #     Child 0 (Leaf, page 1): [10, 20, 30]
    #     Child 1 (Leaf, page 2): [40, 50, 60]
    
    # Print specific node
    db.print_node(page_num=1)
```

**3. Step-by-step validation:**
```python
def test_with_validation():
    db = Database("test.db")
    
    for i in range(100):
        db.insert(i, f"user{i}", f"email{i}")
        
        # Validate after EVERY operation
        is_valid = db.validate_tree()
        if not is_valid:
            print(f"Tree invalid after inserting key {i}!")
            db.print_tree()
            assert False, "Validation failed"
```

**4. Compare before/after:**
```python
def test_with_comparison():
    db = Database("test.db")
    
    # Capture before state
    before = {
        'height': db.tree_height(),
        'nodes': db.count_nodes(),
        'keys': [row[0] for row in db.select_all()]
    }
    
    # Operation
    db.delete(50)
    
    # Capture after state
    after = {
        'height': db.tree_height(),
        'nodes': db.count_nodes(),
        'keys': [row[0] for row in db.select_all()]
    }
    
    # Analyze changes
    print(f"Height: {before['height']} → {after['height']}")
    print(f"Nodes: {before['nodes']} → {after['nodes']}")
    print(f"Keys removed: {set(before['keys']) - set(after['keys'])}")
```

**5. Binary search for failing operation:**
```python
def test_binary_search_failure():
    """Find which operation causes failure"""
    db = Database("test.db")
    
    operations = [
        (db.insert, 1, "user1", "email1"),
        (db.insert, 2, "user2", "email2"),
        # ... 1000 operations ...
    ]
    
    # Binary search
    low, high = 0, len(operations)
    while low < high:
        mid = (low + high) // 2
        
        # Try operations 0 to mid
        db = Database("test.db")
        for i in range(mid):
            op, *args = operations[i]
            op(*args)
        
        if db.validate_tree():
            low = mid + 1  # Failure is after mid
        else:
            high = mid  # Failure at or before mid
        
        db.close()
        os.remove("test.db")
    
    print(f"Failure at operation {low}: {operations[low]}")
```

**6. Use pytest's detailed output:**
```bash
# Run with verbose output
pytest test.py -v

# Stop on first failure
pytest test.py -x

# Show print statements
pytest test.py -s

# Run specific test
pytest test.py::test_leaf_split -v -s
```

**7. Check file state:**
```python
def test_file_inspection():
    db = Database("test.db")
    db.insert(1, "user", "email")
    db.close()
    
    # Check file size
    size = os.path.getsize("test.db")
    expected_size = 8 + 4096  # Header + 1 page
    assert size == expected_size, f"Unexpected file size: {size}"
    
    # Read raw bytes
    with open("test.db", "rb") as f:
        header = f.read(8)
        root_page = struct.unpack("I", header[:4])[0]
        free_head = struct.unpack("I", header[4:8])[0]
        print(f"Root: {root_page}, Free head: {free_head}")
```

---

### Q78: What edge cases should be tested?
**Answer:**

**Critical edge cases:**

**1. Empty database:**
```python
def test_empty_database():
    db = Database("test.db")
    
    # Select on empty
    rows = db.select_all()
    assert rows == []
    
    # Find on empty
    row = db.find(1)
    assert row is None
    
    # Delete on empty
    result = db.delete(1)
    assert result == "Record not found"
    
    # Update on empty
    result = db.update(1, "user", "email")
    assert result == "Record not found"
    
    # Tree still valid
    assert db.validate_tree() == True
```

**2. Single record:**
```python
def test_single_record():
    db = Database("test.db")
    
    # Insert one
    db.insert(42, "user", "email")
    
    # Find it
    row = db.find(42)
    assert row == (42, "user", "email")
    
    # Delete it
    db.delete(42)
    
    # Now empty
    rows = db.select_all()
    assert rows == []
    
    assert db.validate_tree() == True
```

**3. Boundary values:**
```python
def test_boundary_values():
    db = Database("test.db")
    
    # Min key
    db.insert(0, "user0", "email0")
    
    # Max key (uint32_t)
    db.insert(4294967295, "usermax", "emailmax")
    
    # Both should be findable
    assert db.find(0) is not None
    assert db.find(4294967295) is not None
    
    assert db.validate_tree() == True
```

**4. Exact capacity:**
```python
def test_exact_capacity():
    db = Database("test.db")
    
    # Fill to EXACTLY max cells (13)
    for i in range(13):
        db.insert(i, f"user{i}", f"email{i}")
    
    # Should be single leaf
    assert db.tree_height() == 1
    assert db.count_nodes() == 1
    
    # One more triggers split
    db.insert(13, "user13", "email13")
    assert db.tree_height() == 2
    
    assert db.validate_tree() == True
```

**5. Duplicate keys:**
```python
def test_duplicate_keys():
    db = Database("test.db")
    
    # Insert
    result1 = db.insert(1, "user1", "email1")
    assert result1 == "Success"
    
    # Try duplicate
    result2 = db.insert(1, "user2", "email2")
    assert result2 == "Duplicate key"
    
    # Original unchanged
    row = db.find(1)
    assert row == (1, "user1", "email1")
```

**6. Delete and reinsert:**
```python
def test_delete_reinsert():
    db = Database("test.db")
    
    # Insert
    db.insert(1, "Alice", "alice@test.com")
    
    # Delete
    db.delete(1)
    assert db.find(1) is None
    
    # Reinsert (should work!)
    db.insert(1, "Bob", "bob@test.com")
    row = db.find(1)
    assert row == (1, "Bob", "bob@test.com")
```

**7. Large strings (max length):**
```python
def test_max_string_length():
    db = Database("test.db")
    
    # Max username: 32 bytes
    long_username = "a" * 32
    # Max email: 255 bytes
    long_email = "b" * 255
    
    db.insert(1, long_username, long_email)
    row = db.find(1)
    assert row == (1, long_username, long_email)
```

**8. Alternating operations:**
```python
def test_alternating_ops():
    db = Database("test.db")
    
    # Insert some
    for i in range(10):
        db.insert(i * 2, f"user{i}", f"email{i}")
    
    # Delete alternate
    for i in range(0, 10, 2):
        db.delete(i * 2)
    
    # Insert in gaps
    for i in range(10):
        if i % 2 == 0:
            db.insert(i * 2, f"newuser{i}", f"newemail{i}")
    
    assert db.validate_tree() == True
```

---

### Q79: How do you measure test coverage?
**Answer:**

**Coverage dimensions:**

**1. Code coverage (lines executed):**
```bash
# Install coverage tool
pip install coverage

# Run tests with coverage
coverage run -m pytest test.py

# Generate report
coverage report -m

# Output:
Name         Stmts   Miss  Cover   Missing
------------------------------------------
db.hpp        2779     45    98%   1234-1245, 2500
test.py        450      0   100%
------------------------------------------
TOTAL         3229     45    99%
```

**2. Branch coverage (all paths):**
```python
# Example: Split operation
def leaf_node_split(...):
    if is_node_root(old_node):
        create_new_root(...)  # Branch A
    else:
        internal_node_insert(...)  # Branch B

# Need tests that cover BOTH branches:
test_split_root()      # Covers Branch A
test_split_non_root()  # Covers Branch B
```

**3. Operation coverage:**

| Operation | Basic | Edge Case | Error | Persist |
|-----------|-------|-----------|-------|---------|
| INSERT | ✓ | ✓ | ✓ | ✓ |
| FIND | ✓ | ✓ | ✓ | ✓ |
| DELETE | ✓ | ✓ | ✓ | ✓ |
| UPDATE | ✓ | ✓ | ✓ | ✓ |
| SELECT | ✓ | ✓ | - | ✓ |
| RANGE | ✓ | ✓ | - | ✓ |
| Split | ✓ | ✓ | - | ✓ |
| Merge | ✓ | ✓ | - | ✓ |
| Borrow | ✓ | ✓ | - | ✓ |

**4. State coverage:**

**Tree states tested:**
- ✅ Empty (0 records)
- ✅ Single record (1 record)
- ✅ Single leaf (1-13 records)
- ✅ Multiple leaves (14+ records)
- ✅ Height 2 (14-6643 records)
- ✅ Height 3 (6644+ records)
- ✅ Underflow states (post-delete)
- ✅ Overflow states (pre-split)

**5. Data pattern coverage:**
```python
# Sequential inserts
test_sequential_inserts()  # 1, 2, 3, ...

# Reverse inserts
test_reverse_inserts()  # 100, 99, 98, ...

# Random inserts
test_random_inserts()  # 42, 7, 99, 15, ...

# Gaps
test_insert_with_gaps()  # 10, 20, 30, ... (skip values)
```

**6. Coverage metrics:**

**ArborDB test suite coverage:**
```
Lines covered: 2734 / 2779 = 98.4%
Branches covered: 156 / 168 = 92.9%
Operations covered: 9 / 9 = 100%
Edge cases covered: 15 / 20 = 75%
```

**Uncovered code:**
- Error recovery paths (rare)
- Some freelist edge cases
- Hypothetical overflow conditions

---

## 📝 Summary - Part 5 Topics Covered

✅ **validate_tree()** - Purpose, what it checks, why critical  
✅ **validate_tree_node()** - Recursive DFS algorithm  
✅ **Test Suite Overview** - 40 tests across 3 files  
✅ **Test Structure** - Standard patterns, assertions  
✅ **Leaf Split Testing** - Forcing capacity, verification  
✅ **Internal Split Testing** - Large-scale testing  
✅ **Merge Testing** - Underflow creation, verification  
✅ **Key Assertions** - Essential checks in every test  
✅ **Persistence Testing** - Close/reopen pattern  
✅ **Debugging Strategies** - Verbose mode, inspection, binary search  
✅ **Edge Cases** - Empty, single, boundary, capacity  
✅ **Test Coverage** - Code, branch, operation, state coverage  

---

**Next:** [VIVA_QUESTIONS_PART6.md](./VIVA_QUESTIONS_PART6.md) - Bug Fixes & Lessons Learned

**See Also:**
- [TESTING_GUIDE.md](./TESTING_GUIDE.md) - All 40 tests explained
- [CODE_WALKTHROUGH_PART3.md](./CODE_WALKTHROUGH_PART3.md) - Validation deep dive
- [test.py](../test.py), [test_rebalance_persistence.py](../test_rebalance_persistence.py), [test_bug_fixes.py](../test_bug_fixes.py)
