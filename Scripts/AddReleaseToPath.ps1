# adds bin\Release-x64 to the user PATH
# Caracal.exe loads CaracalCore.dll, Core and Prelude from its own folder
$repoRoot = Split-Path -Parent $PSScriptRoot
$releaseDirectory = Join-Path $repoRoot "bin\Release-x64"

if (-not (Test-Path (Join-Path $releaseDirectory "Caracal.exe"))) {
    Write-Error "Caracal.exe not found in $releaseDirectory, build the Release-x64 configuration first."
    exit 1
}

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$entries = @()
if ($userPath) {
    $entries = $userPath.Split(";", [StringSplitOptions]::RemoveEmptyEntries)
}

$alreadyPresent = $entries | Where-Object { $_.TrimEnd("\") -ieq $releaseDirectory }
if ($alreadyPresent) {
    Write-Host "$releaseDirectory is already on the user PATH."
}
else {
    $entries += $releaseDirectory
    [Environment]::SetEnvironmentVariable("Path", ($entries -join ";"), "User")
    Write-Host "Added $releaseDirectory to the user PATH."
}

# the current session does not see the user PATH change
if (-not ($env:Path.Split(";") | Where-Object { $_.TrimEnd("\") -ieq $releaseDirectory })) {
    $env:Path = "$env:Path;$releaseDirectory"
}

Write-Host "Open a new terminal for other sessions to pick it up."
