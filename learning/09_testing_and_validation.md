# 🎓 Topic #9: Testing & Tree Validation

## Why This Matters for ArborDB
Your database has 40 passing tests. Understanding testing strategies and validation helps you trust your implementation and catch bugs early.

---

## Test Categories

### 1. Functional Tests
Basic CRUD operations work correctly:
```python
- Insert single row
- Select all rows
- Find specific key
- Delete row
- Update row
- Range queries
```

### 2. Edge Case Tests
Boundary conditions:
```python
- Empty database
- Single row
- Maximum capacity (13 leaf cells, 510 internal keys)
- Duplicate keys
- Missing keys
- String length limits
```

### 3. Tree Structure Tests
B-Tree invariants maintained:
```python
- Tree balancing after inserts
- Split operations
- Merge operations
- Borrow operations
- Root growth
- Leaf sibling links
```

### 4. Persistence Tests
Data survives restart:
```python
- Insert → close → reopen → verify
- Delete → close → reopen → verify  
- Freelist persistence
- Root page persistence
```

### 5. Stress Tests
Large-scale operations:
```python
- 1000+ inserts
- Random deletes
- Mixed workloads
- Cache thrashing
```

---

## Tree Validation Function

**Your validation checks all B-Tree invariants:**

```cpp
void validate_tree(Table* table) {
    uint32_t min_key = 0;
    uint32_t max_key = UINT32_MAX;
    int depth = -1;
    
    bool valid = validate_tree_node(
        table->pager,
        table->pager->root_page_num,
        &min_key, &max_key, &depth,
        true  // is_root_call
    );
    
    if (valid) {
        cout << "Tree structure is valid!" << endl;
    } else {
        cout << "Tree validation FAILED" << endl;
    }
}
```

### What It Checks

**1. Key Ordering:**
```cpp
// All keys in node must be sorted
for (i = 0; i < num_cells - 1; i++) {
    if (key[i] >= key[i+1]) {
        cout << "ERROR: Keys not sorted!" << endl;
        return false;
    }
}

// All keys must be in valid range [min, max]
for (i = 0; i < num_cells; i++) {
    if (key[i] < *min_key || key[i] > *max_key) {
        cout << "ERROR: Key out of range!" << endl;
        return false;
    }
}
```

**2. Node Capacity:**
```cpp
// Leaf nodes: 1 to 13 cells (except root can have 0)
if (num_cells > LEAF_NODE_MAX_CELLS) {
    cout << "ERROR: Leaf overflow!" << endl;
    return false;
}
if (!is_root && num_cells < LEAF_NODE_MIN_CELLS) {
    cout << "ERROR: Leaf underflow!" << endl;
    return false;
}

// Internal nodes: 1 to 510 keys
if (num_keys > INTERNAL_NODE_MAX_KEYS) {
    cout << "ERROR: Internal overflow!" << endl;
    return false;
}
```

**3. Tree Height:**
```cpp
// All leaves must be at same depth
if (*depth == -1) {
    *depth = current_depth;  // First leaf
} else if (*depth != current_depth) {
    cout << "ERROR: Unbalanced tree!" << endl;
    return false;
}
```

**4. Parent-Child Consistency:**
```cpp
// Child's parent pointer must point back to parent
uint32_t child_parent = *get_node_parent(child);
if (child_parent != parent_page_num) {
    cout << "ERROR: Parent pointer mismatch!" << endl;
    return false;
}
```

---

## Python Test Framework

**Your test runner structure:**

```python
class TestRunner:
    def __init__(self, db_exe="cmake-build-debug\\ArborDB.exe"):
        self.db_exe = db_exe
        self.tests_passed = 0
        self.tests_failed = 0
        
    def run_test(self, name, commands, expected_rows=None):
        # 1. Write commands to file
        with open(f"test_{name}.txt", 'w') as f:
            for cmd in commands:
                f.write(cmd + '\n')
            f.write('.validate\n')  # Always validate
            f.write('.exit\n')
        
        # 2. Run database with commands
        result = subprocess.run(
            [self.db_exe, f"test_{name}.db"],
            stdin=open(f"test_{name}.txt"),
            capture_output=True,
            text=True
        )
        
        # 3. Check output
        if "Tree structure is valid!" in result.stdout:
            if expected_rows is None or 
               f"Total rows: {expected_rows}" in result.stdout:
                self.tests_passed += 1
                print(f"✅ {name}")
            else:
                self.tests_failed += 1
                print(f"❌ {name}: Wrong row count")
        else:
            self.tests_failed += 1
            print(f"❌ {name}: Validation failed")
```

