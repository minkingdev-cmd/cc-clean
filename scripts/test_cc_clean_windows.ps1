[CmdletBinding()]
param(
    [string[]]$Tests
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

$RootDir = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $RootDir "build-cc-clean\Release\cc-clean.exe"
$Src = Join-Path $RootDir "csrc\cc_clean.c"

function Assert-Contains {
    param(
        [string]$Haystack,
        [string]$Needle,
        [string]$Name
    )
    if (-not $Haystack.Contains($Needle)) {
        throw "${Name}: missing [$Needle]"
    }
}

function Assert-Exists {
    param([string]$PathValue, [string]$Name)
    if (-not (Test-Path -LiteralPath $PathValue)) {
        throw "${Name}: missing $PathValue"
    }
}

function Assert-NotExists {
    param([string]$PathValue, [string]$Name)
    if (Test-Path -LiteralPath $PathValue) {
        throw "${Name}: still exists $PathValue"
    }
}

function Assert-True {
    param([bool]$Condition, [string]$Name)
    if (-not $Condition) {
        throw "${Name}: assertion failed"
    }
}

function Build-IfNeeded {
    if (-not (Test-Path -LiteralPath $Bin)) {
        cmake -S $RootDir -B (Join-Path $RootDir "build-cc-clean")
        cmake --build (Join-Path $RootDir "build-cc-clean") --config Release
    }
}

function New-TestDir {
    param([string]$Prefix)
    $dir = Join-Path ([System.IO.Path]::GetTempPath()) ($Prefix + "." + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $dir | Out-Null
    return $dir
}

function Test-HelpFlags {
    $out = & $Bin --help | Out-String
    Assert-Contains $out "--allow-unsafe-purge" "help output"
    Assert-Contains $out "--allow-unsafe-restore" "help output"
    Write-Host "PASS: help output includes dangerous flags"
}

function Test-NoSystemCall {
    $hasSystem = Select-String -Path $Src -SimpleMatch "system(" -Quiet
    if ($hasSystem) {
        throw "source still contains system()"
    }
    Write-Host "PASS: source does not contain system()"
}

function Test-PurgeGuard {
    $tmp = New-TestDir "ccfgtest-purge"
    New-Item -ItemType Directory -Path (Join-Path $tmp "projects") | Out-Null
    Set-Content -LiteralPath (Join-Path $tmp ".credentials.json") -Value "secret" -NoNewline
    try {
        $out = & $Bin clean --config-dir $tmp --purge-config-home --dry-run 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) {
            throw "purge should reject non-default config dir"
        }
        Assert-Contains $out "--purge-config-home" "purge guard"
        Write-Host "PASS: default purge guard works"
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-BackupAndRestoreFlow {
    $tmp = New-TestDir "ccfgtest-flow"
    $backup = New-TestDir "ccbackup-flow"
    try {
        New-Item -ItemType Directory -Path (Join-Path $tmp "projects\session1") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $tmp ".credentials.json") -Value "secret" -NoNewline
        Set-Content -LiteralPath (Join-Path $tmp "projects\session1\msg.txt") -Value "hello" -NoNewline

        $cleanOut = & $Bin clean --config-dir $tmp --backup-dir $backup -y | Out-String
        Assert-Contains $cleanOut "[removed]" "clean output"
        Assert-Exists (Join-Path $backup "manifest.tsv") "manifest"
        Assert-Exists (Join-Path $backup "index.md") "index.md"
        Assert-Contains ((Get-Content -LiteralPath (Join-Path $backup "manifest.tsv") -Raw)) "VERSION`t2" "manifest version"
        Assert-NotExists (Join-Path $tmp ".credentials.json") "clean removed credentials"
        Assert-NotExists (Join-Path $tmp "projects") "clean removed projects"

        $restoreBlocked = & $Bin restore --backup-dir $backup -y 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) {
            throw "restore should reject temp dir without allow-unsafe-restore"
        }
        Assert-Contains $restoreBlocked "--allow-unsafe-restore" "restore allowlist guard"

        & $Bin restore --backup-dir $backup --allow-unsafe-restore -y | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "restore failed after allow-unsafe-restore"
        }
        Assert-Exists (Join-Path $tmp ".credentials.json") "restore credentials"
        Assert-Exists (Join-Path $tmp "projects\session1\msg.txt") "restore projects"
        Write-Host "PASS: clean/backup/restore flow passed"
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-RestoreJsonOutput {
    $tmp = New-TestDir "ccfgtest-restorejson"
    $backup = New-TestDir "ccbackup-restorejson"
    try {
        New-Item -ItemType Directory -Path (Join-Path $tmp "projects\session1") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $tmp ".credentials.json") -Value "secret" -NoNewline
        Set-Content -LiteralPath (Join-Path $tmp "projects\session1\msg.txt") -Value "hello" -NoNewline

        & $Bin clean --config-dir $tmp --backup-dir $backup -y | Out-Null
        $jsonOut = & $Bin restore --backup-dir $backup --allow-unsafe-restore --json -y | Out-String
        $obj = $jsonOut | ConvertFrom-Json
        Assert-True ($null -ne $obj.cleanup_results) "restore json cleanup_results"
        Assert-True ($obj.cleanup_results.Count -ge 2) "restore json count"
        Assert-True (($obj.cleanup_results | Where-Object { $_.status -ne "restored" }).Count -eq 0) "restore json restored state"
        Assert-True (($obj.cleanup_results | Where-Object { $_.kind -eq "filesystem" -and $_.identifier -like "*.credentials.json" }).Count -ge 1) "restore json credentials result"
        Assert-Exists (Join-Path $tmp ".credentials.json") "restore json credentials restored"
        Assert-Exists (Join-Path $tmp "projects\session1\msg.txt") "restore json msg restored"
        Write-Host "PASS: restore JSON test passed"
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-JsonOutputAndIncludeRelated {
    $tmp = New-TestDir "ccfgtest-json"
    try {
        New-Item -ItemType Directory -Path (Join-Path $tmp "agents") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $tmp "agents.backup.demo") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $tmp "settings.json") -Value "{}" -NoNewline
        Set-Content -LiteralPath (Join-Path $tmp ".credentials.json") -Value "secret" -NoNewline

        $jsonOut = & $Bin check --config-dir $tmp --json | Out-String
        Assert-Contains $jsonOut '"config_home"' "check json"
        Assert-Contains $jsonOut '"runtime"' "check json"
        Assert-Contains $jsonOut '"related"' "check json"
        $obj = $jsonOut | ConvertFrom-Json
        Assert-True ($obj.config_home -eq $tmp) "check json config_home"
        Assert-True ($obj.runtime.Count -ge 10) "check json runtime count"
        Assert-True (($obj.runtime | Where-Object { $_.identifier -like "*.credentials.json" -and $_.exists -eq $true -and $_.safe_clean -eq $true }).Count -ge 1) "check json credentials entry"
        Assert-True (($obj.related | Where-Object { $_.identifier -like "*settings.json" -and $_.safe_clean -eq $false }).Count -ge 1) "check json related settings"

        $planOut = & $Bin clean --config-dir $tmp --include-related --dry-run | Out-String
        Assert-Contains $planOut "settings.json" "include-related plan"
        Assert-Contains $planOut "agents.backup.demo" "include-related plan"

        $cleanJson = & $Bin clean --config-dir $tmp --include-related --dry-run --json | Out-String
        $cleanObj = $cleanJson | ConvertFrom-Json
        Assert-True ($null -ne $cleanObj.cleanup_targets) "clean json cleanup_targets"
        Assert-True (($cleanObj.cleanup_targets | Where-Object { $_.identifier -like "*settings.json" }).Count -ge 1) "clean json settings target"
        Assert-True (($cleanObj.cleanup_results | Where-Object { $_.status -ne "skipped" }).Count -eq 0) "clean json skipped results"
        Write-Host "PASS: JSON and include-related test passed"
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-StrictRestoreRemovesExtras {
    $tmp = New-TestDir "ccfgtest-strict"
    $backup = New-TestDir "ccbackup-strict"
    try {
        New-Item -ItemType Directory -Path (Join-Path $tmp "projects\session1") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $tmp "projects\session1\msg.txt") -Value "hello" -NoNewline

        & $Bin clean --config-dir $tmp --backup-dir $backup -y | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $tmp "projects\session1") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $tmp "projects\session1\extra.txt") -Value "extra" -NoNewline
        & $Bin restore --backup-dir $backup --allow-unsafe-restore -y | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "strict restore failed"
        }

        Assert-Exists (Join-Path $tmp "projects\session1\msg.txt") "strict restore restored original file"
        Assert-NotExists (Join-Path $tmp "projects\session1\extra.txt") "strict restore removed extra file"
        Write-Host "PASS: strict restore test passed"
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-AllowUnsafePurgeOnTempDir {
    $tmp = New-TestDir "ccfgtest-allowpurge"
    try {
        New-Item -ItemType Directory -Path (Join-Path $tmp "projects") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $tmp ".credentials.json") -Value "secret" -NoNewline
        $out = & $Bin clean --config-dir $tmp --purge-config-home --allow-unsafe-purge -y | Out-String
        Assert-NotExists $tmp "unsafe purge removed directory"
        Write-Host "PASS: allow-unsafe-purge temp dir test passed"
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-WindowsRegistryAndCredentialRoundTrip {
    $regBase = "HKCU:\Software\Classes\claude-cli"
    $backup = New-TestDir "ccbackup-winstate"
    $configDir = New-TestDir "ccfgtest-winstate"
    $credTarget = "claude-cli-regression-$([System.Guid]::NewGuid().ToString('N'))"
    $cmdValue = '%LOCALAPPDATA%\claude-cli\claude.exe "%1"'
    $skipReason = $null

    if (Test-Path -LiteralPath $regBase) {
        $skipReason = "existing HKCU\Software\Classes\claude-cli found; skip to avoid touching real environment"
    }
    if (-not (Get-Command cmdkey -ErrorAction SilentlyContinue)) {
        $skipReason = "cmdkey not found; skip Windows credential test"
    }
    if ($skipReason) {
        Write-Host "SKIP: $skipReason"
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $configDir -Recurse -Force -ErrorAction SilentlyContinue
        return
    }

    try {
        New-Item -Path $regBase -Force | Out-Null
        New-Item -Path "$regBase\shell\open\command" -Force | Out-Null
        New-ItemProperty -Path $regBase -Name "URL Protocol" -Value "" -Force | Out-Null
        try {
            $rootKey = Get-Item -LiteralPath "Registry::$($regBase -replace '^HKCU:', 'HKEY_CURRENT_USER')"
            $rootKey.SetValue("", "Claude CLI URL", [Microsoft.Win32.RegistryValueKind]::String)
            $cmdKey = Get-Item -LiteralPath "Registry::$($regBase -replace '^HKCU:', 'HKEY_CURRENT_USER')\shell\open\command"
            $cmdKey.SetValue("", $cmdValue, [Microsoft.Win32.RegistryValueKind]::ExpandString)
        } catch {
            Write-Host "SKIP: registry write not permitted on this runner"
            return
        }

        & cmdkey /generic:$credTarget /user:regression-user /pass:regression-secret | Out-Null

        $checkJson = & $Bin check --config-dir $configDir --json | Out-String
        $checkObj = $checkJson | ConvertFrom-Json
        Assert-True (($checkObj.runtime | Where-Object { $_.kind -eq "registry" -and $_.identifier -eq "HKCU\Software\Classes\claude-cli" -and $_.exists -eq $true }).Count -ge 1) "registry key detection"
        Assert-True (($checkObj.runtime | Where-Object { $_.kind -eq "registry_value" -and $_.identifier -eq "HKCU\Software\Classes\claude-cli\shell\open\command [value:(Default)]" -and $_.exists -eq $true }).Count -ge 1) "registry value detection"
        Assert-True (($checkObj.runtime | Where-Object { $_.kind -eq "credential" -and $_.identifier -eq $credTarget -and $_.exists -eq $true }).Count -ge 1) "credential detection"

        & $Bin clean --config-dir $configDir --backup-dir $backup -y | Out-Null
        $manifest = Get-Content -LiteralPath (Join-Path $backup "manifest.tsv") -Raw
        Assert-Contains $manifest "REGVAL`t" "manifest registry value"
        Assert-Contains $manifest "WINCRED`t" "manifest credential"
        Assert-Contains $manifest "`t2`t" "manifest REG_EXPAND_SZ type"
        Assert-True (-not (Test-Path -LiteralPath $regBase)) "clean removed registry"

        $afterClean = & $Bin check --config-dir $configDir --json | Out-String | ConvertFrom-Json
        Assert-True (($afterClean.runtime | Where-Object { $_.kind -eq "credential" -and $_.identifier -eq $credTarget -and $_.exists -eq $true }).Count -eq 0) "clean removed credential"

        $restoreJson = & $Bin restore --backup-dir $backup --json -y | Out-String
        $restoreObj = $restoreJson | ConvertFrom-Json
        Assert-True (($restoreObj.cleanup_results | Where-Object { $_.status -ne "restored" }).Count -eq 0) "restore json restored state"

        Assert-True (Test-Path -LiteralPath $regBase) "restore registry"
        $restoredCmdKey = Get-Item -LiteralPath "Registry::$($regBase -replace '^HKCU:', 'HKEY_CURRENT_USER')\shell\open\command"
        $restoredValue = $restoredCmdKey.GetValue("", $null, 'DoNotExpandEnvironmentNames')
        $restoredKind = $restoredCmdKey.GetValueKind("")
        Assert-True ($restoredValue -eq $cmdValue) "restore registry value"
        Assert-True ($restoredKind -eq [Microsoft.Win32.RegistryValueKind]::ExpandString) "restore registry kind"

        $afterRestore = & $Bin check --config-dir $configDir --json | Out-String | ConvertFrom-Json
        Assert-True (($afterRestore.runtime | Where-Object { $_.kind -eq "credential" -and $_.identifier -eq $credTarget -and $_.exists -eq $true }).Count -ge 1) "restore credential"
        Write-Host "PASS: Windows registry/credential test passed"
    } finally {
        & cmdkey /delete:$credTarget 2>$null | Out-Null
        Remove-Item -LiteralPath $regBase -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $configDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Build-IfNeeded

if ($Tests -and $Tests.Count -gt 0) {
    foreach ($testName in $Tests) {
        & $testName
    }
    Write-Host "Selected tests passed."
    exit 0
}

Test-HelpFlags
Test-NoSystemCall
Test-PurgeGuard
Test-BackupAndRestoreFlow
Test-RestoreJsonOutput
Test-JsonOutputAndIncludeRelated
Test-StrictRestoreRemovesExtras
Test-AllowUnsafePurgeOnTempDir
Test-WindowsRegistryAndCredentialRoundTrip
Write-Host "All tests passed."
