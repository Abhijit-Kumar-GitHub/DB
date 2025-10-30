#!/usr/bin/env python3
"""
Automated Test Suite for SkipListDB
Mimics pytest-style assertions for database validation
"""

import subprocess
import os
import sys
from typing import List, Tuple

class TestRunner:
    def __init__(self, db_exe="cmake-build-debug\\SkipListDB.exe"):
        self.db_exe = db_exe
        self.tests_passed = 0
        self.tests_failed = 0
        self.test_files = []
        
    def run_test(self, name: str, commands: List[str], expected_rows: int = None, 
                 should_validate: bool = True, max_height: int = None):
        """Run a single test case"""
        test_file = f"test_{name}.txt"
        db_file = f"test_{name}.db"
        
        # Write test commands
        with open(test_file, 'w') as f:
            for cmd in commands:
                f.write(cmd + '\n')
            if should_validate:
                f.write('.validate\n')
            if expected_rows is not None:
                f.write('select\n')
            f.write('.exit\n')
        
        self.test_files.append(test_file)
        
        # Remove old db
        if os.path.exists(db_file):
            os.remove(db_file)
        
        # Run test
        try:
            with open(test_file, 'r') as f:
                result = subprocess.run(
                    [self.db_exe, db_file],
                    stdin=f,
                    capture_output=True,
                    text=True,
                    timeout=10
                )
            
            output = result.stdout
            
            # Assertions
            passed = True
            errors = []
            
            if should_validate:
                if "Tree structure is valid!" not in output:
                    passed = False
                    errors.append("❌ Tree validation failed")
                if "Tree validation FAILED" in output:
                    passed = False
                    errors.append("❌ Tree has structural errors")
            
            if expected_rows is not None:
                if f"Total rows: {expected_rows}" not in output:
                    passed = False
                    errors.append(f"❌ Expected {expected_rows} rows, got different count")
            
            if max_height is not None:
                if f"Depth: " in output:
                    import re
                    match = re.search(r'Depth: (\d+)', output)
                    if match:
                        actual_height = int(match.group(1))
                        if actual_height > max_height:
                            passed = False
                            errors.append(f"❌ Tree height {actual_height} exceeds max {max_height}")
            
            if "Error:" in output and "Error reading input" not in output:
                passed = False
                errors.append(f"❌ Runtime error detected")
            
            if passed:
                self.tests_passed += 1
                print(f"✓ {name:40s} PASSED")
            else:
                self.tests_failed += 1
                print(f"✗ {name:40s} FAILED")
                for error in errors:
                    print(f"  {error}")
                # Print last 20 lines of output for debugging
                lines = output.split('\n')
                print("  Last output:")
                for line in lines[-20:]:
                    if line.strip():
                        print(f"    {line}")
        
        except subprocess.TimeoutExpired:
            self.tests_failed += 1
            print(f"✗ {name:40s} TIMEOUT")
        except Exception as e:
            self.tests_failed += 1
            print(f"✗ {name:40s} ERROR: {e}")
    
    def cleanup(self):
        """Remove test files"""
        for f in self.test_files:
            if os.path.exists(f):
                os.remove(f)
            db_file = f.replace('.txt', '.db')
            if os.path.exists(db_file):
                os.remove(db_file)
    
    def print_summary(self):
        """Print test summary"""
        total = self.tests_passed + self.tests_failed
        percentage = (self.tests_passed / total * 100) if total > 0 else 0
        
        print("\n" + "="*60)
        print(f"Test Results: {self.tests_passed}/{total} passed ({percentage:.1f}%)")
        print("="*60)
        
        if self.tests_failed == 0:
            print("🎉 All tests passed!")
            return 0
        else:
            print(f"⚠️  {self.tests_failed} test(s) failed")
            return 1


