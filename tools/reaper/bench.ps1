<#
G1-Emu's VST3 in REAPER on Windows, end to end, without touching the real G1-Emu memory.

    pwsh tools\reaper\bench.ps1 [-Patch "test-patches\Test MIDI.pch"] [-Work <dir>]

1. A work directory (default %TEMP%\g1-reaper-bench) stands in for APPDATA: a copy of REAPER's own
   settings (audio device, plugin scan) and a G1-Emu memory of its own, with the ROM of the real
   settings. G1-Emu takes APPDATA as its data directory, so the real flash.bin is never read nor
   written.
2. "new": REAPER starts with an empty project; upload.py, standing in for Animatek NME, finds the
   plugin on the direct link (skipping a standalone that may hold the first port) and uploads the
   patch into slot A; REAPER plays a MIDI item and saves the project.
3. "reopen": REAPER opens that project and plays it again with no upload: slot A must come back
   with the project.

Each phase prints G1-Emu's log lines. The verdict is the health line: Test MIDI.pch gives about
-53.8 dBFS (as `g1patchtest` measures it), "silent" means no sound. Needs the installed plugin
(C:\Program Files\Common Files\VST3), REAPER, Python, and a built g1patchtest for the packets.
REAPER's anticipative FX processing is on by default: the log says so ("processes ahead").
#>
param(
    [string]$Patch = "test-patches\Test MIDI.pch",
    [string]$Work = "$env:TEMP\g1-reaper-bench",
    [string]$Reaper = "C:\Program Files\REAPER (x64)\reaper.exe"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$realAppData = $env:APPDATA
$app = "$Work\appdata"
$g1 = "$app\Animatek\G1-Emu"
New-Item -ItemType Directory -Force $g1, "$Work\packets" | Out-Null
if (-not (Test-Path "$app\REAPER")) { Copy-Item -Recurse "$realAppData\REAPER" "$app\REAPER" }

# The ROM the real settings name, or the source tree's.
$rom = "$root\Roms\NORD-MODULAR-RACK-VER-3.03.BIN"
$real = "$realAppData\Animatek\G1-Emu\settings.conf"
if (Test-Path $real) {
    $line = Select-String -Path $real -Pattern '^\s*rom\s*=\s*(.+)$' | Select-Object -First 1
    if ($line -and (Test-Path $line.Matches[0].Groups[1].Value.Trim())) { $rom = $line.Matches[0].Groups[1].Value.Trim() }
}
"rom = $rom`nmodel = regular`nshowDisclaimer = 0`ndirectLink = 1" | Set-Content "$g1\settings.conf"

# The patch's upload, packet by packet, as NME sends it.
$pt = Get-ChildItem $root -Directory -Filter "build*" | ForEach-Object {
    Get-ChildItem $_.FullName -Recurse -Filter g1patchtest.exe -ErrorAction SilentlyContinue } | Select-Object -First 1
if (-not $pt) { throw "g1patchtest not built" }
Remove-Item "$Work\packets\*" -ErrorAction SilentlyContinue
& $pt.FullName $rom (Join-Path $root $Patch) --note 60 --seconds 1 --dump-packets "$Work\packets" | Select-String 'uploaded'

$log = "$g1\g1-emu.log"
foreach ($phase in "new", "reopen") {
    "== $phase"
    $before = if (Test-Path $log) { (Get-Content $log).Count } else { 0 }
    $env:APPDATA = $app
    $env:G1_PHASE = $phase
    $env:G1_BENCH_DIR = $Work
    $args_ = @("-newinst", "-nosplash")
    if ($phase -eq "new") { $args_ += @("-new", "`"$PSScriptRoot\bench.lua`"") }
    else { $args_ += @("`"$Work\bench.rpp`"", "`"$PSScriptRoot\bench.lua`"") }
    $p = Start-Process $Reaper -ArgumentList $args_ -PassThru
    if ($phase -eq "new") { python -I "$PSScriptRoot\upload.py" "$Work\packets" --wait-boot 6 }
    if (-not $p.WaitForExit(180000)) { Stop-Process -Id $p.Id -Force; "REAPER did not quit" }
    $env:APPDATA = $realAppData
    Get-Content "$Work\bench-$phase.txt" -ErrorAction SilentlyContinue
    if (Test-Path $log) {
        Get-Content $log | Select-Object -Skip $before |
            Where-Object { $_ -match 'health|dropout|runner end|processes ahead|error|fail' }
    }
}
