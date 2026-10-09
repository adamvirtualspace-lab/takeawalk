# Builds plugin\build\takeawalk.dll with the MSVC toolchain from Visual Studio.
# Pass -Install to also copy it into the game's plugins folder (game must be closed).
param(
	[switch]$Install,
	[string]$GameDir = 'E:\home\adam\.local\share\Steam\steamapps\common\Euro Truck Simulator 2'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out  = Join-Path $root 'build'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ toolchain was not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

New-Item -ItemType Directory -Force $out | Out-Null

$cl = @(
	'cl', '/nologo', '/LD', '/O2', '/MT', '/W4', '/EHsc', '/std:c++17', '/D_CRT_SECURE_NO_WARNINGS',
	"/I`"$root\third_party\scs_sdk\include`"", "/I`"$root\third_party`"",
	"/Fo`"$out\\`"", "/Fe`"$out\takeawalk.dll`"",
	"`"$root\src\*.cpp`"", "`"$root\src\*.c`"",
	'/link', 'user32.lib', 'gdi32.lib', 'winmm.lib', "/DEF:`"$root\src\takeawalk.def`"", "/IMPLIB:`"$out\takeawalk.lib`""
) -join ' '

cmd /c "`"$vcvars`" >nul && $cl"
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

if ($Install) {
	$plugins = Join-Path $GameDir 'bin\win_x64\plugins'
	New-Item -ItemType Directory -Force $plugins | Out-Null
	Copy-Item (Join-Path $out 'takeawalk.dll') $plugins -Force

	# Footstep recordings go in a folder of their own next to the plugin.
	# Recordings from an earlier install go first: the plugin loads every footstepNN.ogg it finds.
	$sounds = Join-Path $plugins 'takeawalk'
	New-Item -ItemType Directory -Force $sounds | Out-Null
	Get-ChildItem $sounds -Filter 'footstep*.ogg' | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
	Copy-Item (Join-Path $root 'sounds\*') $sounds -Force

	# Settings the player may have changed are left alone.
	if (-not (Test-Path (Join-Path $plugins 'takeawalk.ini'))) {
		Copy-Item (Join-Path $root 'takeawalk.ini') $plugins
	}
	"Installed to $plugins"
}
