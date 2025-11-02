# 🎓 ArborDB Complete Learning Path - Master Index

## 📋 Overview

This is your complete guide to mastering the ArborDB B-Tree database implementation. Follow this path serially for best results!

---

## 📚 Phase 1: Foundational Topics (Estimated: 3-4 hours)

### Prerequisites
- Basic C++ knowledge (variables, functions, loops, classes)
- Basic DSA (arrays, linked lists, sorting)

### Topics to Study

| # | Topic | File | Duration | Key Concepts |
|---|-------|------|----------|--------------|
| 1 | **Pointers & Memory** | [01_pointers_and_memory.md](./01_pointers_and_memory.md) | 20 min | void*, casting, pointer arithmetic |
| 2 | **Structs & Memory Layout** | [02_structs_and_memory_layout.md](./02_structs_and_memory_layout.md) | 20 min | Binary layouts, alignment, offsets |
| 3 | **File I/O & Binary Format** | [03_file_io_and_binary_format.md](./03_file_io_and_binary_format.md) | 25 min | fstream, seekg/seekp, binary data |
| 4 | **STL Containers** | [04_stl_containers.md](./04_stl_containers.md) | 25 min | map, list, set usage |
| 5 | **BST to B-Trees** | [05_bst_to_btrees.md](./05_bst_to_btrees.md) | 30 min | Tree fundamentals, B-Tree evolution |
| 6 | **LRU Cache** | [06_lru_cache_implementation.md](./06_lru_cache_implementation.md) | 30 min | O(1) cache, dirty tracking |
| 7 | **Split/Merge/Borrow** | [07_btree_split_merge_borrow.md](./07_btree_split_merge_borrow.md) | 40 min | Tree balancing algorithms |
| 8 | **Serialization** | [08_serialization_and_storage.md](./08_serialization_and_storage.md) | 25 min | Binary encoding/decoding |
| 9 | **Testing & Validation** | [09_testing_and_validation.md](./09_testing_and_validation.md) | 25 min | Test strategies, tree validation |

**Total:** ~4 hours of focused reading

---

## 🔍 Phase 2: Code Walkthrough (Estimated: 4-5 hours)

### Deep Dive into Implementation

| # | Part | File | Duration | What You'll Learn |
|---|------|------|----------|-------------------|
| 1 | **Architecture & Data Structures** | [CODE_WALKTHROUGH_PART1.md](./CODE_WALKTHROUGH_PART1.md) | 90 min | Pager, LRU cache, node layouts |
| 2 | **B-Tree Operations** | [CODE_WALKTHROUGH_PART2.md](./CODE_WALKTHROUGH_PART2.md) | 90 min | Insert, delete, split, merge |
| 3 | **Validation & Testing** | [CODE_WALKTHROUGH_PART3.md](./CODE_WALKTHROUGH_PART3.md) | 90 min | Complete traces, bug fixes |
| 4 | **Testing Guide** | [TESTING_GUIDE.md](./TESTING_GUIDE.md) | 60 min | All 40 tests explained |

**Total:** ~5.5 hours of code analysis

---

## 📂 Phase 3: Source Code Exploration (Estimated: 2-3 hours)

### Files to Read (in order)

| # | File | Lines | What to Focus On |
|---|------|-------|------------------|
| 1 | `db.hpp` | 278 | Constants, struct definitions, function prototypes |
| 2 | `main.cpp` (lines 1-500) | 500 | Pager functions, LRU cache, file I/O |
| 3 | `main.cpp` (lines 500-1000) | 500 | Parsers, serialization, node accessors |
| 4 | `main.cpp` (lines 1000-1500) | 500 | Insert, split operations |
| 5 | `main.cpp` (lines 1500-2000) | 500 | Delete, underflow handling (leaf) |
| 6 | `main.cpp` (lines 2000-2779) | 779 | Underflow handling (internal), validation, REPL |
| 7 | `test.py` | 819 | Main test suite (31 tests) |
| 8 | `test_rebalance_persistence.py` | 313 | Bug #4 tests (5 tests) |
| 9 | `test_bug_fixes.py` | 237 | Bug #1-3 tests (4 tests) |

**Recommendation:** Read alongside walkthrough documents for context!  
**See Also:** [TESTING_GUIDE.md](./TESTING_GUIDE.md) for complete test explanations

