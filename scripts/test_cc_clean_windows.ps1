[CmdletBinding()]
param(
    [string[]]$Tests
)

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $RootDir "build\Release\cc-clean.exe"
$Src = Join-Path $RootDir "csrc"

function Test-StringContains {
    param([AllowNull()][string]$Haystack,[string]$Needle)
    return ([string]$Haystack).IndexOf([string]$Needle, [System.StringComparison]::Ordinal) -ge 0
}

function Assert-Contains {
    param([AllowNull()][string]$Haystack,[string]$Needle,[string]$Name)
    if (-not (Test-StringContains -Haystack $Haystack -Needle $Needle)) {
        throw ("{0}: missing [{1}]" -f $Name, $Needle)
    }
}

function Assert-Exists {
    param([string]$PathValue,[string]$Name)
    if (-not (Test-Path -LiteralPath $PathValue)) {
        throw ("{0}: missing path {1}" -f $Name, $PathValue)
    }
}

function Assert-NotExists {
    param([string]$PathValue,[string]$Name)
    if (Test-Path -LiteralPath $PathValue) {
        throw ("{0}: path still exists {1}" -f $Name, $PathValue)
    }
}

function Assert-True {
    param([bool]$Condition,[string]$Name)
    if (-not $Condition) {
        throw ("{0}: assertion failed" -f $Name)
    }
}

