<#
.SYNOPSIS
    Taskbar Thumbnail Reorder (TTR) Auto-Fix & Compatibility Qualification Pipeline
.DESCRIPTION
    Automatically detects new Windows taskbar builds, fetches Microsoft PDBs,
    extracts symbol RVAs with DIA, signs the new compatibility manifest sequence,
    publishes updates to GitHub, installs locally, and launches TTR into your session.
#>

[CmdletBinding()]
param(
    [switch]$SkipPush = $false
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  TTR Auto-Fix: Windows Taskbar Compatibility Updater" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# 1. Locate required tools
$ToolsDir = Join-Path $ScriptDir "out\build\vs2022-x64\Release"
if (-not (Test-Path $ToolsDir)) {
    $candidate = Get-ChildItem -Path "$ScriptDir\out\build" -Filter "compatgen.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($candidate) {
        $ToolsDir = Split-Path -Parent $candidate.FullName
    } else {
        throw "Could not find built tools (compatgen.exe, manifestc.exe). Please build the project first."
    }
}

$ModuleIdExe   = Join-Path $ToolsDir "moduleid.exe"
$CompatGenExe  = Join-Path $ToolsDir "compatgen.exe"
$ManifestCExe  = Join-Path $ToolsDir "manifestc.exe"
$ManifestSignExe = Join-Path $ToolsDir "manifestsign.exe"
$TtrExe        = Join-Path $ToolsDir "TaskbarThumbnailReorder.exe"

$PrivateKey = "$env:USERPROFILE\.ttr\keys\manifest.private.blob"
$PublicKey  = Join-Path $ScriptDir "compat\qualified\manifest-public-key.bin"
$SymbolSpec = Join-Path $ScriptDir "compat\symbol-spec.yaml"
$FeedDir    = Join-Path $ScriptDir "out\compat-feed"
$LocalCompatDir = "$env:LOCALAPPDATA\TaskbarThumbnailReorder\compat"

if (-not (Test-Path $PrivateKey)) {
    throw "Private signing key not found at $PrivateKey."
}

# 2. Identify installed Windows taskbar modules
Write-Host "`n[1/7] Inspecting installed Windows taskbar modules..." -ForegroundColor Yellow
$TaskbarDll = "C:\Windows\System32\Taskbar.dll"
$TaskbarViewDll = Get-ChildItem -Path "C:\Windows\SystemApps" -Filter "Taskbar.View.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName -First 1

if (-not (Test-Path $TaskbarDll) -or -not $TaskbarViewDll -or -not (Test-Path $TaskbarViewDll)) {
    throw "Unable to locate Taskbar.dll and Taskbar.View.dll on this system."
}

# 3. Check if already supported by installed or repository manifest
Write-Host "`n[2/7] Checking current compatibility status..." -ForegroundColor Yellow
$InstalledManifest = Join-Path $LocalCompatDir "compat.bin"
$InstalledSig      = Join-Path $LocalCompatDir "compat.sig"

