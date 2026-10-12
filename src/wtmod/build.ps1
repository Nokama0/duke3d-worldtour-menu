# Builds the World Tour proxy DLL. 32-bit, because the game is.
# Output: build\XINPUT1_3.dll, which goes NEXT TO the game's duke3d.exe.
$ErrorActionPreference = "Stop"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsroot = if (Test-Path $vswhere) {
  & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vsroot) { throw "Visual Studio with the C++ x86/x64 build tools was not found (vswhere.exe)" }
& (Join-Path $vsroot "Common7\Tools\Launch-VsDevShell.ps1") -Arch x86 -HostArch amd64 -SkipAutomaticLocation

Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force ..\..\build\obj | Out-Null

# No d3d11.lib: the overlay only calls D3D11 through vtables on interfaces the game already owns, so the DLL
# imports nothing from Direct3D. gdi32 is for the font atlas and fontgen.cpp (panel faces rasterised through GDI).
# menu.cpp is Megaton's panel copied verbatim, with ui/config/wallclock; features.cpp shims the features this port lacks.
# font_data.cpp is the baked font atlas (4100 lines, the slowest unit here), copied byte for byte from Megaton.
# fov.cpp is Megaton's field-of-view arithmetic; fovwt.cpp rewrites one call to the game's own setaspect.
# project/world/esp_pics/esp.cpp are the ESP; freecam/camera.cpp the free camera (one hook substitutes the pose
# into the renderer's globals for a single drawrooms). dmgnumwt.cpp hooks ifhitbyweapon for damage numbers.
# wswitchwt.cpp: weapon switch speed. dbgdump.cpp: on-demand dump key, off unless debugdump=1.
# triggerwt.cpp: trigger chains and secret volume. secretpathwt.cpp: "Path to a secret", a BFS over the sector
# graph (geometry only, so a HINT that ignores keycards and one-way drops). nocullwt.cpp: "Disable culling".
# The suite runs first and a red check stops the build: the pure rules live in headers and are pinned here,
# so a defect fails the build instead of costing the player a round trip.
New-Item -ItemType Directory -Force ..\..\build\obj\tests | Out-Null
cl /nologo /O2 /W3 /EHsc /MT /Zc:preprocessor /Fo..\..\build\obj\tests\ /Fe..\..\build\wtmod_tests.exe `
   tests\test_host.cpp tests\test_actorstate.cpp tests\test_target.cpp target.cpp
if ($LASTEXITCODE -ne 0) { Write-Error "suite failed to compile"; exit 1 }
& ..\..\build\wtmod_tests.exe
if ($LASTEXITCODE -ne 0) { Write-Error "suite FAILED -- not building the DLL"; exit 1 }
# The installer's refusal rules (whose XINPUT1_3.dll it may replace, which duke3d.exe it accepts) on a temp folder.
& powershell -NoProfile -ExecutionPolicy Bypass -File ..\..\tools\wt_install.ps1 -SelfTest
if ($LASTEXITCODE -ne 0) { Write-Error "installer self-test FAILED -- not building the DLL"; exit 1 }

cl /nologo /O2 /W3 /EHsc /MT /MP /Fo..\..\build\obj\ `
   wtmod.cpp hook.cpp mouseaim.cpp smoothview.cpp overlay.cpp `
   canvas.cpp font_data.cpp fontgen.cpp canvas_selftest.cpp `
   input.cpp clipboard.cpp ui.cpp config.cpp wallclock.cpp features.cpp menu.cpp fov.cpp fovwt.cpp `
   project.cpp world.cpp worldwt.cpp esp_pics.cpp esp.cpp dmgnumwt.cpp freecam.cpp camera.cpp nocullwt.cpp `
   dbgdump.cpp wswitchwt.cpp fireratewt.cpp triggerwt.cpp secretpathwt.cpp target.cpp fogwt.cpp lightwt.cpp raycast.cpp aimwt.cpp tbotwt.cpp silentwt.cpp accuracywt.cpp shotwt.cpp autosavewt.cpp actorstatewt.cpp autoswitchwt.cpp `
   /link /DLL /DEF:wtmod.def /OUT:..\..\build\XINPUT1_3.dll user32.lib gdi32.lib
if ($LASTEXITCODE -ne 0) { Write-Error "wtmod build failed"; exit 1 }

$dll = Resolve-Path ..\..\build\XINPUT1_3.dll
Write-Host "built $dll"
Write-Host "  sha256 $((Get-FileHash $dll).Hash)"
