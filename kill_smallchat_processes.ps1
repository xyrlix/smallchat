# Simple SmallChat process cleanup script

echo "Starting SmallChat process cleanup..."

# Try to kill all smallchat-related processes
try {
    $processes = Get-Process | Where-Object { $_.ProcessName -like "smallchat*" }
    foreach ($process in $processes) {
        echo "Killing process: $($process.ProcessName) (PID: $($process.Id))"
        $process.Kill()
    }
    echo "Process cleanup completed"
} catch {
    echo "Error cleaning processes: $($_.Exception.Message)"
}

echo "Cleanup completed!"