---

## 🧪 Phase 4: Testing & Experimentation (Estimated: 2-3 hours)

### 1. Run the Database (30 min)

```powershell
# Build the project
cd c:\Users\abhijit\CLionProjects\ArborDB
cmake --build cmake-build-debug

# Run interactively
.\cmake-build-debug\ArborDB.exe test.db

# Try commands:
db > insert 1 alice alice@example.com
db > insert 2 bob bob@example.com
db > select
db > .btree
db > .validate
db > .exit
```

### 2. Run Test Suite (30 min)

```powershell
# Basic tests
python test.py

# Advanced tests
python test_rebalance_persistence.py

# Bug fix verification
python test_bug_fixes.py
```

### 3. Experiment (60-90 min)

**Exercise 1: Trigger a split**
- Insert 14 rows into empty database
- Watch `.btree` output after each insert
- Observe root split at insert #14

**Exercise 2: Trigger a merge**
- Insert 20 rows
- Delete 15 rows
- Watch tree shrink via `.btree`
- Run `.validate` after each operation

**Exercise 3: Modify constants**
- Edit `db.hpp`: Change `LEAF_NODE_MAX_CELLS` to 7
- Rebuild and test
- Observe more frequent splits

---

## 🎯 Mastery Checklist

### Core Concepts

- [ ] Understand void* and reinterpret_cast usage
- [ ] Can explain LRU cache O(1) operations
- [ ] Know why dirty page tracking is critical
- [ ] Understand when mark_page_dirty() must be called
- [ ] Can trace binary search through internal nodes
- [ ] Understand 50/50 split distribution rationale
- [ ] Know when merge vs. borrow happens
- [ ] Understand recursive underflow handling
- [ ] Can explain why memmove vs. memcpy matters
- [ ] Know all 7 validation checks

### Implementation Details

- [ ] Can locate root_page_num in file header
- [ ] Know size of leaf node header (6 bytes)
- [ ] Understand cell layout (key + value)
- [ ] Can calculate LEAF_NODE_MAX_CELLS (13)
- [ ] Know INTERNAL_NODE_MAX_KEYS (510)
- [ ] Understand sibling link purpose (next_leaf)
- [ ] Can trace pager_get_page() cache hit/miss
- [ ] Know when pages get evicted from cache
- [ ] Understand freelist page recycling
- [ ] Can explain parent pointer updates

### Algorithms

- [ ] Can trace `table_find()` from root to leaf
- [ ] Understand `leaf_node_insert()` cell shifting
- [ ] Can walk through `leaf_node_split_and_insert()`
- [ ] Know `create_new_root()` tree growth
- [ ] Understand `handle_leaf_underflow()` decision tree
- [ ] Can trace borrow from sibling operation
- [ ] Understand merge operation and parent update
- [ ] Know `handle_internal_underflow()` recursion
- [ ] Can trace complete delete operation
- [ ] Understand range query traversal

### Testing & Debugging

- [ ] Know how to run test suite
- [ ] Can interpret `.btree` output
- [ ] Understand `.validate` checks
- [ ] Can identify common bug patterns
- [ ] Know 4 major bug fixes in codebase
- [ ] Can write new test cases
- [ ] Understand test runner architecture
- [ ] Can use validation to catch bugs

---

## 🚀 Advanced Topics (Beyond Current Scope)

Once you've mastered the basics, explore:

1. **Concurrency**
   - Reader-writer locks
   - Multi-version concurrency control (MVCC)
   - Optimistic locking

2. **Transactions**
   - Write-ahead logging (WAL)
   - ACID properties
   - Rollback mechanisms

3. **Optimizations**
   - Prefix compression
   - Page compression
   - Bulk loading
   - Index statistics

4. **Advanced Data Structures**
   - B+ Trees (all data in leaves)
   - Fractal Trees (buffered updates)
   - LSM Trees (write-optimized)

---

## 📖 Recommended Study Order

### For Complete Beginners (10-15 hours total)

```
Day 1: Topics 1-3 (Pointers, Structs, File I/O)
Day 2: Topics 4-6 (STL, Trees, LRU)
Day 3: Topics 7-9 (B-Tree ops, Serialization, Testing)
Day 4: Code Walkthrough Part 1
Day 5: Code Walkthrough Part 2
Day 6: Code Walkthrough Part 3
Day 7: Testing & Experimentation
```