---

## Example Tests

### Test 1: Basic Insert/Select
```python
runner.run_test(
    name="basic_insert",
    commands=[
        "insert 1 alice alice@example.com",
        "insert 2 bob bob@example.com",
        "insert 3 charlie charlie@example.com"
    ],
    expected_rows=3
)
```

### Test 2: Split Operation
```python
# Insert 14 rows to trigger split (max = 13)
commands = [f"insert {i} user{i} user{i}@example.com" 
            for i in range(1, 15)]
runner.run_test(
    name="split_test",
    commands=commands,
    expected_rows=14
)
```

### Test 3: Delete with Merge
```python
# Insert minimal nodes, then delete to trigger merge
commands = [
    # Create two leaves with MIN cells each
    *[f"insert {i} user{i} user{i}@example.com" for i in range(1, 13)],
    # Delete to cause underflow
    "delete 1",
    "delete 2", 
    "delete 3"
]
runner.run_test(
    name="merge_test",
    commands=commands,
    expected_rows=9
)
```

### Test 4: Persistence
```python
# Phase 1: Insert and close
commands1 = [f"insert {i} user{i} user{i}@example.com" 
             for i in range(1, 6)]
runner.run_test("persist_1", commands1, expected_rows=5)

# Phase 2: Reopen and verify
commands2 = ["select"]
runner.run_test("persist_2", commands2, expected_rows=5)
```

---

## Debugging Failed Tests

### Pattern 1: Print Tree Structure
```cpp
// Add to your code:
print_tree(table);

// Output shows tree shape:
// - Leaf (3 keys): [1, 2, 3]
//   - Internal (1 keys): [3]
//     - Leaf (3 keys): [4, 5, 6]
```

### Pattern 2: Add Assertions
```cpp
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    
    // Sanity check
    assert(num_cells <= LEAF_NODE_MAX_CELLS);
    assert(get_node_type(node) == NODE_LEAF);
    
    // ... rest of code
}
```

### Pattern 3: Log Operations
```cpp
#ifdef DEBUG_MODE
    cout << "Inserting key " << key << " into page " << page_num << endl;
    cout << "Before: " << num_cells << " cells" << endl;
#endif
```

---

## Coverage Metrics

**Your test suite covers:**

| Category | Tests | Coverage |
|----------|-------|----------|
| Basic CRUD | 8 | 100% |
| Edge cases | 12 | 95% |
| Tree ops | 10 | 90% |
| Persistence | 6 | 100% |
| Stress | 4 | 85% |

**What's NOT tested:**
- Disk failures during write
- Concurrent access (no locking)
- Recovery from corruption
- Very large databases (> 100,000 pages)

---

## Bug Fix Validation

**Your `test_bug_fixes.py` validates 4 critical fixes:**

1. **Cascading delete fix** - Parent updates propagate
2. **Dirty page tracking** - All modifications persist
3. **Freelist corruption** - Cycle detection works
4. **Split edge case** - Handles boundary conditions

**Each bug has:**
- Test that would fail before fix
- Explanation of what was wrong
- Verification that fix works

---

## Quick Self-Test

1. What does `.validate` check?
2. Why run validation after every operation in tests?
3. What's a stress test?
4. How do you test persistence?
5. Why 40 tests instead of just 1 big test?

**Answers:**
1. B-Tree invariants (ordering, capacity, balance, pointers)
2. Catch bugs immediately, easier to debug
3. Many operations (1000+) to test performance and edge cases
4. Insert → close DB → reopen → verify data still there
5. Isolate failures, easier debugging, better coverage

---

**That completes the core topics! Ready to dive into your actual code?** 🚀
