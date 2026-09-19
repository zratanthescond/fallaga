Add-Type -AssemblyName System.Windows.Forms

# Start the game
$process = Start-Process -FilePath ".\build\Release\RDR2_Prototype.exe" -RedirectStandardOutput "c:\Users\honco\OneDrive\Bureau\fallaga\game_stdout.txt" -RedirectStandardError "c:\Users\honco\OneDrive\Bureau\fallaga\game_stderr.txt" -PassThru

Write-Host "Waiting for the game to load assets..."
Start-Sleep -Seconds 10

# Bring the window to the front
$wshell = New-Object -ComObject Wscript.Shell
$wshell.AppActivate("RDR2 Prototype")
Start-Sleep -Seconds 2

# Toggle Noclip by pressing 'n'
Write-Host "Toggling noclip mode..."
$wshell.SendKeys("n")
Start-Sleep -Seconds 2

# Fly upwards by holding Space
Write-Host "Simulating flying upwards..."
$wshell.SendKeys(" ")
Start-Sleep -Seconds 3

# Fly forward in noclip by pressing 'z'
Write-Host "Simulating moving forward..."
$wshell.SendKeys("z")
Start-Sleep -Seconds 3

# Toggle Noclip OFF (Press N again)
Write-Host "Toggling noclip mode OFF..."
$wshell.SendKeys("n")
Start-Sleep -Seconds 2

# Simulate jump (Press Space)
Write-Host "Simulating jumping..."
$wshell.SendKeys(" ")
Start-Sleep -Seconds 2

# Stop the game process
Stop-Process -Id $process.Id -Force
Write-Host "Process stopped."
