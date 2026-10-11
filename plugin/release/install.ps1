# Installs Take a Walk into Euro Truck Simulator 2, or removes it with -Uninstall.
# Run it through install.bat or uninstall.bat. -GameDir names the game's folder when
# it cannot be found through Steam.
param(
	[switch]$Uninstall,
	[string]$GameDir
)

$ErrorActionPreference = 'Stop'

$gameName   = 'Euro Truck Simulator 2'
$testedGame = '1.61.1.1'
$source     = Join-Path $PSScriptRoot 'plugins'

function Test-GameDir([string]$dir) {
	# Not Join-Path: it fails on a drive which is not connected, and Steam may list such a library.
	return $dir -and (Test-Path -LiteralPath ([IO.Path]::Combine($dir, 'bin\win_x64\eurotrucks2.exe')))
}

# Steam lists every library folder in libraryfolders.vdf of its own install.
function Find-GameDir {
	$steamRoots = @()
	foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
		$properties = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
		if ($properties) {
			$steamRoots += $properties.SteamPath, $properties.InstallPath
		}
	}
	$steamRoots += (Join-Path ${env:ProgramFiles(x86)} 'Steam')

	foreach ($steam in ($steamRoots | Where-Object { $_ } | Select-Object -Unique)) {
		$libraries = @($steam)
		$list = [IO.Path]::Combine($steam, 'steamapps\libraryfolders.vdf')
		if (Test-Path -LiteralPath $list) {
			foreach ($line in Get-Content -LiteralPath $list) {
				if ($line -match '"path"\s+"(.+)"') {
					$libraries += $Matches[1].Replace('\\', '\')
				}
			}
		}
		foreach ($library in $libraries) {
			$candidate = [IO.Path]::Combine($library, 'steamapps\common', $gameName)
			if (Test-GameDir $candidate) {
				return $candidate
			}
		}
	}
	return $null
}

if (-not (Test-GameDir $GameDir)) {
	if ($GameDir) {
		Write-Host "No game found in '$GameDir'."
	}
	$GameDir = Find-GameDir
}
while (-not (Test-GameDir $GameDir)) {
	Write-Host "$gameName was not found through Steam."
	Write-Host 'In Steam: right-click the game, Manage, Browse local files, and copy the folder''s path.'
	$GameDir = (Read-Host 'Paste the game folder here (or press Enter to give up)').Trim('"', ' ')
	if (-not $GameDir) {
		exit 1
	}
}

$plugins = Join-Path $GameDir 'bin\win_x64\plugins'
Write-Host "Game folder: $GameDir"

if (Get-Process -Name 'eurotrucks2' -ErrorAction SilentlyContinue) {
	Write-Host 'The game is running. Close it and run this again.'
	exit 1
}

$dll      = Join-Path $plugins 'takeawalk.dll'
$settings = Join-Path $plugins 'takeawalk.ini'
$sounds   = Join-Path $plugins 'takeawalk'
$restore  = Join-Path $plugins 'takeawalk.restore'

if ($Uninstall) {
	if (Test-Path -LiteralPath $restore) {

		# The game was last closed while on foot, with the speedometer and hints hidden.
		# The plugin puts them back the next time the game starts, so it has to stay for that.

		Write-Host 'The game was last closed while you were on foot. Start the game once and quit it,'
		Write-Host 'so that Take a Walk can put your speedometer and hint settings back, then run this again.'
		exit 1
	}
	$removed = $false
	foreach ($file in $dll, $settings) {
		if (Test-Path -LiteralPath $file) {
			Remove-Item -LiteralPath $file
			$removed = $true
		}
	}
	if (Test-Path -LiteralPath $sounds) {
		Remove-Item -LiteralPath $sounds -Recurse
		$removed = $true
	}
	if ($removed) {
		Write-Host 'Take a Walk is removed.'
	}
	else {
		Write-Host 'Take a Walk was not installed here.'
	}
	exit 0
}

if (-not (Test-Path -LiteralPath (Join-Path $source 'takeawalk.dll'))) {
	Write-Host "The 'plugins' folder with takeawalk.dll is missing next to this script. Extract the whole zip first."
	exit 1
}

$version = (Get-Item -LiteralPath (Join-Path $GameDir 'bin\win_x64\eurotrucks2.exe')).VersionInfo.ProductVersion
if ($version -ne $testedGame) {
	if (-not $version) { $version = 'unknown' }
	Write-Host "Note: this game is version $version. Take a Walk was made for $testedGame;"
	Write-Host 'on another version it may switch features off or not work. See the README.'
}

New-Item -ItemType Directory -Force -Path $plugins | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'takeawalk.dll') -Destination $plugins -Force

# Recordings from an earlier version go first: the plugin plays every footstepNN.ogg it finds.
New-Item -ItemType Directory -Force -Path $sounds | Out-Null
Get-ChildItem -LiteralPath $sounds -Filter 'footstep*.ogg' | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
Copy-Item -Path (Join-Path $source 'takeawalk\*') -Destination $sounds -Force

if (Test-Path -LiteralPath $settings) {
	Write-Host 'Your existing takeawalk.ini is kept. New settings use their defaults until you add them.'
}
else {
	Copy-Item -LiteralPath (Join-Path $source 'takeawalk.ini') -Destination $plugins
}

Write-Host ''
Write-Host "Take a Walk is installed in $plugins"
Write-Host 'Start the game, accept its notice about SDK features, park, set the parking brake and press F9.'
