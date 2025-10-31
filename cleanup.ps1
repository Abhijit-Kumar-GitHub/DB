# SkipListDB Project Cleanup Script
# Removes development artifacts and test files for clean presentation

Write-Host "🧹 Starting Project Cleanup..." -ForegroundColor Cyan
Write-Host ""

# Count files before cleanup
$filesBefore = (Get-ChildItem -File -Recurse -Exclude .git | Measure-Object).Count

# 1. Remove all test database files
Write-Host "🗑️  Removing test database files..." -ForegroundColor Yellow
$dbFiles = Get-Item *.db -ErrorAction SilentlyContinue
if ($dbFiles) {
    Remove-Item *.db -Force
    Write-Host "   ✓ Removed $($dbFiles.Count) .db files" -ForegroundColor Green
} else {
    Write-Host "   ✓ No .db files found" -ForegroundColor Gray
}

# 2. Remove test command files
Write-Host "🗑️  Removing test command files..." -ForegroundColor Yellow
$txtFiles = @(
    "test_*.txt",
    "manual_test_commands.txt"
)
$removedTxt = 0
foreach ($pattern in $txtFiles) {
    $files = Get-Item $pattern -ErrorAction SilentlyContinue
    if ($files) {
        Remove-Item $pattern -Force
        $removedTxt += ($files | Measure-Object).Count
    }
}
if ($removedTxt -gt 0) {
    Write-Host "   ✓ Removed $removedTxt .txt test files" -ForegroundColor Green
} else {
    Write-Host "   ✓ No .txt test files found" -ForegroundColor Gray
}

# 3. Remove development markdown files
Write-Host "🗑️  Removing development notes..." -ForegroundColor Yellow
$devDocs = @(
    "BUG_FIX_COMPLETE.md",
    "DOCUMENTATION_SUMMARY.md",
    "EDGE_CASE_ANALYSIS.md",
    "EDGE_CASE_SUMMARY.md",
    "FREELIST_IMPLEMENTATION.md"
)
$removedDocs = 0
foreach ($doc in $devDocs) {
    if (Test-Path $doc) {
        Remove-Item $doc -Force
        $removedDocs++
    }
}
if ($removedDocs -gt 0) {
    Write-Host "   ✓ Removed $removedDocs development .md files" -ForegroundColor Green
} else {
    Write-Host "   ✓ No development notes found" -ForegroundColor Gray
}

# 4. Remove check utilities
Write-Host "🗑️  Removing testing utilities..." -ForegroundColor Yellow
$utilities = @(
    "check_constants.cpp",
    "check_constants.exe"
)
$removedUtils = 0
foreach ($util in $utilities) {
    if (Test-Path $util) {
        Remove-Item $util -Force
        $removedUtils++
    }
}
if ($removedUtils -gt 0) {
    Write-Host "   ✓ Removed $removedUtils utility files" -ForegroundColor Green
} else {
    Write-Host "   ✓ No utility files found" -ForegroundColor Gray
}

# 5. Optional: Clean build artifacts (commented out by default)
# Uncomment if you want to clean and rebuild
# Write-Host "🗑️  Cleaning build artifacts..." -ForegroundColor Yellow
# if (Test-Path "cmake-build-debug") {
#     Remove-Item "cmake-build-debug" -Recurse -Force
#     Write-Host "   ✓ Removed cmake-build-debug/" -ForegroundColor Green
# }

Write-Host ""
Write-Host "✅ Cleanup Complete!" -ForegroundColor Green
Write-Host ""

# Count files after cleanup
$filesAfter = (Get-ChildItem -File -Recurse -Exclude .git | Measure-Object).Count
$filesRemoved = $filesBefore - $filesAfter

Write-Host "📊 Summary:" -ForegroundColor Cyan
Write-Host "   Files before: $filesBefore" -ForegroundColor White
Write-Host "   Files after:  $filesAfter" -ForegroundColor White
Write-Host "   Removed:      $filesRemoved files" -ForegroundColor Yellow
Write-Host ""

# Show remaining essential files
Write-Host "📁 Essential files remaining:" -ForegroundColor Cyan
$essentialFiles = @(
    "main.cpp",
    "db.hpp",
    "CMakeLists.txt",
    "README.md",
    "README_SUMMARY.md",
    "TECHNICAL_REPORT.md",
    "run_tests.py",
    "test_freelist.py"
)

foreach ($file in $essentialFiles) {
    if (Test-Path $file) {
        $size = (Get-Item $file).Length
        $sizeKB = [math]::Round($size / 1KB, 1)
        Write-Host "   checkmark $file (${sizeKB} KB)" -ForegroundColor Green
    } else {
        Write-Host "   warning $file (missing)" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "🎯 Your project is now ready for presentation!" -ForegroundColor Cyan
Write-Host ""
