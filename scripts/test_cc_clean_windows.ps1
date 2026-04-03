[CmdletBinding()]
param(
    [string[]]$Tests
)

$ErrorActionPreference = "Stop"

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
        throw "$Name: 未包含 [$Needle]"
    }
}

function Assert-Exists {
    param([string]$PathValue, [string]$Name)
    if (-not (Test-Path -LiteralPath $PathValue)) {
        throw "$Name: 不存在 $PathValue"
    }
}

function Assert-NotExists {
    param([string]$PathValue, [string]$Name)
    if (Test-Path -LiteralPath $PathValue) {
        throw "$Name: 仍存在 $PathValue"
    }
}

function Assert-True {
    param([bool]$Condition, [string]$Name)
    if (-not $Condition) {
        throw "$Name: 断言失败"
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
    Assert-Contains $out "--allow-unsafe-purge" "帮助输出"
    Assert-Contains $out "--allow-unsafe-restore" "帮助输出"
    Write-Host "PASS: 帮助输出包含危险开关"
}

function Test-NoSystemCall {
    $hasSystem = Select-String -Path $Src -SimpleMatch "system(" -Quiet
    if ($hasSystem) {
        throw "源码中仍存在 system()"
    }
    Write-Host "PASS: 源码中不存在 system()"
}

function Test-PurgeGuard {
    $tmp = New-TestDir "ccfgtest-purge"
    New-Item -ItemType Directory -Path (Join-Path $tmp "projects") | Out-Null
    Set-Content -LiteralPath (Join-Path $tmp ".credentials.json") -Value "secret" -NoNewline
    try {
        $out = & $Bin clean --config-dir $tmp --purge-config-home --dry-run 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) {
            throw "非默认目录 purge 应被拒绝"
        }
        Assert-Contains $out "拒绝执行 --purge-config-home" "purge 保护"
        Write-Host "PASS: purge 默认保护生效"
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
        Assert-Contains $cleanOut "[removed]" "clean 输出"
        Assert-Exists (Join-Path $backup "manifest.tsv") "manifest"
        Assert-Exists (Join-Path $backup "index.md") "index.md"
        Assert-Contains ((Get-Content -LiteralPath (Join-Path $backup "manifest.tsv") -Raw)) "VERSION`t2" "manifest 版本"
        Assert-NotExists (Join-Path $tmp ".credentials.json") "clean 删除 credentials"
        Assert-NotExists (Join-Path $tmp "projects") "clean 删除 projects"

        $restoreBlocked = & $Bin restore --backup-dir $backup -y 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) {
            throw "未加 allow-unsafe-restore 时应拒绝恢复到临时目录"
        }
        Assert-Contains $restoreBlocked "--allow-unsafe-restore" "restore 白名单保护"

        & $Bin restore --backup-dir $backup --allow-unsafe-restore -y | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "加 allow-unsafe-restore 后恢复失败"
        }
        Assert-Exists (Join-Path $tmp ".credentials.json") "restore 恢复 credentials"
        Assert-Exists (Join-Path $tmp "projects\session1\msg.txt") "restore 恢复 projects"
        Write-Host "PASS: clean/backup/restore 回归流程通过"
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
        Assert-True ($obj.cleanup_results.Count -ge 2) "restore json 数量"
        Assert-True (($obj.cleanup_results | Where-Object { $_.status -ne "restored" }).Count -eq 0) "restore json restored 状态"
        Assert-True (($obj.cleanup_results | Where-Object { $_.kind -eq "filesystem" -and $_.identifier -eq (Join-Path $tmp ".credentials.json") }).Count -ge 1) "restore json credentials 结果"
        Assert-Exists (Join-Path $tmp ".credentials.json") "restore json 恢复 credentials"
        Assert-Exists (Join-Path $tmp "projects\session1\msg.txt") "restore json 恢复 msg"
        Write-Host "PASS: restore JSON 回归通过"
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
        Assert-True ($obj.runtime.Count -ge 10) "check json runtime 数量"
        Assert-True (($obj.runtime | Where-Object { $_.identifier -eq (Join-Path $tmp ".credentials.json") -and $_.exists -eq $true -and $_.safe_clean -eq $true }).Count -ge 1) "check json credentials 条目"
        Assert-True (($obj.related | Where-Object { $_.identifier -eq (Join-Path $tmp "settings.json") -and $_.safe_clean -eq $false }).Count -ge 1) "check json related settings"

        $planOut = & $Bin clean --config-dir $tmp --include-related --dry-run | Out-String
        Assert-Contains $planOut "settings.json" "include-related 计划"
        Assert-Contains $planOut "agents.backup.demo" "include-related 计划"

        $cleanJson = & $Bin clean --config-dir $tmp --include-related --dry-run --json | Out-String
        $cleanObj = $cleanJson | ConvertFrom-Json
        Assert-True ($null -ne $cleanObj.cleanup_targets) "clean json cleanup_targets"
        Assert-True (($cleanObj.cleanup_targets | Where-Object { $_.identifier -eq (Join-Path $tmp "settings.json") }).Count -ge 1) "clean json settings target"
        Assert-True (($cleanObj.cleanup_results | Where-Object { $_.status -ne "skipped" }).Count -eq 0) "clean json skipped results"
        Write-Host "PASS: JSON 输出与 include-related 回归通过"
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
            throw "strict restore 失败"
        }

        Assert-Exists (Join-Path $tmp "projects\session1\msg.txt") "strict restore 恢复原文件"
        Assert-NotExists (Join-Path $tmp "projects\session1\extra.txt") "strict restore 删除额外文件"
        Write-Host "PASS: strict restore 回归通过"
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
        Assert-Contains $out "整个配置根目录" "unsafe purge 输出"
        Assert-NotExists $tmp "unsafe purge 删除目录"
        Write-Host "PASS: allow-unsafe-purge 临时目录回归通过"
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
        $skipReason = "检测到现有 HKCU:\\Software\\Classes\\claude-cli，跳过以避免覆盖真实环境"
    }
    if (-not (Get-Command cmdkey -ErrorAction SilentlyContinue)) {
        $skipReason = "未找到 cmdkey，跳过 Windows Credential 回归"
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
        $rootKey = Get-Item -LiteralPath "Registry::$($regBase -replace '^HKCU:', 'HKEY_CURRENT_USER')"
        $rootKey.SetValue("", "Claude CLI URL", [Microsoft.Win32.RegistryValueKind]::String)
        $cmdKey = Get-Item -LiteralPath "Registry::$($regBase -replace '^HKCU:', 'HKEY_CURRENT_USER')\shell\open\command"
        $cmdKey.SetValue("", $cmdValue, [Microsoft.Win32.RegistryValueKind]::ExpandString)

        & cmdkey /generic:$credTarget /user:regression-user /pass:regression-secret | Out-Null

        $checkJson = & $Bin check --config-dir $configDir --json | Out-String
        $checkObj = $checkJson | ConvertFrom-Json
        Assert-True (($checkObj.runtime | Where-Object { $_.kind -eq "registry" -and $_.identifier -eq "HKCU\Software\Classes\claude-cli" -and $_.exists -eq $true }).Count -ge 1) "registry key 检测"
        Assert-True (($checkObj.runtime | Where-Object { $_.kind -eq "registry_value" -and $_.identifier -eq "HKCU\Software\Classes\claude-cli\shell\open\command [value:(Default)]" -and $_.exists -eq $true }).Count -ge 1) "registry value 检测"
        Assert-True (($checkObj.runtime | Where-Object { $_.kind -eq "credential" -and $_.identifier -eq $credTarget -and $_.exists -eq $true }).Count -ge 1) "credential 检测"

        & $Bin clean --config-dir $configDir --backup-dir $backup -y | Out-Null
        $manifest = Get-Content -LiteralPath (Join-Path $backup "manifest.tsv") -Raw
        Assert-Contains $manifest "REGVAL`t" "manifest 注册表值"
        Assert-Contains $manifest "WINCRED`t" "manifest credential"
        Assert-Contains $manifest "`t2`t" "manifest REG_EXPAND_SZ 类型"
        Assert-True (-not (Test-Path -LiteralPath $regBase)) "clean 删除 registry"

        $afterClean = & $Bin check --config-dir $configDir --json | Out-String | ConvertFrom-Json
        Assert-True (($afterClean.runtime | Where-Object { $_.kind -eq "credential" -and $_.identifier -eq $credTarget -and $_.exists -eq $true }).Count -eq 0) "clean 删除 credential"

        $restoreJson = & $Bin restore --backup-dir $backup --json -y | Out-String
        $restoreObj = $restoreJson | ConvertFrom-Json
        Assert-True (($restoreObj.cleanup_results | Where-Object { $_.status -ne "restored" }).Count -eq 0) "restore json restored 状态"

        Assert-True (Test-Path -LiteralPath $regBase) "restore 恢复 registry"
        $restoredCmdKey = Get-Item -LiteralPath "Registry::$($regBase -replace '^HKCU:', 'HKEY_CURRENT_USER')\shell\open\command"
        $restoredValue = $restoredCmdKey.GetValue("", $null, 'DoNotExpandEnvironmentNames')
        $restoredKind = $restoredCmdKey.GetValueKind("")
        Assert-True ($restoredValue -eq $cmdValue) "restore 恢复 registry 值"
        Assert-True ($restoredKind -eq [Microsoft.Win32.RegistryValueKind]::ExpandString) "restore 恢复 registry 类型"

        $afterRestore = & $Bin check --config-dir $configDir --json | Out-String | ConvertFrom-Json
        Assert-True (($afterRestore.runtime | Where-Object { $_.kind -eq "credential" -and $_.identifier -eq $credTarget -and $_.exists -eq $true }).Count -ge 1) "restore 恢复 credential"
        Write-Host "PASS: Windows 注册表/凭据回归通过"
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
    Write-Host "指定回归测试通过。"
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
Write-Host "全部回归测试通过。"