def main():
    runner = TestRunner()
    
    print("="*60)
    print("SkipListDB Automated Test Suite")
    print("="*60 + "\n")
    
    # Test 1: Basic insert and select
    runner.run_test(
        "basic_insert",
        ["insert 1 user1 email1@test.com",
         "insert 2 user2 email2@test.com",
         "insert 3 user3 email3@test.com"],
        expected_rows=3,
        max_height=0
    )
    
    # Test 2: Delete operation
    runner.run_test(
        "basic_delete",
        ["insert 1 user1 email1@test.com",
         "insert 2 user2 email2@test.com",
         "insert 3 user3 email3@test.com",
         "delete 2"],
        expected_rows=2
    )
    
    # Test 3: Update operation
    runner.run_test(
        "basic_update",
        ["insert 1 user1 email1@test.com",
         "insert 2 user2 email2@test.com",
         "update 1 newuser newemail@test.com"],
        expected_rows=2
    )
    
    # Test 4: Leaf node split (trigger at 14 insertions)
    commands = [f"insert {i} user{i} email{i}@test.com" for i in range(1, 16)]
    runner.run_test(
        "leaf_split",
        commands,
        expected_rows=15,
        max_height=1
    )
    
    # Test 5: Leaf node borrowing
    commands = ([f"insert {i} user{i} email{i}@test.com" for i in range(1, 16)] +
                ["delete 8"])
    runner.run_test(
        "leaf_borrow",
        commands,
        expected_rows=14,
        max_height=1
    )
    
    # Test 6: Leaf node merge
    commands = ([f"insert {i} user{i} email{i}@test.com" for i in range(1, 16)] +
                [f"delete {i}" for i in range(8, 13)])
    runner.run_test(
        "leaf_merge",
        commands,
        expected_rows=10,
        max_height=1
    )
    
    # Test 7: Large dataset (should maintain reasonable height)
    commands = [f"insert {i} user{i} email{i}@test.com" for i in range(1, 101)]
    runner.run_test(
        "large_insert_100",
        commands,
        expected_rows=100,
        max_height=3  # log_7(100) ≈ 2.4, so 3 is reasonable
    )
    
    # Test 8: Heavy deletes on medium dataset
    commands = ([f"insert {i} user{i} email{i}@test.com" for i in range(1, 21)] +
                [f"delete {i}" for i in range(8, 15)])
    runner.run_test(
        "medium_cascade_delete",
        commands,
        expected_rows=13,
        max_height=2
    )
    
    # Test 9: Range query
    commands = ([f"insert {i} user{i} email{i}@test.com" for i in range(1, 21)] +
                ["range 5 10"])
    runner.run_test(
        "range_query",
        commands,
        should_validate=True
    )
    
    # Test 10: Find operation
    commands = ([f"insert {i} user{i} email{i}@test.com" for i in range(1, 11)] +
                ["find 5"])
    runner.run_test(
        "find_existing",
        commands,
        should_validate=True
    )
    
    # Test 11: Alternating insert/delete
    commands = []
    for i in range(1, 21):
        commands.append(f"insert {i} user{i} email{i}@test.com")
        if i % 3 == 0:
            commands.append(f"delete {i-1}")
    runner.run_test(
        "alternating_ops",
        commands,
        expected_rows=14,  # 20 inserts - 6 deletes
        max_height=2
    )
    
    # Test 12: Persistence (insert, close, reopen, select)
    commands_phase1 = [f"insert {i} user{i} email{i}@test.com" for i in range(1, 11)]
    test_file = "test_persist.txt"
    db_file = "test_persist.db"
    
    # Phase 1: Insert and close
    with open(test_file, 'w') as f:
        for cmd in commands_phase1:
            f.write(cmd + '\n')
        f.write('.exit\n')
    
    with open(test_file, 'r') as f:
        subprocess.run([runner.db_exe, db_file], stdin=f, capture_output=True, timeout=5)
    
    # Phase 2: Reopen and verify
    with open(test_file, 'w') as f:
        f.write('select\n')
        f.write('.validate\n')
        f.write('.exit\n')
    
    with open(test_file, 'r') as f:
        result = subprocess.run([runner.db_exe, db_file], stdin=f, capture_output=True, text=True, timeout=5)
    
    if "Total rows: 10" in result.stdout and "Tree structure is valid!" in result.stdout:
        runner.tests_passed += 1
        print(f"✓ {'persistence':40s} PASSED")
    else:
        runner.tests_failed += 1
        print(f"✗ {'persistence':40s} FAILED")
    
    runner.test_files.extend([test_file, db_file])
    
    # Cleanup
    runner.cleanup()
    
    # Summary
    exit_code = runner.print_summary()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
