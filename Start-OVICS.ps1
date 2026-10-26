$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$qemu = Get-Command 'qemu-system-i386' -ErrorAction Stop
if (-not (Test-Path 'ovics.elf')) { throw 'ovics.elf missing. Build OVICS first.' }
& $qemu.Source -vga std -m 128 -kernel ovics.elf -netdev 'user,id=net,net=10.0.2.0/24,host=10.0.2.2,dns=10.0.2.3' -device 'rtl8139,netdev=net'
if ($LASTEXITCODE -ne 0) { throw "QEMU exited: $LASTEXITCODE" }
