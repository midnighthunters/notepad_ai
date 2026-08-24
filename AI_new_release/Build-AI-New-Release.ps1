[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$solution = Join-Path $repoRoot 'PowerEditor\build\Notepad++.sln'
$releaseRoot = Join-Path $PSScriptRoot 'portable'
$compileDirectory = Join-Path $PSScriptRoot 'build\x64'
$stagingDirectory = Join-Path $releaseRoot $Platform

function Find-MSBuild {
    $command = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vsWhere) {
        $installationPath = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    throw 'MSBuild was not found. Install Visual Studio 2022 Build Tools with the C++ desktop workload, then run this script again.'
}

$msBuild = Find-MSBuild
New-Item -ItemType Directory -Path $compileDirectory -Force | Out-Null
& $msBuild $solution /m /t:Build "/p:Configuration=Release;Platform=$Platform;OutDir=$compileDirectory\"
if ($LASTEXITCODE -ne 0) {
    throw "Release build failed with exit code $LASTEXITCODE."
}

$sourceDirectory = Join-Path $repoRoot 'PowerEditor\build\Release'
$compiledExecutable = Join-Path $compileDirectory 'notepad++.exe'
if (-not (Test-Path -LiteralPath $compiledExecutable)) {
    throw "The expected release executable was not produced in $compileDirectory."
}

if (Test-Path -LiteralPath $stagingDirectory) {
    Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
Copy-Item -Path (Join-Path $sourceDirectory '*') -Destination $stagingDirectory -Recurse -Force
Copy-Item -LiteralPath $compiledExecutable -Destination (Join-Path $stagingDirectory 'notepad++.exe') -Force

# This marker makes the portable release keep its configuration beside the executable,
# avoiding an installed Notepad++ configuration directory.
New-Item -ItemType File -Path (Join-Path $stagingDirectory 'doLocalConf.xml') -Force | Out-Null

$archive = Join-Path $releaseRoot "AI_new_release-$Platform-portable.zip"
if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive -Force
}
Compress-Archive -Path (Join-Path $stagingDirectory '*') -DestinationPath $archive -CompressionLevel Optimal
Write-Host "Portable AI release staged at: $stagingDirectory"
Write-Host "Portable AI release archive:    $archive"
