# Packs the staged folder into an .msix using Microsoft's MakeAppx (part of the free Windows SDK).
# Each run writes a NEW, uniquely named file, so a previous package that is still open in the browser can't block it.
param([string]$Stage = (Join-Path $PSScriptRoot "stage"), [string]$OutDir = $PSScriptRoot)
$mk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\makeappx.exe" -ErrorAction SilentlyContinue |
      Sort-Object FullName -Descending | Select-Object -First 1
if (-not $mk) {
    Write-Host "makeappx.exe was not found." -ForegroundColor Yellow
    Write-Host "Install the Windows SDK (https://developer.microsoft.com/windows/downloads/windows-sdk/) and tick 'Windows SDK Signing Tools for Desktop Apps', then run this again."
    exit 1
}
Write-Host "Using $($mk.FullName)"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$Out = Join-Path $OutDir "MudCrawler-$stamp.msix"
function Pack($target) { & $mk.FullName pack /d $Stage /p $target /o | Out-Host; return $LASTEXITCODE }

$rc = Pack $Out
if ($rc -ne 0) {
    # Something blocked writing here (antivirus scan, locked folder, ...). Build in the temp folder instead, then copy.
    Write-Host "First attempt failed. Retrying through your temp folder..." -ForegroundColor Yellow
    $tmp = Join-Path $env:TEMP "MudCrawler-$stamp.msix"
    $rc = Pack $tmp
    if ($rc -eq 0) {
        Copy-Item $tmp $Out -Force -ErrorAction SilentlyContinue
        if (-not (Test-Path $Out)) { $Out = $tmp }
    }
}
if ($rc -eq 0 -and (Test-Path $Out)) {
    Write-Host ""
    Write-Host "Created: $Out" -ForegroundColor Green
    Write-Host "Upload that file in Partner Center (Packages)."
    Start-Process explorer.exe "/select,`"$Out`""
} else {
    Write-Host ""
    Write-Host "Packing failed. Copy the text above and send it to me." -ForegroundColor Red
    exit 1
}
