# Build Spriggit ESP for Skyrim VR
# Converts the Spriggit YAML files to 3DUI.esp

param(
    [string]$OutputDir = "C:\games\skyrim\VRDEV\mods\DressVR"
)

$ErrorActionPreference = "Stop"

# Paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SpriggitDir = Split-Path -Parent $ScriptDir
$EspVersionsDir = Join-Path $SpriggitDir "esp-versions"
$EspName = "3DUI.esp"
$EspPath = Join-Path $EspVersionsDir $EspName

# Ensure esp-versions directory exists
if (-not (Test-Path $EspVersionsDir)) {
    New-Item -ItemType Directory -Path $EspVersionsDir | Out-Null
    Write-Host "Created esp-versions directory"
}

# If existing ESP exists, rename it with git commit hash
if (Test-Path $EspPath) {
    # Get short git commit hash
    $GitHash = git rev-parse --short HEAD 2>$null
    if (-not $GitHash) {
        Write-Warning "Could not get git hash, using timestamp instead"
        $GitHash = Get-Date -Format "yyyyMMdd-HHmmss"
    }

    $BackupName = "3DUI.$GitHash.esp"
    $BackupPath = Join-Path $EspVersionsDir $BackupName

    # Don't overwrite if backup already exists with same hash
    if (-not (Test-Path $BackupPath)) {
        Move-Item -Path $EspPath -Destination $BackupPath
        Write-Host "Backed up existing ESP to: $BackupName"
    } else {
        Remove-Item -Path $EspPath
        Write-Host "Removed existing ESP (backup with hash $GitHash already exists)"
    }
}

# Build the ESP using Spriggit
Write-Host "Building ESP from Spriggit files..."
Write-Host "  Input:  $SpriggitDir"
Write-Host "  Output: $EspPath"

spriggit deserialize -i "$SpriggitDir" -o "$EspPath"

if ($LASTEXITCODE -ne 0) {
    Write-Error "Spriggit build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

if (-not (Test-Path $EspPath)) {
    Write-Error "ESP file was not created"
    exit 1
}

Write-Host "ESP built successfully: $EspPath" -ForegroundColor Green

# Copy to output directory
if ($OutputDir) {
    if (-not (Test-Path $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
        Write-Host "Created output directory: $OutputDir"
    }

    $FinalPath = Join-Path $OutputDir $EspName
    Copy-Item -Path $EspPath -Destination $FinalPath -Force
    Write-Host "Copied ESP to: $FinalPath" -ForegroundColor Green
}

Write-Host "Build complete!" -ForegroundColor Cyan
