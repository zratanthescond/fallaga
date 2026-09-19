Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# Create artifacts folder if it doesn't exist
$artifactsPath = "C:\Users\honco\.gemini\antigravity\brain\96f9229c-bc81-4b23-9d3a-54f470b9db27"
if (-not (Test-Path $artifactsPath)) {
    New-Item -ItemType Directory -Path $artifactsPath -Force
}

# Start the game
$process = Start-Process -FilePath ".\build\Release\RDR2_Prototype.exe" -PassThru

# Wait 15 seconds for assets to load
Write-Host "Waiting for the game to load assets..."
Start-Sleep -Seconds 15

# Bring the window to the front
$wshell = New-Object -ComObject Wscript.Shell
$wshell.AppActivate("RDR2 Prototype")
Start-Sleep -Seconds 2

# Helper function to take screenshot
function Take-Screenshot($name) {
    $Screen = [System.Windows.Forms.Screen]::PrimaryScreen
    $Width   = $Screen.Bounds.Width
    $Height  = $Screen.Bounds.Height
    $Left    = $Screen.Bounds.Left
    $Top     = $Screen.Bounds.Top
    $Bitmap  = New-Object System.Drawing.Bitmap $Width, $Height
    $Graphic = [System.Drawing.Graphics]::FromImage($Bitmap)
    $Graphic.CopyFromScreen($Left, $Top, 0, 0, $Bitmap.Size)
    $Bitmap.Save("$artifactsPath\$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $Graphic.Dispose()
    $Bitmap.Dispose()
    Write-Host "Screenshot saved as $name.png"
}

# 1. Take screenshot of Idle pose
Take-Screenshot "pose_idle"

# 2. Walk forward by pressing W/Z
Write-Host "Simulating walking..."
$wshell.SendKeys("z")
Start-Sleep -Seconds 2
Take-Screenshot "pose_walk"

# 3. Toggle Aiming by pressing V
Write-Host "Simulating aiming..."
$wshell.SendKeys("v")
Start-Sleep -Seconds 2
Take-Screenshot "pose_aim"

# Stop the game process
Stop-Process -Id $process.Id -Force
Write-Host "Process stopped."
