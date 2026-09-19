$action = New-ScheduledTaskAction -Execute 'C:\Users\honco\OneDrive\Bureau\fallaga\build\Release\RDR2_Prototype.exe' -WorkingDirectory 'C:\Users\honco\OneDrive\Bureau\fallaga'
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Set-ScheduledTask -TaskName 'LaunchRDR2' -Action $action -Settings $settings
schtasks /run /tn "LaunchRDR2"
Start-Sleep -Seconds 2
Get-Process -Name RDR2_Prototype -ErrorAction SilentlyContinue | Select-Object Id, ProcessName, Responding, WorkingSet64
