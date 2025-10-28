# PowerShell script to generate insert commands in RANDOM order
# Usage: .\generate_random_dataset.ps1 -numRows 10000 | .\cmake-build-debug\SkipListDB.exe test_random.db

param(
    [int]$numRows = 10000
)

# Generate array of IDs
$ids = 1..$numRows

# Shuffle the IDs using Fisher-Yates algorithm
for ($i = $ids.Count - 1; $i -gt 0; $i--) {
    $j = Get-Random -Minimum 0 -Maximum ($i + 1)
    $temp = $ids[$i]
    $ids[$i] = $ids[$j]
    $ids[$j] = $temp
}

# Generate insert commands in random order
foreach ($id in $ids) {
    Write-Output "insert $id user$id user$id@example.com"
}

# Add a select command at the end to verify
Write-Output "select"
Write-Output ".exit"