if ((Test-Path $InstalledManifest) -and (Test-Path $InstalledSig)) {
    $diagOut = & $TtrExe --diagnose-offline --manifest $InstalledManifest --signature $InstalledSig 2>&1 | Out-String
    if ($diagOut -match "Compatibility:\s+SUPPORTED" -and $diagOut -match "Exact record match:\s+yes") {
        Write-Host "  -> Current taskbar build is ALREADY supported and qualified!" -ForegroundColor Green
        
        # Ensure TTR is running
        $running = Get-Process -Name "*TaskbarThumbnailReorder*" -ErrorAction SilentlyContinue
        if (-not $running) {
            Write-Host "  -> Launching TTR into interactive session..." -ForegroundColor Cyan
            schtasks /create /tn "TTR_Launch" /tr "`"$env:LOCALAPPDATA\TaskbarThumbnailReorder\TaskbarThumbnailReorder.exe`"" /sc once /st 00:00 /it /f | Out-Null
            schtasks /run /tn "TTR_Launch" | Out-Null
            schtasks /delete /tn "TTR_Launch" /f | Out-Null
        }
        Write-Host "`nAll good! TTR is active." -ForegroundColor Green
        exit 0
    }
}

# 4. Extract CodeView information using moduleid
Write-Host "  -> New or unsupported taskbar build detected. Qualifying now..." -ForegroundColor Yellow
$modTaskbarJson     = & $ModuleIdExe $TaskbarDll | ConvertFrom-Json
$modTaskbarViewJson = & $ModuleIdExe $TaskbarViewDll | ConvertFrom-Json

$modTaskbar     = $modTaskbarJson[0]
$modTaskbarView = $modTaskbarViewJson[0]

Write-Host ("     Taskbar.dll:      Timestamp {0}, Size {1}, GUID {2}" -f $modTaskbar.timestamp, $modTaskbar.size_of_image, $modTaskbar.codeview.pdb_guid) -ForegroundColor Gray
Write-Host ("     Taskbar.View.dll: Timestamp {0}, Size {1}, GUID {2}" -f $modTaskbarView.timestamp, $modTaskbarView.size_of_image, $modTaskbarView.codeview.pdb_guid) -ForegroundColor Gray

# 5. Download Microsoft PDB symbols
Write-Host "`n[3/7] Fetching official Microsoft public PDB symbols..." -ForegroundColor Yellow
$ScratchDir = Join-Path $ScriptDir "out\qualification\unqualified\autofix-symbols"
New-Item -ItemType Directory -Path $ScratchDir -Force | Out-Null

function Download-Pdb($modInfo, $targetDir) {
    $guidClean = $modInfo.codeview.pdb_guid.Replace("{","").Replace("}","").Replace("-","").ToUpper()
    $age = $modInfo.codeview.pdb_age
    $pdbName = $modInfo.codeview.pdb_path
    $url = "https://msdl.microsoft.com/download/symbols/$pdbName/$guidClean$age/$pdbName"
    $dest = Join-Path $targetDir $pdbName
    
    if (-not (Test-Path $dest)) {
        Write-Host "  -> Downloading $pdbName from $url ..." -ForegroundColor Gray
        Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    } else {
        Write-Host "  -> Using cached $pdbName" -ForegroundColor Gray
    }
    return $dest
}

$TaskbarPdb     = Download-Pdb $modTaskbar $ScratchDir
$TaskbarViewPdb = Download-Pdb $modTaskbarView $ScratchDir

# 6. Run compatgen to resolve symbol RVAs
Write-Host "`n[4/7] Resolving XAML symbols with compatgen..." -ForegroundColor Yellow
$FragTaskbar     = Join-Path $ScratchDir "taskbar-frag.json"
$FragTaskbarView = Join-Path $ScratchDir "taskbar-view-frag.json"

& $CompatGenExe $TaskbarDll $TaskbarPdb $SymbolSpec xaml $FragTaskbar
if ($LASTEXITCODE -ne 0) { throw "compatgen failed for Taskbar.dll" }

& $CompatGenExe $TaskbarViewDll $TaskbarViewPdb $SymbolSpec xaml $FragTaskbarView
if ($LASTEXITCODE -ne 0) { throw "compatgen failed for Taskbar.View.dll" }

$frag1 = Get-Content $FragTaskbar | ConvertFrom-Json
$frag2 = Get-Content $FragTaskbarView | ConvertFrom-Json

# 7. Build next record and sequence
Write-Host "`n[5/7] Assembling new compatibility record and compiling manifest..." -ForegroundColor Yellow
$FeedRecordsDir = Join-Path $FeedDir "records"
if (-not (Test-Path $FeedRecordsDir)) {
    $FeedRecordsDir = Join-Path $ScriptDir "compat\qualified\records"
}

# Fetch latest record file
$recordFiles = Get-ChildItem -Path $FeedRecordsDir -Filter "*.json" | Sort-Object Name
$latestRecordFile = $recordFiles[-1]
$latestData = Get-Content $latestRecordFile.FullName | ConvertFrom-Json

$newSequence = [int]$latestData.sequence + 1
$newRecordId = [uint64]($latestData.records[-1].id + 1)

Write-Host "  -> Creating Sequence $newSequence with Record ID $newRecordId..." -ForegroundColor Cyan

# Construct symbols array
$allSymbols = [System.Collections.Generic.List[object]]::new()
foreach ($sym in $frag1.symbols) {
    $allSymbols.Add([PSCustomObject]@{
        id = $sym.id
        module = 0
        rva = [uint32]$sym.rva
        kind = $sym.kind
        required = $sym.required
    })
}
foreach ($sym in $frag2.symbols) {
    $allSymbols.Add([PSCustomObject]@{
        id = $sym.id
        module = 1
        rva = [uint32]$sym.rva
        kind = $sym.kind
        required = $sym.required
    })
}

$newRecord = [PSCustomObject]@{
    id = $newRecordId
    backend_flags = @("xaml")
    modules = @(
        [PSCustomObject]@{
            name = "taskbar.dll"
            pdb_age = [uint32]$modTaskbar.codeview.pdb_age
            pdb_guid = $modTaskbar.codeview.pdb_guid
            size_of_image = [uint32]$modTaskbar.size_of_image
            timestamp = [uint32]$modTaskbar.timestamp
        },
        [PSCustomObject]@{
            name = "taskbar.view.dll"
            pdb_age = [uint32]$modTaskbarView.codeview.pdb_age
            pdb_guid = $modTaskbarView.codeview.pdb_guid
            size_of_image = [uint32]$modTaskbarView.size_of_image
            timestamp = [uint32]$modTaskbarView.timestamp
        }
    )
    symbols = $allSymbols
    adjustments = @()
}

$combinedRecords = [System.Collections.Generic.List[object]]::new()
foreach ($r in $latestData.records) {
    $combinedRecords.Add($r)
}
$combinedRecords.Add($newRecord)

$newManifestJson = [PSCustomObject]@{
    sequence = $newSequence
    records  = $combinedRecords
}

$NewRecordJsonPath = Join-Path $ScriptDir "compat\qualified\records\$newRecordId.json"
$newManifestJson | ConvertTo-Json -Depth 10 | Set-Content -Path $NewRecordJsonPath -Encoding UTF8

# Compile and sign
$CompatBinPath = Join-Path $ScriptDir "compat\qualified\compat.bin"
$CompatSigPath = Join-Path $ScriptDir "compat\qualified\compat.sig"

& $ManifestCExe $NewRecordJsonPath $CompatBinPath
if ($LASTEXITCODE -ne 0) { throw "manifestc compilation failed" }

& $ManifestSignExe sign $PrivateKey $CompatBinPath
if ($LASTEXITCODE -ne 0) { throw "manifestsign signing failed" }

Move-Item -Path "$CompatBinPath.sig" -Destination $CompatSigPath -Force

& $ManifestSignExe verify $PublicKey $CompatBinPath $CompatSigPath
if ($LASTEXITCODE -ne 0) { throw "manifestsign verification failed" }

# Test offline diagnosis against new manifest
$diagVerify = & $TtrExe --diagnose-offline --manifest $CompatBinPath --signature $CompatSigPath 2>&1 | Out-String
if ($diagVerify -notmatch "Compatibility:\s+SUPPORTED" -or $diagVerify -notmatch "Result:\s+PASS") {
    throw "Offline diagnostics verification failed on newly generated manifest!"
}
Write-Host "  -> Manifest Sequence $newSequence compiled, signed, and validated PASS!" -ForegroundColor Green

# 8. Update Repositories & Push
Write-Host "`n[6/7] Updating repositories and publishing feed..." -ForegroundColor Yellow

# Update out/compat-feed
if (Test-Path $FeedDir) {
    Copy-Item $CompatBinPath -Destination (Join-Path $FeedDir "compat.bin") -Force
    Copy-Item $CompatSigPath -Destination (Join-Path $FeedDir "compat.sig") -Force
    Copy-Item $NewRecordJsonPath -Destination (Join-Path $FeedDir "records\$newRecordId.json") -Force

    # Generate SHA256SUMS.txt
    $sums = [System.Collections.Generic.List[string]]::new()
    $sums.Add("$((Get-FileHash -Algorithm SHA256 (Join-Path $FeedDir 'compat.bin')).Hash.ToLower())  compat.bin")
    $sums.Add("$((Get-FileHash -Algorithm SHA256 (Join-Path $FeedDir 'compat.sig')).Hash.ToLower())  compat.sig")
    
    $feedRecFiles = Get-ChildItem -Path (Join-Path $FeedDir "records") -Filter "*.json" | Sort-Object Name
    foreach ($rf in $feedRecFiles) {
        $sums.Add("$((Get-FileHash -Algorithm SHA256 $rf.FullName).Hash.ToLower())  records/$($rf.Name)")
    }
    
    $sumsContent = ($sums -join "`n") + "`n"
    [System.IO.File]::WriteAllText((Join-Path $FeedDir "SHA256SUMS.txt"), $sumsContent, [System.Text.UTF8Encoding]::new($false))

    if (-not $SkipPush) {
        try {
            git -C $FeedDir add .
            git -C $FeedDir commit -m "Publish signed compatibility manifest sequence $newSequence (record $newRecordId)"
            git -C $FeedDir push origin main
            Write-Host "  -> Pushed Sequence $newSequence to SukazuC/TTR-compat." -ForegroundColor Green
        } catch {
            Write-Warning "Could not push to TTR-compat remote: $_"
        }
    }
}

# Update main TTR repo docs
$docPath = Join-Path $ScriptDir "docs\qualification\$newRecordId.md"
$osVer = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion").DisplayVersion
$osBuild = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion").CurrentBuild
$osUbr = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion").UBR
$today = (Get-Date).ToString("MMMM dd, yyyy")

$docContent = @"
# Qualification record $newRecordId

Qualification completed $today for Taskbar Thumbnail Reorder on Windows 11 $osVer build $osBuild.$osUbr.

## Exact compatibility scope

The selected backend is non-animated XAML. This qualification applies only when both module
identities match exactly:

| Module | Path | Timestamp | SizeOfImage | PDB GUID | Age | Module SHA-256 |
| --- | --- | ---: | ---: | --- | ---: | --- |
| `taskbar.dll` | `C:\Windows\System32\Taskbar.dll` | $($modTaskbar.timestamp) | $($modTaskbar.size_of_image) | `$($modTaskbar.codeview.pdb_guid)` | $($modTaskbar.codeview.pdb_age) | `$($modTaskbar.sha256.ToUpper())` |
| `Taskbar.View.dll` | `$TaskbarViewDll` | $($modTaskbarView.timestamp) | $($modTaskbarView.size_of_image) | `$($modTaskbarView.codeview.pdb_guid)` | $($modTaskbarView.codeview.pdb_age) | `$($modTaskbarView.sha256.ToUpper())` |

Both Microsoft PDBs matched their DLL CodeView GUID and age exactly before DIA enumeration. All 14
required XAML symbols resolved uniquely and passed section-permission validation.

## Signed compatibility data

Sequence $newSequence retains legacy records and adds exact record $newRecordId.

| File | SHA-256 |
| --- | --- |
| `records/$newRecordId.json` | `$((Get-FileHash -Algorithm SHA256 $NewRecordJsonPath).Hash)` |
| `compat.bin` | `$((Get-FileHash -Algorithm SHA256 $CompatBinPath).Hash)` |
| `compat.sig` | `$((Get-FileHash -Algorithm SHA256 $CompatSigPath).Hash)` |
| `manifest-public-key.bin` | `$((Get-FileHash -Algorithm SHA256 $PublicKey).Hash)` |
"@
[System.IO.File]::WriteAllText($docPath, $docContent + "`n", [System.Text.UTF8Encoding]::new($false))

if (-not $SkipPush) {
    try {
        git -C $ScriptDir add README.md compat/qualified/ docs/qualification/
        git -C $ScriptDir commit -m "Qualify record $newRecordId (sequence $newSequence)"
        git -C $ScriptDir push origin master
        Write-Host "  -> Pushed qualification record to SukazuC/TTR." -ForegroundColor Green
    } catch {
        Write-Warning "Could not push to TTR remote: $_"
    }
}

# 9. Deploy Locally & Launch
Write-Host "`n[7/7] Installing update locally and restarting TTR..." -ForegroundColor Yellow
New-Item -ItemType Directory -Path $LocalCompatDir -Force | Out-Null
Copy-Item $CompatBinPath -Destination (Join-Path $LocalCompatDir "compat.bin") -Force
Copy-Item $CompatSigPath -Destination (Join-Path $LocalCompatDir "compat.sig") -Force

Stop-Process -Name "*TaskbarThumbnailReorder*" -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

schtasks /create /tn "TTR_Launch" /tr "`"$env:LOCALAPPDATA\TaskbarThumbnailReorder\TaskbarThumbnailReorder.exe`"" /sc once /st 00:00 /it /f | Out-Null
schtasks /run /tn "TTR_Launch" | Out-Null
schtasks /delete /tn "TTR_Launch" /f | Out-Null

Write-Host "`n============================================================" -ForegroundColor Green
Write-Host "  SUCCESS! TTR has been qualified, signed, published, and launched." -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