function Build-IfNeeded {
    if (-not (Test-Path -LiteralPath $Bin)) {
        cmake -S $RootDir -B (Join-Path $RootDir "build")
        cmake --build (Join-Path $RootDir "build") --config Release
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
    Write-Host "PASS: help output contains unsafe flags"
}

function Test-NoSystemCall {
    $matches = Get-ChildItem -Path $Src -Filter *.c | Select-String -Pattern '(^|[^_A-Za-z0-9])(system|popen|_popen)\('
    if (@($matches).Count -gt 0) {
        throw "source still contains system()/popen()/_popen()"
    }
    Write-Host "PASS: source does not contain system()/popen()/_popen()"
}

function Test-PurgeGuard {
    $tmp = New-TestDir "ccfgtest-purge"
    New-Item -ItemType Directory -Path (Join-Path $tmp "projects") | Out-Null
    Set-Content -LiteralPath (Join-Path $tmp ".credentials.json") -Value "secret" -NoNewline
    try {
        $out = ""
        $failed = $false
        try {
            $out = & $Bin clean --config-dir $tmp --purge-config-home --dry-run 2>&1 | Out-String
            $failed = ($LASTEXITCODE -ne 0)
        } catch {
            $out = ($_ | Out-String)
            $failed = $true
        }
        if (-not $failed) {
            throw "non-default purge should be rejected"
        }
        Assert-Contains $out "--purge-config-home" "purge guard"
        Write-Host "PASS: purge guard works"
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
        Assert-NotExists (Join-Path $tmp ".credentials.json") "clean credentials"
        Assert-NotExists (Join-Path $tmp "projects") "clean projects"

        $restoreBlocked = & $Bin restore --backup-dir $backup -y 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) {
            throw "restore to temp dir should require allow-unsafe-restore"
        }
        Assert-Contains $restoreBlocked "--allow-unsafe-restore" "restore whitelist"

        & $Bin restore --backup-dir $backup --allow-unsafe-restore -y | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "restore failed with allow-unsafe-restore"
        }
        Assert-Exists (Join-Path $tmp ".credentials.json") "restore credentials"
        Assert-Exists (Join-Path $tmp "projects\session1\msg.txt") "restore projects"
        Write-Host "PASS: clean/backup/restore flow works"
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
        Assert-True (($obj.cleanup_results | Where-Object { $_.status -ne "restored" }).Count -eq 0) "restore json statuses"
        Assert-Exists (Join-Path $tmp ".credentials.json") "restore json credentials"
        Assert-Exists (Join-Path $tmp "projects\session1\msg.txt") "restore json msg"
        Write-Host "PASS: restore JSON works"
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

        $planOut = & $Bin clean --config-dir $tmp --include-related --dry-run | Out-String
        Assert-Contains $planOut "settings.json" "include-related plan"
        Assert-Contains $planOut "agents.backup.demo" "include-related plan"

        $cleanJson = & $Bin clean --config-dir $tmp --include-related --dry-run --json | Out-String
        $cleanObj = $cleanJson | ConvertFrom-Json
        Assert-True ($null -ne $cleanObj.cleanup_targets) "clean json targets"
        Assert-True (($cleanObj.cleanup_results | Where-Object { $_.status -ne "skipped" }).Count -eq 0) "clean json skipped"
        Write-Host "PASS: JSON output and include-related work"
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

        Assert-Exists (Join-Path $tmp "projects\session1\msg.txt") "strict restore original file"
        Assert-NotExists (Join-Path $tmp "projects\session1\extra.txt") "strict restore extra file"
        Write-Host "PASS: strict restore removes extras"
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
        if ($LASTEXITCODE -ne 0) { throw 'unsafe purge should succeed' }
        Assert-NotExists $tmp "unsafe purge removed dir"
        Write-Host "PASS: allow-unsafe-purge works on temp dir"
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-RestoreRejectsDotDotTarget {
    $homeDir = New-TestDir "ccfgtest-dotdot-home"
    $backup = New-TestDir "ccbackup-dotdot"
    $oldUserProfile = $env:USERPROFILE
    try {
        $env:USERPROFILE = $homeDir
        New-Item -ItemType Directory -Path (Join-Path $backup 'files') -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $backup 'files\payload.txt') -Value 'payload' -NoNewline
        $orig = Join-Path $homeDir '.claude\..\escape.txt'
        Set-Content -LiteralPath (Join-Path $backup 'manifest.tsv') -Value ("VERSION`t2`nFS`t{0}`tfiles/payload.txt`tfile`n" -f $orig) -NoNewline

        $out = & $Bin restore --backup-dir $backup -y 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) { throw 'dotdot restore target should be rejected' }
        Assert-NotExists (Join-Path $homeDir 'escape.txt') 'dotdot restore target outside whitelist'
        Write-Host 'PASS: restore rejects dotdot target'
    } finally {
        $env:USERPROFILE = $oldUserProfile
        Remove-Item -LiteralPath $homeDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-RestoreRejectsBackupEscape {
    $homeDir = New-TestDir 'ccfgtest-backup-escape-home'
    $backupRoot = New-TestDir 'ccbackup-escape-root'
    $backup = Join-Path $backupRoot 'backup'
    $oldUserProfile = $env:USERPROFILE
    try {
        $env:USERPROFILE = $homeDir
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $backupRoot 'outside.txt') -Value 'payload' -NoNewline
        $orig = Join-Path $homeDir '.claude\restore.txt'
        Set-Content -LiteralPath (Join-Path $backup 'manifest.tsv') -Value ("VERSION`t2`nFS`t{0}`t../outside.txt`tfile`n" -f $orig) -NoNewline

        $out = & $Bin restore --backup-dir $backup -y 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) { throw 'backup escape rel should be rejected' }
        Assert-NotExists (Join-Path $homeDir '.claude\restore.txt') 'backup escape target'
        Write-Host 'PASS: restore rejects backup escape rel'
    } finally {
        $env:USERPROFILE = $oldUserProfile
        Remove-Item -LiteralPath $homeDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backupRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-RestoreRejectsOverlongManifestLine {
    $homeDir = New-TestDir 'ccfgtest-longline-home'
    $backup = New-TestDir 'ccbackup-longline'
    $oldUserProfile = $env:USERPROFILE
    try {
        $env:USERPROFILE = $homeDir
        $longPayload = 'A' * 17000
        Set-Content -LiteralPath (Join-Path $backup 'manifest.tsv') -Value ("VERSION`t2`nBROKEN`t{0}`n" -f $longPayload) -NoNewline

        $out = & $Bin restore --backup-dir $backup -y 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0) { throw 'overlong manifest line should be rejected' }
        Write-Host 'PASS: restore rejects overlong manifest line'
    } finally {
        $env:USERPROFILE = $oldUserProfile
        Remove-Item -LiteralPath $homeDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-ExtendedRuntimeInstallArtifacts {
    $homeDir = New-TestDir "ccfgtest-extended-home"
    $backup = New-TestDir "ccbackup-extended"
    $configDir = Join-Path $homeDir ".claude"
    $oldUserProfile = $env:USERPROFILE
    $oldAppData = $env:APPDATA
    $oldLocalAppData = $env:LOCALAPPDATA
    $oldXdgData = $env:XDG_DATA_HOME
    $oldXdgCache = $env:XDG_CACHE_HOME
    $oldXdgState = $env:XDG_STATE_HOME
    try {
        $env:USERPROFILE = $homeDir
        $env:APPDATA = Join-Path $homeDir "AppData\Roaming"
        $env:LOCALAPPDATA = Join-Path $homeDir "AppData\Local"
        $env:XDG_DATA_HOME = Join-Path $homeDir ".local\share"
        $env:XDG_CACHE_HOME = Join-Path $homeDir ".cache"
        $env:XDG_STATE_HOME = Join-Path $homeDir ".local\state"

        New-Item -ItemType Directory -Path (Join-Path $configDir "local") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "uploads\u1") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "sessions\s1") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "startup-perf") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "backups") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "plans") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "cache") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "traces") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "ide") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "shell-snapshots") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "jobs") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "tasks") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $configDir "teams") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $configDir "local\claude") -Value "wrapper" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "history.jsonl") -Value "history" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "uploads\u1\file.txt") -Value "upload" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "server-sessions.json") -Value "{}" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "sessions\s1\state.json") -Value "{}" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "mcp-needs-auth-cache.json") -Value "{}" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "usage-data") -Value "{}" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "startup-perf\run1.txt") -Value "perf" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "backups\state.txt") -Value "bak" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "plans\p1.txt") -Value "plan" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "cache\changelog.md") -Value "cache" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "traces\t1.json") -Value "{}" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "ide\state.json") -Value "{}" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "shell-snapshots\s1.txt") -Value "snap" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "jobs\j1.json") -Value "{}" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "tasks\t1.json") -Value "{}" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "teams\team1.json") -Value "{}" -NoNewline
        Set-Content -LiteralPath (Join-Path $configDir "completion.bash") -Value "completion" -NoNewline

        New-Item -ItemType Directory -Path (Join-Path $homeDir ".local\share\claude\versions") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $homeDir ".cache\claude\staging") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $homeDir ".local\state\claude\locks") -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $homeDir ".local\bin") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $homeDir ".local\share\claude\versions\1.0.0") -Value "bin" -NoNewline
        Set-Content -LiteralPath (Join-Path $homeDir ".cache\claude\staging\1.0.0") -Value "stage" -NoNewline
        Set-Content -LiteralPath (Join-Path $homeDir ".local\state\claude\locks\1.0.0") -Value "lock" -NoNewline
        Set-Content -LiteralPath (Join-Path $homeDir ".local\bin\claude.exe") -Value "exec" -NoNewline

        $nativeHostDir = Join-Path $env:APPDATA "Claude Code\ChromeNativeHost"
        New-Item -ItemType Directory -Path $nativeHostDir -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $nativeHostDir "com.anthropic.claude_code_browser_extension.json") -Value "{}" -NoNewline

        $cleanOut = & $Bin clean --backup-dir $backup -y | Out-String
        Assert-Contains $cleanOut "history.jsonl" "extended clean output"
        Assert-NotExists (Join-Path $configDir "history.jsonl") "extended history removed"
        Assert-NotExists (Join-Path $homeDir ".local\bin\claude.exe") "extended native installer removed"

        $null = & $Bin restore --backup-dir $backup -y | Out-String
        Assert-True ($LASTEXITCODE -eq 0) "extended restore exit code"
        Assert-Exists (Join-Path $configDir "history.jsonl") "extended history restored"
        Assert-Exists (Join-Path $homeDir ".local\bin\claude.exe") "extended native installer restored"
        Write-Host "PASS: extended install/runtime artifacts work"
    } finally {
        $env:USERPROFILE = $oldUserProfile
        $env:APPDATA = $oldAppData
        $env:LOCALAPPDATA = $oldLocalAppData
        $env:XDG_DATA_HOME = $oldXdgData
        $env:XDG_CACHE_HOME = $oldXdgCache
        $env:XDG_STATE_HOME = $oldXdgState
        Remove-Item -LiteralPath $homeDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-ShellIdeAndNpmArtifacts {
    $homeDir = New-TestDir "ccfgtest-shellide"
    $backup = New-TestDir "ccbackup-shellide"
    $configDir = Join-Path $homeDir ".claude"
    $npmPrefix = Join-Path $homeDir "npm-global"
    $oldUserProfile = $env:USERPROFILE
    $oldAppData = $env:APPDATA
    $oldLocalAppData = $env:LOCALAPPDATA
    $oldXdgConfig = $env:XDG_CONFIG_HOME
    $oldNpmPrefix = $env:NPM_CONFIG_PREFIX
    try {
        $env:USERPROFILE = $homeDir
        $env:APPDATA = Join-Path $homeDir "AppData\Roaming"
        $env:LOCALAPPDATA = Join-Path $homeDir "AppData\Local"
        $env:XDG_CONFIG_HOME = Join-Path $homeDir ".config"
        $env:NPM_CONFIG_PREFIX = $npmPrefix

        New-Item -ItemType Directory -Path (Join-Path $configDir "local") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $configDir "local\claude") -Value "wrapper" -NoNewline
        New-Item -ItemType Directory -Path (Join-Path $homeDir ".config\fish") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $homeDir ".zshrc") -Value @"
# Claude Code shell completions
[[ -f "$configDir/completion.zsh" ]] && source "$configDir/completion.zsh"
alias claude="$configDir/local/claude"
alias keepclaude="echo keep"
"@ -NoNewline
        Set-Content -LiteralPath (Join-Path $homeDir ".bashrc") -Value @"
# Claude Code shell completions
[ -f "$configDir/completion.bash" ] && source "$configDir/completion.bash"
"@ -NoNewline
        Set-Content -LiteralPath (Join-Path $homeDir ".config\fish\config.fish") -Value @"
# Claude Code shell completions
[ -f "$configDir/completion.fish" ] && source "$configDir/completion.fish"
"@ -NoNewline

        New-Item -ItemType Directory -Path (Join-Path $homeDir ".vscode\extensions\anthropic.claude-code-1.0.0") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $homeDir ".vscode\extensions\anthropic.claude-code-1.0.0\package.json") -Value "{}" -NoNewline
        New-Item -ItemType Directory -Path (Join-Path $env:APPDATA "JetBrains\PyCharm2024.1\plugins\claude-code-jetbrains-plugin") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $env:APPDATA "JetBrains\PyCharm2024.1\plugins\claude-code-jetbrains-plugin\plugin.txt") -Value "plugin" -NoNewline

        New-Item -ItemType Directory -Path (Join-Path $npmPrefix "node_modules\@anthropic-ai\claude-code") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $npmPrefix "claude.cmd") -Value "cmd" -NoNewline
        Set-Content -LiteralPath (Join-Path $npmPrefix "claude.ps1") -Value "ps1" -NoNewline
        Set-Content -LiteralPath (Join-Path $npmPrefix "claude") -Value "exe" -NoNewline
        Set-Content -LiteralPath (Join-Path $npmPrefix "node_modules\@anthropic-ai\claude-code\package.json") -Value "{}" -NoNewline

        $cleanOut = & $Bin clean --backup-dir $backup -y | Out-String
        Assert-Contains $cleanOut ".zshrc" "shell clean output"
        $zshContent = [string](Get-Content -LiteralPath (Join-Path $homeDir ".zshrc") -Raw -ErrorAction SilentlyContinue)
        Assert-Contains $zshContent 'alias keepclaude="echo keep"' "custom alias preserved"
        if (Test-StringContains -Haystack $zshContent -Needle 'alias claude=') { throw "default alias should be removed from zshrc" }
        $bashContent = [string](Get-Content -LiteralPath (Join-Path $homeDir ".bashrc") -Raw -ErrorAction SilentlyContinue)
        if (Test-StringContains -Haystack $bashContent -Needle 'completion.bash') { throw "bash completion should be removed" }
        $fishContent = [string](Get-Content -LiteralPath (Join-Path $homeDir ".config\fish\config.fish") -Raw -ErrorAction SilentlyContinue)
        if (Test-StringContains -Haystack $fishContent -Needle 'completion.fish') { throw "fish completion should be removed" }
        Assert-NotExists (Join-Path $homeDir ".vscode\extensions\anthropic.claude-code-1.0.0") "vscode extension removed"
        Assert-NotExists (Join-Path $npmPrefix "claude.cmd") "npm cmd removed"
        Assert-NotExists (Join-Path $npmPrefix "node_modules\@anthropic-ai\claude-code") "npm package removed"

        $null = & $Bin restore --backup-dir $backup -y | Out-String
        $restoredZsh = [string](Get-Content -LiteralPath (Join-Path $homeDir ".zshrc") -Raw -ErrorAction SilentlyContinue)
        Assert-Contains $restoredZsh "Claude Code shell completions" "zsh restored"
        Assert-Contains $restoredZsh 'alias claude="' "alias restored"
        Assert-Contains $restoredZsh 'local/claude"' "alias target restored"
        Write-Host "PASS: shell/IDE/npm artifacts work"
    } finally {
        $env:USERPROFILE = $oldUserProfile
        $env:APPDATA = $oldAppData
        $env:LOCALAPPDATA = $oldLocalAppData
        $env:XDG_CONFIG_HOME = $oldXdgConfig
        $env:NPM_CONFIG_PREFIX = $oldNpmPrefix
        Remove-Item -LiteralPath $homeDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-WindowsRegistryAndCredentialRoundTrip {
    $regBase = "HKCU:\Software\Classes\claude-cli"
    $backup = New-TestDir "ccbackup-winstate"
    $configDir = New-TestDir "ccfgtest-winstate"
    $credTarget = "claude-cli-regression-$([System.Guid]::NewGuid().ToString('N'))"
    $cmdValue = '%LOCALAPPDATA%\claude-cli\claude.exe "%1"'
    $skipReason = $null

    if (Test-Path -LiteralPath $regBase) { $skipReason = "existing HKCU:\Software\Classes\claude-cli found" }
    if (-not (Get-Command cmdkey -ErrorAction SilentlyContinue)) { $skipReason = "cmdkey not found" }
    if ($skipReason) {
        Write-Host "SKIP: $skipReason"
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $configDir -Recurse -Force -ErrorAction SilentlyContinue
        return
    }

    try {
        try {
            New-Item -Path $regBase -Force | Out-Null
            New-Item -Path "$regBase\shell\open\command" -Force | Out-Null
            New-ItemProperty -Path $regBase -Name "URL Protocol" -Value "" -Force | Out-Null
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
        Assert-True (($checkObj.runtime | Where-Object { $_.kind -eq "registry" -and $_.identifier -eq "HKCU\Software\Classes\claude-cli" -and $_.exists -eq $true }).Count -ge 1) "registry key detected"
        Assert-True (($checkObj.runtime | Where-Object { $_.kind -eq "registry_value" -and $_.identifier -eq "HKCU\Software\Classes\claude-cli\shell\open\command [value:(Default)]" -and $_.exists -eq $true }).Count -ge 1) "registry value detected"
        Assert-True (($checkObj.runtime | Where-Object { $_.kind -eq "credential" -and $_.identifier -eq $credTarget -and $_.exists -eq $true }).Count -ge 1) "credential detected"

        & $Bin clean --config-dir $configDir --backup-dir $backup -y | Out-Null
        $manifest = Get-Content -LiteralPath (Join-Path $backup "manifest.tsv") -Raw
        Assert-Contains $manifest "REGVAL`t" "manifest registry value"
        Assert-Contains $manifest "WINCRED`t" "manifest credential"
        Assert-Contains $manifest "`t2`t" "manifest REG_EXPAND_SZ"
        Assert-True (-not (Test-Path -LiteralPath $regBase)) "clean removed registry"

        $afterClean = & $Bin check --config-dir $configDir --json | Out-String | ConvertFrom-Json
        Assert-True (($afterClean.runtime | Where-Object { $_.kind -eq "credential" -and $_.identifier -eq $credTarget -and $_.exists -eq $true }).Count -eq 0) "clean removed credential"

        $restoreJson = & $Bin restore --backup-dir $backup --json -y | Out-String
        $restoreObj = $restoreJson | ConvertFrom-Json
        Assert-True (($restoreObj.cleanup_results | Where-Object { $_.status -ne "restored" }).Count -eq 0) "restore json statuses"

        Assert-True (Test-Path -LiteralPath $regBase) "restore recreated registry"
        $restoredCmdKey = Get-Item -LiteralPath "Registry::$($regBase -replace '^HKCU:', 'HKEY_CURRENT_USER')\shell\open\command"
        $restoredValue = $restoredCmdKey.GetValue("", $null, 'DoNotExpandEnvironmentNames')
        $restoredKind = $restoredCmdKey.GetValueKind("")
        Assert-True ($restoredValue -eq $cmdValue) "restore registry value"
        Assert-True ($restoredKind -eq [Microsoft.Win32.RegistryValueKind]::ExpandString) "restore registry kind"

        $afterRestore = & $Bin check --config-dir $configDir --json | Out-String | ConvertFrom-Json
        Assert-True (($afterRestore.runtime | Where-Object { $_.kind -eq "credential" -and $_.identifier -eq $credTarget -and $_.exists -eq $true }).Count -ge 1) "restore credential"
        Write-Host "PASS: Windows registry/credential round trip works"
    } finally {
        & cmdkey /delete:$credTarget 2>$null | Out-Null
        Remove-Item -LiteralPath $regBase -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $configDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Build-IfNeeded

if ($Tests -and $Tests.Count -gt 0) {
    foreach ($testName in $Tests) { & $testName }
    Write-Host "Selected regression tests passed."
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
Test-RestoreRejectsDotDotTarget
Test-RestoreRejectsBackupEscape
Test-RestoreRejectsOverlongManifestLine
Test-ExtendedRuntimeInstallArtifacts
Test-ShellIdeAndNpmArtifacts
Test-WindowsRegistryAndCredentialRoundTrip
Write-Host "All regression tests passed."
