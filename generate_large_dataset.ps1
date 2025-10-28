# PowerShell script to generate 10,000 insert commands
# Usage: .\generate_large_dataset.ps1 | .\cmake-build-debug\SkipListDB.exe test.db

param(
    [int]$numRows = 10000,
    [int]$startId = 1
)

# Generate insert commands
for ($i = $startId; $i -le ($startId + $numRows - 1); $i++) {
    Write-Output "insert $i user$i user$i@example.com"
}

# Add a select command at the end to verify
Write-Output "select"
Write-Output ".exit"