### For Intermediate (5-7 hours total)

```
Session 1: Skim Topics 1-9 (focus on 5-7)
Session 2: Code Walkthrough Parts 1-2
Session 3: Code Walkthrough Part 3
Session 4: Testing & Experimentation
```

### For Advanced (2-3 hours total)

```
Session 1: Code Walkthrough Parts 1-3 (skim familiar parts)
Session 2: Source code exploration + testing
```

---

## 🛠️ Tools & Resources

### Required Tools
- C++ Compiler (MSVC/GCC/Clang)
- CMake (build system)
- Python 3.x (for tests)
- Text editor (VS Code recommended)

### Useful VS Code Extensions
- C/C++ Extension Pack
- CMake Tools
- Markdown Preview Enhanced

### Reference Documentation
- [SQLite B-Tree Documentation](https://www.sqlite.org/btreemodule.html)
- [Database Internals Book](https://www.databass.dev/)
- [CMU Database Course](https://15445.courses.cs.cmu.edu/)

---

## 💡 Study Tips

### Active Learning Strategies

1. **Code Annotation**
   - Print source code sections
   - Annotate with your own comments
   - Draw memory diagrams

2. **Visual Diagrams**
   - Draw tree transformations
   - Sketch cache states
   - Visualize page layouts

3. **Teach Back Method**
   - Explain concepts out loud
   - Write your own documentation
   - Create your own examples

4. **Deliberate Practice**
   - Modify code and predict behavior
   - Break things intentionally
   - Fix bugs you introduce

### Common Pitfalls to Avoid

❌ **Don't:** Skip foundational topics
✅ **Do:** Build understanding layer by layer

❌ **Don't:** Just read code passively
✅ **Do:** Trace execution with pen and paper

❌ **Don't:** Ignore test failures
✅ **Do:** Investigate every test failure deeply

❌ **Don't:** Move on without understanding
✅ **Do:** Spend extra time on confusing parts

---

## 📝 Progress Tracking

### Your Learning Log

Keep notes on:
- **Date & Duration:** Track time spent
- **Topics Covered:** What you studied
- **Key Insights:** "Aha!" moments
- **Questions:** Things you want to clarify
- **Experiments:** Code modifications tried

### Example Log Entry

```
Date: Nov 1, 2025
Duration: 90 minutes
Topics: LRU Cache implementation (Topic 6)
Insights:
  - Finally understand why we need THREE containers!
  - map for O(1) lookup, list for LRU order, set for dirty tracking
  - Each serves a distinct purpose
Questions:
  - Why not use unordered_map instead of map?
  - What happens if dirty set grows too large?
Experiments:
  - Changed cache size to 10, saw more evictions
  - Added print statements to trace cache hits/misses
```

---

## 🎓 Certificate of Mastery

Once you can answer these questions confidently, you've mastered ArborDB:

1. **Architecture:** Explain the 3-tier architecture and why each layer exists
2. **LRU Cache:** Draw the data structures and trace a cache eviction
3. **Split Operation:** Walk through a complete leaf split on whiteboard
4. **Merge Operation:** Explain when borrow vs. merge happens
5. **Validation:** List all 7 tree invariants checked by validate_tree()
6. **Bug Fixes:** Describe the 4 major bugs and their fixes
7. **Testing:** Explain how the test runner works and write a new test
8. **Persistence:** Trace a complete insert → dirty track → flush → reload cycle

---

## 🙏 Acknowledgments

This learning path was created to make database internals accessible to everyone, from beginners to experts. The ArborDB implementation is production-grade code suitable for:

- Learning database fundamentals
- Understanding B-Tree algorithms
- Studying systems programming in C++
- Preparing for database interviews
- Building your own storage engines

---

## 📧 Next Steps

After mastering ArborDB:

1. **Contribute:** Add new features (transactions, indexing, compression)
2. **Build:** Create your own storage engine from scratch
3. **Learn More:** Study SQLite, PostgreSQL, RocksDB source code
4. **Share:** Teach others what you've learned

---

**Remember:** Understanding comes from doing, not just reading. Run the code, break things, fix them, and experiment freely!

**Good luck on your journey to database mastery! 🚀**
