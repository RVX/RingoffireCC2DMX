# Flash ringoffire_CC2DMX to a connected Teensy 4.1
# Reliable two-step flash (arduino-cli upload alone is unreliable on Teensy).
# Run from the repo root:  .\flash.ps1

$ErrorActionPreference = 'Stop'
$sketch = "ringoffire_CC2DMX"
$fqbn   = "teensy:avr:teensy41:usb=serialmidi,speed=600,opt=o2std,keys=en-us"

Write-Host "== Compiling ==" -ForegroundColor Cyan
cmd /c "arduino-cli compile --fqbn `"$fqbn`" `"$sketch`" 2>&1"
if ($LASTEXITCODE -ne 0) { Write-Host "COMPILE FAILED" -ForegroundColor Red; exit 1 }

$TOOLS = "$env:LOCALAPPDATA\Arduino15\packages\teensy\tools\teensy-tools\1.61.0"
if (-not (Test-Path "$TOOLS\teensy_reboot.exe")) {
  $TOOLS = Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\teensy\tools\teensy-tools" -Directory |
           Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
}
$BUILD = Get-ChildItem "$env:LOCALAPPDATA\arduino\sketches" -Directory |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName

Write-Host "== Staging hex into Teensy Loader ==" -ForegroundColor Cyan
cmd /c "`"$TOOLS\teensy_post_compile.exe`" -file=$sketch.ino -path=`"$BUILD`" -tools=`"$TOOLS`" -board=TEENSY41 2>nul"
if ($LASTEXITCODE -ne 0) { Write-Host "STAGE FAILED" -ForegroundColor Red; exit 1 }

Write-Host "== Rebooting Teensy into new firmware ==" -ForegroundColor Cyan
cmd /c "`"$TOOLS\teensy_reboot.exe`" 2>nul"
if ($LASTEXITCODE -ne 0) { Write-Host "REBOOT FAILED (press the Teensy button and re-run)" -ForegroundColor Red; exit 1 }

Write-Host "FLASHED OK" -ForegroundColor Green
