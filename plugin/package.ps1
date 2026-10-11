# Builds the plugin and packs a release: plugin\build\takeawalk-<version>.zip, holding the
# plugin, its settings and sounds, the installer and the notes for players.
# The version comes from TAKEAWALK_VERSION in src\takeawalk.cpp.
param(
	[switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out  = Join-Path $root 'build'

$define = Select-String -LiteralPath (Join-Path $root 'src\takeawalk.cpp') -Pattern '#define\s+TAKEAWALK_VERSION\s+"([^"]+)"'
if (-not $define) { throw 'TAKEAWALK_VERSION was not found in src\takeawalk.cpp.' }
$version = $define.Matches[0].Groups[1].Value

if (-not $SkipBuild) {
	& (Join-Path $root 'build.ps1')
}

$name  = "takeawalk-$version"
$stage = Join-Path $out $name
$zip   = Join-Path $out "$name.zip"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse }
if (Test-Path -LiteralPath $zip)   { Remove-Item -LiteralPath $zip }

$plugins = Join-Path $stage 'plugins'
$sounds  = Join-Path $plugins 'takeawalk'
New-Item -ItemType Directory -Force -Path $sounds | Out-Null

Copy-Item -LiteralPath (Join-Path $out 'takeawalk.dll')   -Destination $plugins
Copy-Item -LiteralPath (Join-Path $root 'takeawalk.ini')  -Destination $plugins
Copy-Item -Path (Join-Path $root 'sounds\*.ogg')          -Destination $sounds
foreach ($file in 'install.bat', 'uninstall.bat', 'install.ps1', 'README.txt', 'THIRD-PARTY.txt') {
	Copy-Item -LiteralPath (Join-Path $root "release\$file") -Destination $stage
}

# The settings a player gets must not have a diagnostic left switched on.
$enabled = Select-String -LiteralPath (Join-Path $plugins 'takeawalk.ini') -Pattern '^(trace_\w+|collision_census|probe_key)=1'
if ($enabled) { throw "takeawalk.ini has diagnostics switched on: $($enabled.Line -join ', ')" }

Compress-Archive -Path $stage -DestinationPath $zip
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
"Packed $zip"
"SHA-256 $hash"
