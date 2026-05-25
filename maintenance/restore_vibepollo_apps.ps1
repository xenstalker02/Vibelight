# restore_vibepollo_apps.ps1
# Requires admin. Stops Vibepollo, writes restored apps.json, restarts Vibepollo.

$src  = "C:\temp\vibepollo_apps_new.json"
$dest = "C:\Program Files\Vibepollo\config\apps.json"

Write-Host "Stopping Vibepollo scheduled task..."
Stop-ScheduledTask -TaskName "Vibepollo" -ErrorAction SilentlyContinue

# Give sunshine.exe a moment to exit cleanly
Start-Sleep -Seconds 2
Stop-Process -Name "sunshine" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

Write-Host "Writing restored apps.json ($((Get-Content $src | ConvertFrom-Json).apps.Count) apps)..."
Copy-Item -Path $src -Destination $dest -Force

Write-Host "Starting Vibepollo..."
Start-ScheduledTask -TaskName "Vibepollo"

Start-Sleep -Seconds 3

$proc = Get-Process -Name "sunshine" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "Vibepollo restarted OK (PID $($proc.Id))"
} else {
    Write-Host "WARNING: sunshine.exe not running after restart. Check task scheduler."
}

Write-Host "Done. Refresh Vibelight to see restored apps."
