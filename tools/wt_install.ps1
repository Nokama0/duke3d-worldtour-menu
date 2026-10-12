# Installs the World Tour proxy DLL beside the game, and writes a default ini
# if there is not one already. Refuses if the game is running, if duke3d.exe is
# not the World Tour build the code was read from, and if the XINPUT1_3.dll
# already there is not one this installer wrote (another mod's wrapper, x360ce,
# Microsoft's own). Prints MATCH once the installed file equals the build.
# -SelfTest checks the ownership and exe rules on a temp dir and touches nothing else.
param(
  [string]$GameDir = '',
  [string]$Dll = "build\XINPUT1_3.dll",
  [int]$Borderless = 1,
  [int]$FpsCap = 175,
  [switch]$SelfTest
)
$ErrorActionPreference = "Stop"

# Steam World Tour build 386 ("Pagoda-DEF", 2016.10.26): every RVA and fingerprint in src/wtmod was read from it.
$SupportedExeHashes = @('47A52C13FF607DACFEACB81DB0A2B9B829D13EEFEA626656563BCD8AA7F00A43')
# Every build carries the readout's title string; a DLL holding it was built from this repo.
$Marker = 'Nuke Panel WT'
$RecordName = 'wtmod_installed.txt'

function Test-SupportedExe($exe) {
  if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { return $false }
  return $SupportedExeHashes -contains (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
}

# Ours to overwrite: absent, the hash recorded at the last install, the bytes about to be written, or a build of this
# repo (the marker). Anything else is someone else's DLL.
function Test-Owned($dst, $recordedHash, $srcHash) {
  if (-not (Test-Path -LiteralPath $dst)) { return $true }
  $now = (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash
  if (($recordedHash -and $now -eq $recordedHash) -or ($srcHash -and $now -eq $srcHash)) { return $true }
  $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $dst).ProviderPath))
  return $text.Contains($Marker)
}

if ($SelfTest) {
  $t = Join-Path ([IO.Path]::GetTempPath()) ("wt-install-selftest-" + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $t | Out-Null
  try {
    $cases = @()
    $ours = Join-Path $t 'ours.bin'; [IO.File]::WriteAllText($ours, 'ours')
    $foreign = Join-Path $t 'foreign.bin'; [IO.File]::WriteAllText($foreign, 'x360ce wrapper')
    $marked = Join-Path $t 'marked.bin'; [IO.File]::WriteAllText($marked, "MZ..$Marker..")
    $oursHash = (Get-FileHash -LiteralPath $ours -Algorithm SHA256).Hash
    $newHash = 'AB' * 32
    $cases += @{ n = 'absent';         ok = (Test-Owned (Join-Path $t 'nope.bin') $null $newHash) -eq $true }
    $cases += @{ n = 'recorded';       ok = (Test-Owned $ours $oursHash $newHash) -eq $true }
    $cases += @{ n = 'same-as-source'; ok = (Test-Owned $ours $null $oursHash) -eq $true }
    $cases += @{ n = 'marker';         ok = (Test-Owned $marked $null $newHash) -eq $true }
    $cases += @{ n = 'foreign';        ok = (Test-Owned $foreign $null $newHash) -eq $false }
    $cases += @{ n = 'foreign-recorded-other'; ok = (Test-Owned $foreign $oursHash $newHash) -eq $false }
    $cases += @{ n = 'exe-other';      ok = (Test-SupportedExe $foreign) -eq $false }
    $odd = Join-Path $t 'we[i]rd'; New-Item -ItemType Directory -Force -Path $odd | Out-Null
    $cases += @{ n = 'literal-path';   ok = (Test-Owned (Join-Path $odd 'nope.bin') $null $newHash) -eq $true }
    $pass = 0; $fail = 0
    foreach ($c in $cases) { if ($c.ok) { $pass++ } else { $fail++; Write-Host "FAIL $($c.n)" } }
    Write-Host "install self-test: $pass passed, $fail failed"
    if ($fail) { exit 1 } else { exit 0 }
  } finally {
    Remove-Item -LiteralPath $t -Recurse -Force -ErrorAction SilentlyContinue
  }
}

if (-not $GameDir) { Write-Host "REFUSED: -GameDir names the folder holding duke3d.exe"; exit 1 }

# A relative -Dll resolves against THIS REPO, never against the caller's
# working directory. Run from another checkout it happily copied that project's
# build\XINPUT1_3.dll into the game and printed a hash for it, which looks
# exactly like a successful install. (2026-09-13)
if (-not [System.IO.Path]::IsPathRooted($Dll)) {
  $Dll = Join-Path (Split-Path -Parent $PSScriptRoot) $Dll
}

if (Get-Process -Name "duke3d" -ErrorAction SilentlyContinue) {
  Write-Host "REFUSED: duke3d is running. Close the game and run this again."
  exit 1
}
if (-not (Test-Path -LiteralPath $Dll -PathType Leaf)) { Write-Host "REFUSED: no dll to install at $Dll"; exit 1 }
$exe = Join-Path $GameDir "duke3d.exe"
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { Write-Host "REFUSED: no duke3d.exe in $GameDir"; exit 1 }
if (-not (Test-SupportedExe $exe)) {
  Write-Host "  expected $($SupportedExeHashes -join ' or ')"
  Write-Host "  found    $((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)"
  Write-Host "REFUSED: $exe is not the World Tour build this mod was made for (Megaton Edition and other releases also ship a duke3d.exe)."
  exit 1
}

$dest = Join-Path $GameDir "XINPUT1_3.dll"
$record = Join-Path $GameDir $RecordName
$srcHash = (Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash
$recordedHash = $null
if (Test-Path -LiteralPath $record -PathType Leaf) {
  $recordedHash = (Get-Content -LiteralPath $record -TotalCount 1)
  if ($recordedHash) { $recordedHash = $recordedHash.Trim() }
}
if (-not (Test-Owned $dest $recordedHash $srcHash)) {
  Write-Host "REFUSED: $dest was not installed by this script (another XInput wrapper or mod?). Move it aside yourself if you want it replaced."
  exit 1
}
Copy-Item -LiteralPath $Dll -Destination $dest -Force
$destHash = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash
if ($destHash -ne $srcHash) {
  Write-Host "  source    $srcHash"
  Write-Host "  installed $destHash"
  Write-Host "MISMATCH: the installed dll is not the build"
  exit 1
}
Set-Content -LiteralPath $record -Value $destHash -Encoding ASCII
Write-Host "  dll sha256 $destHash"
Write-Host "MATCH"

$ini = Join-Path $GameDir "wtmod.ini"
if (-not (Test-Path -LiteralPath $ini)) {
@"
[wtmod]
; 1 makes the game's window borderless and fills the monitor it is on.
borderless=$Borderless
; Optional explicit rectangle; leave w and h at 0 to use the whole monitor.
x=0
y=0
w=0
h=0
; Frames per second ceiling; 0 leaves the game uncapped, as it ships.
fpscap=$FpsCap
"@ | Set-Content -LiteralPath $ini -Encoding ASCII
  Write-Host "  wrote $ini (borderless=$Borderless fpscap=$FpsCap)"
} else {
  Write-Host "  kept existing $ini"
}
Write-Host "  log will be $(Join-Path $GameDir 'wtmod.log')"
