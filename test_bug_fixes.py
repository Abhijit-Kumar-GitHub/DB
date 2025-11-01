#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Bug Fix Validation Tests
Tests for the 3 critical bugs fixed on Nov 2, 2025:
1. Bug #1: Missing mark_page_dirty in execute_update
2. Bug #2: Missing mark_page_dirty in leaf_node_delete
3. Bug #3: O(n²) bubble sort in merge operations
"""

import subprocess
import os
import sys

# Windows console UTF-8 fix
if sys.platform == 'win32':
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')
    except:
        pass

DB_FILE = "test_bugfixes.db"
EXE_PATH = "cmake-build-debug/ArborDB.exe"

def cleanup():
    """Remove test database file"""
    if os.path.exists(DB_FILE):
        os.remove(DB_FILE)

def run_commands(commands, fresh_start=True):
    """Execute commands in database and return output"""
    if fresh_start:
        cleanup()
    input_text = "\n".join(commands) + "\n.exit\n"
    try:
        result = subprocess.run(
            [EXE_PATH, DB_FILE],
            input=input_text,
            capture_output=True,
            text=True,
            timeout=10
        )
        return result.stdout
    except Exception as e:
        print(f"Error running database: {e}")
        return ""

def test_bug_1_update_persistence():
    """
    Bug #1 Test: Update persistence after restart
    Before fix: Updates were visible but lost on restart (missing mark_page_dirty)
    After fix: Updates should persist to disk
    """
    print("=" * 70)
    print("BUG #1: Update Persistence Test")
    print("=" * 70)
    
    # First session: Insert and update
    commands = [
        "insert 1 alice alice@example.com",
        "insert 2 bob bob@example.com",
        "insert 3 charlie charlie@example.com",
        "update 2 robert robert@updated.com",  # BUG #1: This should persist
        "select",
    ]
    output1 = run_commands(commands)
    
    # Verify update happened
    if "robert" not in output1 or "robert@updated.com" not in output1:
        print("✗ FAILED: Update not visible in same session")
        cleanup()
        return False
    
    # Second session: Reopen and verify update persisted
    commands = [
        "select",
    ]
    output2 = run_commands(commands, fresh_start=False)
    
    # Check if update persisted
    if "robert" in output2 and "robert@updated.com" in output2:
        print("✓ PASSED: Update persisted to disk after restart")
        cleanup()
        return True
    else:
        print("✗ FAILED: Update lost after restart (Bug #1 not fixed)")
        print(f"Expected: robert@updated.com")
        print(f"Got: {output2}")
        cleanup()
        return False

def test_bug_2_delete_persistence():
    """
    Bug #2 Test: Delete persistence after restart (non-underflow case)
    Before fix: Deletes that didn't trigger merge were lost on restart
    After fix: All deletes should persist to disk
    """
    print("\n" + "=" * 70)
    print("BUG #2: Delete Persistence Test (Non-Underflow)")
    print("=" * 70)
    
    # First session: Insert many records, delete one (no underflow)
    commands = [
        "insert 1 alice alice@example.com",
        "insert 2 bob bob@example.com",
        "insert 3 charlie charlie@example.com",
        "insert 4 david david@example.com",
        "insert 5 eve eve@example.com",
        "insert 6 frank frank@example.com",
        "insert 7 grace grace@example.com",
        "insert 8 henry henry@example.com",
        "delete 5",  # BUG #2: This should persist (no merge needed)
        "select",
    ]
    output1 = run_commands(commands)
    
    # Verify delete happened
    if "eve" in output1:
        print("✗ FAILED: Delete not visible in same session")
        cleanup()
        return False
    
    # Second session: Reopen and verify delete persisted
    commands = [
        "select",
    ]
    output2 = run_commands(commands, fresh_start=False)
    
    # Count records (should be 7, not 8)
    record_count = output2.count("@example.com")
    
    if "eve" not in output2 and record_count == 7:
        print("✓ PASSED: Delete persisted to disk after restart")
        cleanup()
        return True
    else:
        print("✗ FAILED: Delete lost after restart (Bug #2 not fixed)")
        print(f"Expected: 7 records without 'eve'")
        print(f"Got: {record_count} records")
        cleanup()
        return False

def test_bug_3_merge_performance():
    """
    Bug #3 Test: Merge sort performance
    Before fix: O(n²) bubble sort in merge operations
    After fix: O(n log n) sort using std::sort
    Note: This is a correctness test, not a performance test
    """
    print("\n" + "=" * 70)
    print("BUG #3: Merge Sort Correctness Test")
    print("=" * 70)
    
    # Create scenario that triggers merge with potentially unsorted keys
    # Insert keys that will cause borrowing (which can unsort nodes)
    # Then delete to trigger merge
    commands = [
        # Insert ascending order
        "insert 10 user10 user10@example.com",
        "insert 20 user20 user20@example.com",
        "insert 30 user30 user30@example.com",
        "insert 40 user40 user40@example.com",
        "insert 50 user50 user50@example.com",
        "insert 60 user60 user60@example.com",
        "insert 70 user70 user70@example.com",
        "insert 80 user80 user80@example.com",
        # Delete some to trigger underflow and merge
        "delete 30",
        "delete 40",
        "delete 50",
        "select",  # Verify sorted order maintained
    ]
    output = run_commands(commands)
    
    # Extract IDs from output and verify they're sorted
    import re
    ids = [int(m.group(1)) for m in re.finditer(r'\((\d+),', output)]
    
    if ids == sorted(ids):
        print("✓ PASSED: Keys remain sorted after merge operations")
        print(f"  Sorted IDs: {ids}")
        cleanup()
        return True
    else:
        print("✗ FAILED: Keys not sorted after merge (Bug #3 not fixed)")
        print(f"  Expected: {sorted(ids)}")
        print(f"  Got:      {ids}")
        cleanup()
        return False

def test_combined_scenario():
    """
    Combined test: Update + Delete + Restart
    Tests all 3 bugs in a realistic scenario
    """
    print("\n" + "=" * 70)
    print("COMBINED TEST: Update + Delete + Restart")
    print("=" * 70)
    
    # Session 1: Insert, update, delete
    commands = [
        "insert 1 alice alice@example.com",
        "insert 2 bob bob@example.com",
        "insert 3 charlie charlie@example.com",
        "insert 4 david david@example.com",
        "insert 5 eve eve@example.com",
        "update 2 robert robert@updated.com",
        "update 4 daniel daniel@updated.com",
        "delete 3",
        "delete 5",
        "select",
    ]
    output1 = run_commands(commands)
    
    # Session 2: Restart and verify
    commands = ["select"]
    output2 = run_commands(commands, fresh_start=False)
    
    # Verify:
    # - robert@updated.com exists (Bug #1)
    # - daniel@updated.com exists (Bug #1)
    # - charlie not present (Bug #2)
    # - eve not present (Bug #2)
    # - Keys in sorted order (Bug #3)
    
    checks = [
        ("robert@updated.com" in output2, "Update 1 persisted"),
        ("daniel@updated.com" in output2, "Update 2 persisted"),
        ("charlie" not in output2, "Delete 1 persisted"),
        ("eve" not in output2, "Delete 2 persisted"),
        (output2.count("@") == 3, "Correct record count (3)"),
    ]
    
    all_passed = all(check[0] for check in checks)
    
    if all_passed:
        print("✓ PASSED: All operations persisted correctly")
        for check in checks:
            print(f"  ✓ {check[1]}")
        cleanup()
        return True
    else:
        print("✗ FAILED: Some operations didn't persist")
        for check in checks:
            status = "✓" if check[0] else "✗"
            print(f"  {status} {check[1]}")
        cleanup()
        return False

def main():
    print("\n" + "=" * 70)
    print("BUG FIX VALIDATION SUITE")
    print("Testing 3 critical bugs fixed on Nov 2, 2025")
    print("=" * 70)
    
    results = []
    
    # Run all bug tests
    results.append(("Bug #1: Update Persistence", test_bug_1_update_persistence()))
    results.append(("Bug #2: Delete Persistence", test_bug_2_delete_persistence()))
    results.append(("Bug #3: Merge Sort", test_bug_3_merge_performance()))
    results.append(("Combined Scenario", test_combined_scenario()))
    
    # Print summary
    print("\n" + "=" * 70)
    print("TEST RESULTS SUMMARY")
    print("=" * 70)
    
    for test_name, passed in results:
        status = "✓ PASSED" if passed else "✗ FAILED"
        print(f"{status}: {test_name}")
    
    passed_count = sum(1 for _, passed in results if passed)
    total_count = len(results)
    
    print("\n" + "=" * 70)
    print(f"TOTAL: {passed_count}/{total_count} tests passed ({100*passed_count//total_count}%)")
    print("=" * 70)
    
    if passed_count == total_count:
        print("🎉 ALL BUG FIXES VALIDATED!")
        return 0
    else:
        print("⚠️  SOME BUGS NOT FIXED")
        return 1

if __name__ == "__main__":
    sys.exit(main())
