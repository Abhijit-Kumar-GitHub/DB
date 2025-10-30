#!/usr/bin/env python3
"""
Freelist Test - Simplified
Tests basic freelist functionality
"""

import subprocess
import os
import sys

def run_db_commands(commands, db_file):
    """Run commands and return output"""
    try:
        process = subprocess.Popen(
            ['cmake-build-debug/SkipListDB.exe', db_file],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            errors='replace'  # Handle unicode errors
        )
        output, errors = process.communicate(input=commands, timeout=10)
        return output + errors
    except subprocess.TimeoutExpired:
        process.kill()
        return "TIMEOUT"

def test_freelist_basic():
    """Test that freelist doesn't crash with simple operations"""
    print("\nTesting freelist with simple operations...")
    
    db_file = "test_freelist_basic.db"
    if os.path.exists(db_file):
        os.remove(db_file)
    
    # Simple test: insert, delete, insert again
    commands = ""
    for i in range(1, 6):
        commands += f"insert {i} user{i} user{i}@example.com\n"
    commands += "delete 3\n"
    commands += "insert 10 user10 user10@example.com\n"
    commands += ".validate\n.exit\n"
    
    output = run_db_commands(commands, db_file)
    
    # Check for validation success
    has_validation = ("Tree is valid" in output or "valid!" in output)
    no_crashes = ("TIMEOUT" not in output)
    
    if has_validation and no_crashes:
        print("  ✅ PASSED: Basic operations completed successfully")
        return True
    else:
        print("  ❌ FAILED: Issues detected")
        if "TIMEOUT" in output:
            print("  - Process timed out")
        if not has_validation:
            print("  - Validation failed")
            # Show first error line
            for line in output.split('\n'):
                if 'ERROR' in line or 'Error' in line:
                    print(f"  - {line.strip()}")
                    break
        return False

def test_freelist_medium():
    """Test freelist with medium workload"""
    print("\nTesting freelist with medium workload...")
    
    db_file = "test_freelist_medium.db"
    if os.path.exists(db_file):
        os.remove(db_file)
    
    # Insert 20, delete some, insert more
    commands = ""
    for i in range(1, 21):
        commands += f"insert {i} user{i} user{i}@example.com\n"
    
    # Delete a few in the middle
    for i in [5, 10, 15]:
        commands += f"delete {i}\n"
    
    # Insert new ones
    for i in range(30, 33):
        commands += f"insert {i} user{i} user{i}@example.com\n"
    
    commands += ".validate\n.exit\n"
    
    output = run_db_commands(commands, db_file)
    
    has_validation = ("Tree is valid" in output or "valid!" in output)
    no_crashes = ("TIMEOUT" not in output)
    
    if has_validation and no_crashes:
        print("  ✅ PASSED: Medium workload completed successfully")
        return True
    else:
        print("  ❌ FAILED: Issues detected")
        if "TIMEOUT" in output:
            print("  - Process timed out")
        if not has_validation:
            print("  - Validation failed")
            for line in output.split('\n'):
                if 'ERROR' in line or 'Error' in line:
                    print(f"  - {line.strip()}")
                    break
        return False

def test_freelist_page_reuse():
    """Test that pages are actually being reused"""
    print("\nTesting freelist page reuse...")
    
    db_file = "test_freelist_reuse.db"
    if os.path.exists(db_file):
        os.remove(db_file)
    
    # Phase 1: Insert 50 records to create multiple pages
    commands1 = ""
    for i in range(1, 51):
        commands1 += f"insert {i} user{i} user{i}@example.com\n"
    commands1 += ".btree\n.exit\n"
    
    output1 = run_db_commands(commands1, db_file)
    
    # Extract page count before deletes
    pages_before = set()
    for line in output1.split('\n'):
        if '(page' in line.lower():
            # Look for patterns like "(page 3,"
            import re
            matches = re.findall(r'\(page (\d+)', line)
            for match in matches:
                pages_before.add(int(match))
    
    max_page_before = max(pages_before) if pages_before else 0
    print(f"  Before deletes: max page #{max_page_before} ({len(pages_before)} pages total)")
    
    # Phase 2: Delete 20 records (should free some pages)
    commands2 = ""
    for i in range(15, 35):
        commands2 += f"delete {i}\n"
    commands2 += ".exit\n"
    
    output2 = run_db_commands(commands2, db_file)
    
    # Phase 3: Insert 15 new records (should reuse freed pages)
    commands3 = ""
    for i in range(60, 75):
        commands3 += f"insert {i} user{i} user{i}@example.com\n"
    commands3 += ".btree\n.exit\n"
    
    output3 = run_db_commands(commands3, db_file)
    
    # Extract page count after re-inserts
    pages_after = set()
    for line in output3.split('\n'):
        if '(page' in line.lower():
            import re
            matches = re.findall(r'\(page (\d+)', line)
            for match in matches:
                pages_after.add(int(match))
    
    max_page_after = max(pages_after) if pages_after else 0
    print(f"  After re-inserts: max page #{max_page_after} ({len(pages_after)} pages total)")
    
    # If freelist works, max_page_after should be close to max_page_before
    # Without freelist, it would be much higher
    pages_allocated = max_page_after - max_page_before
    
    if pages_allocated <= 5:
        print(f"  ✅ PASSED: Only {pages_allocated} new pages allocated (freelist working!)")
        return True
    else:
        print(f"  ❌ FAILED: {pages_allocated} new pages allocated (freelist may not be working)")
        print(f"  Pages before: {sorted(pages_before)}")
        print(f"  Pages after: {sorted(pages_after)}")
        return False

if __name__ == "__main__":
    print("=" * 60)
    print("Freelist Test Suite")
    print("=" * 60)
    print()
    
    results = []
    results.append(("Basic Operations", test_freelist_basic()))
    results.append(("Medium Workload", test_freelist_medium()))
    results.append(("Page Reuse", test_freelist_page_reuse()))
    
    print()
    print("=" * 60)
    passed = sum(1 for _, result in results if result)
    total = len(results)
    print(f"Results: {passed}/{total} tests passed")
    print("=" * 60)
    
    if passed < total:
        print("⚠️  Some tests failed")
        sys.exit(1)
    else:
        print("🎉 All tests passed!")
        sys.exit(0)
