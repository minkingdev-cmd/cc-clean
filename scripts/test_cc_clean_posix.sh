#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT_DIR}/build/cc-clean"
SRC="${ROOT_DIR}/csrc"

pass() {
  printf 'PASS: %s\n' "$1"
}

fail() {
  printf 'FAIL: %s\n' "$1" >&2
  exit 1
}

assert_contains() {
  local haystack="$1"
  local needle="$2"
  local name="$3"
  if [[ "$haystack" != *"$needle"* ]]; then
    fail "$name: 未包含 [$needle]"
  fi
}

assert_file_exists() {
  local path="$1"
  local name="$2"
  [[ -e "$path" ]] || fail "$name: 文件不存在 $path"
}

assert_file_not_exists() {
  local path="$1"
  local name="$2"
  [[ ! -e "$path" ]] || fail "$name: 文件仍存在 $path"
}

assert_json_python() {
  local json_text="$1"
  local expr="$2"
  local name="$3"
  JSON_INPUT="$json_text" python3 - "$expr" "$name" <<'PY'
import json, os, sys
expr = sys.argv[1]
name = sys.argv[2]
data = json.loads(os.environ["JSON_INPUT"])
safe_builtins = {"len": len, "isinstance": isinstance, "list": list, "any": any, "all": all}
ok = eval(expr, {"__builtins__": safe_builtins}, {"data": data})
if not ok:
    print(f"FAIL: {name}: JSON 断言失败: {expr}", file=sys.stderr)
    sys.exit(1)
PY
}

build_if_needed() {
  if [[ ! -x "$BIN" ]]; then
    cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build"
    cmake --build "$ROOT_DIR/build"
  fi
}

test_help_flags() {
  local out
  out="$($BIN --help)"
  assert_contains "$out" "--allow-unsafe-purge" "帮助输出"
  assert_contains "$out" "--allow-unsafe-restore" "帮助输出"
  pass "帮助输出包含危险开关"
}

test_no_system_call() {
  if find "$SRC" -name '*.c' -print0 | xargs -0 grep -nE '(^|[^_[:alnum:]])(system|popen|_popen)\(' >/dev/null 2>&1; then
    fail "源码中仍存在 system()/popen()/_popen()"
  fi
  pass "源码中不存在 system()/popen()/_popen()"
}

test_purge_guard() {
  local tmpdir out
  tmpdir="$(mktemp -d /tmp/ccfgtest.purge.XXXXXX)"
  mkdir -p "$tmpdir/projects"
  printf 'secret' >"$tmpdir/.credentials.json"
  set +e
  out="$($BIN clean --config-dir "$tmpdir" --purge-config-home --dry-run 2>&1)"
  local rc=$?
  set -e
  rm -rf "$tmpdir"
  [[ $rc -ne 0 ]] || fail "非默认目录 purge 应被拒绝"
  assert_contains "$out" "拒绝执行 --purge-config-home" "purge 保护"
  pass "purge 默认保护生效"
}

test_backup_and_restore_flow() {
  local tmpdir backup clean_out restore_out
  tmpdir="$(mktemp -d /tmp/ccfgtest.flow.XXXXXX)"
  backup="$(mktemp -d /tmp/ccbackup.flow.XXXXXX)"
  mkdir -p "$tmpdir/projects/session1"
  printf 'secret' >"$tmpdir/.credentials.json"
  printf 'hello' >"$tmpdir/projects/session1/msg.txt"

  clean_out="$($BIN clean --config-dir "$tmpdir" --backup-dir "$backup" -y)"
  assert_contains "$clean_out" "[removed] file" "clean 输出"
  assert_contains "$clean_out" "[removed] dir" "clean 输出"
  assert_file_exists "$backup/manifest.tsv" "manifest"
  assert_file_exists "$backup/index.md" "index.md"
  assert_contains "$(cat "$backup/manifest.tsv")" $'VERSION\t2' "manifest 版本"
  assert_file_not_exists "$tmpdir/.credentials.json" "clean 删除 credentials"
  assert_file_not_exists "$tmpdir/projects" "clean 删除 projects"

  set +e
  restore_out="$($BIN restore --backup-dir "$backup" -y 2>&1)"
  local rc=$?
  set -e
  [[ $rc -ne 0 ]] || fail "未加 allow-unsafe-restore 时应拒绝恢复到 /tmp"
  assert_contains "$restore_out" "--allow-unsafe-restore" "restore 白名单保护"

  $BIN restore --backup-dir "$backup" --allow-unsafe-restore -y >/dev/null
  assert_file_exists "$tmpdir/.credentials.json" "restore 恢复 credentials"
  assert_file_exists "$tmpdir/projects/session1/msg.txt" "restore 恢复 projects"

  rm -rf "$tmpdir" "$backup"
  pass "clean/backup/restore 回归流程通过"
}

test_restore_json_output() {
  local tmpdir backup json_out
  tmpdir="$(mktemp -d /tmp/ccfgtest.restorejson.XXXXXX)"
  backup="$(mktemp -d /tmp/ccbackup.restorejson.XXXXXX)"
  mkdir -p "$tmpdir/projects/session1"
  printf 'secret' >"$tmpdir/.credentials.json"
  printf 'hello' >"$tmpdir/projects/session1/msg.txt"

  $BIN clean --config-dir "$tmpdir" --backup-dir "$backup" -y >/dev/null
  json_out="$($BIN restore --backup-dir "$backup" --allow-unsafe-restore --json -y)"
  assert_json_python "$json_out" "'cleanup_results' in data and isinstance(data['cleanup_results'], list) and len(data['cleanup_results']) >= 2" "restore json cleanup_results"
  assert_json_python "$json_out" "all(x['status'] == 'restored' for x in data['cleanup_results'])" "restore json restored 状态"
  assert_json_python "$json_out" "any(x['kind'] == 'filesystem' and x['identifier'] == '$tmpdir/.credentials.json' for x in data['cleanup_results'])" "restore json credentials 结果"
  assert_file_exists "$tmpdir/.credentials.json" "restore json 恢复 credentials"
  assert_file_exists "$tmpdir/projects/session1/msg.txt" "restore json 恢复 msg"

  rm -rf "$tmpdir" "$backup"
  pass "restore JSON 回归通过"
}

test_json_output_and_include_related() {
  local tmpdir json_out plan_out clean_json
  tmpdir="$(mktemp -d /tmp/ccfgtest.json.XXXXXX)"
  mkdir -p "$tmpdir/agents" "$tmpdir/agents.backup.demo"
  printf '{}' >"$tmpdir/settings.json"
  printf 'secret' >"$tmpdir/.credentials.json"

  json_out="$($BIN check --config-dir "$tmpdir" --json)"
  assert_contains "$json_out" '"config_home"' "check json"
  assert_contains "$json_out" '"runtime"' "check json"
  assert_contains "$json_out" '"related"' "check json"
  assert_json_python "$json_out" "data['config_home'] == '$tmpdir'" "check json config_home"
  assert_json_python "$json_out" "isinstance(data['runtime'], list) and len(data['runtime']) >= 10" "check json runtime 数量"
  assert_json_python "$json_out" "any(x['identifier'] == '$tmpdir/.credentials.json' and x['exists'] is True and x['safe_clean'] is True for x in data['runtime'])" "check json credentials 条目"
  assert_json_python "$json_out" "any(x['identifier'] == '$tmpdir/settings.json' and x['safe_clean'] is False for x in data['related'])" "check json related settings"

  plan_out="$($BIN clean --config-dir "$tmpdir" --include-related --dry-run)"
  assert_contains "$plan_out" 'settings.json' "include-related 计划"
  assert_contains "$plan_out" 'agents.backup.demo' "include-related 计划"
  assert_contains "$plan_out" '[file]' "include-related 计划"

  clean_json="$($BIN clean --config-dir "$tmpdir" --include-related --dry-run --json)"
  assert_json_python "$clean_json" "'cleanup_targets' in data and isinstance(data['cleanup_targets'], list)" "clean json cleanup_targets"
  assert_json_python "$clean_json" "any(x['identifier'] == '$tmpdir/settings.json' for x in data['cleanup_targets'])" "clean json settings target"
  assert_json_python "$clean_json" "'cleanup_results' in data and all(x['status'] == 'skipped' for x in data['cleanup_results'])" "clean json skipped results"

  rm -rf "$tmpdir"
  pass "JSON 输出与 include-related 回归通过"
}

test_strict_restore_removes_extras() {
  local tmpdir backup
  tmpdir="$(mktemp -d /tmp/ccfgtest.strict.XXXXXX)"
  backup="$(mktemp -d /tmp/ccbackup.strict.XXXXXX)"
  mkdir -p "$tmpdir/projects/session1"
  printf 'hello' >"$tmpdir/projects/session1/msg.txt"

  $BIN clean --config-dir "$tmpdir" --backup-dir "$backup" -y >/dev/null
  mkdir -p "$tmpdir/projects/session1"
  printf 'extra' >"$tmpdir/projects/session1/extra.txt"
  $BIN restore --backup-dir "$backup" --allow-unsafe-restore -y >/dev/null

  assert_file_exists "$tmpdir/projects/session1/msg.txt" "strict restore 恢复原文件"
  assert_file_not_exists "$tmpdir/projects/session1/extra.txt" "strict restore 删除额外文件"

  rm -rf "$tmpdir" "$backup"
  pass "strict restore 回归通过"
}

test_allow_unsafe_purge_on_temp_dir() {
  local tmpdir out
  tmpdir="$(mktemp -d /tmp/ccfgtest.allowpurge.XXXXXX)"
  mkdir -p "$tmpdir/projects"
  printf 'secret' >"$tmpdir/.credentials.json"
  out="$($BIN clean --config-dir "$tmpdir" --purge-config-home --allow-unsafe-purge -y)"
  assert_contains "$out" "整个配置根目录" "unsafe purge 输出"
  assert_file_not_exists "$tmpdir" "unsafe purge 删除目录"
  pass "allow-unsafe-purge 临时目录回归通过"
}

test_restore_rejects_dotdot_target() {
  local home backup out rc orig
  home="$(mktemp -d /tmp/ccfgtest.dotdot-home.XXXXXX)"
  backup="$(mktemp -d /tmp/ccbackup.dotdot.XXXXXX)"
  mkdir -p "$backup/files"
  printf 'payload' >"$backup/files/payload.txt"
  orig="$home/.claude/../escape.txt"
  cat >"$backup/manifest.tsv" <<EOF
VERSION	2
FS	$orig	files/payload.txt	file
EOF

  set +e
  out="$(HOME="$home" "$BIN" restore --backup-dir "$backup" -y 2>&1)"
  rc=$?
  set -e
  [[ $rc -ne 0 ]] || fail "dotdot restore target 应被拒绝"
  assert_contains "$out" "恢复目标不在允许范围内" "dotdot restore target"
  assert_file_not_exists "$home/escape.txt" "dotdot restore target 未写出到白名单外"

  rm -rf "$home" "$backup"
  pass "restore 拒绝 .. 目标路径绕过"
}

test_restore_rejects_backup_escape() {
  local home backup_root backup out rc orig
  home="$(mktemp -d /tmp/ccfgtest.escape-home.XXXXXX)"
  backup_root="$(mktemp -d /tmp/ccbackup.escape-root.XXXXXX)"
  backup="$backup_root/backup"
  mkdir -p "$backup"
  printf 'payload' >"$backup_root/outside.txt"
  orig="$home/.claude/restore.txt"
  cat >"$backup/manifest.tsv" <<EOF
VERSION	2
FS	$orig	../outside.txt	file
EOF

  set +e
  out="$(HOME="$home" "$BIN" restore --backup-dir "$backup" -y 2>&1)"
  rc=$?
  set -e
  [[ $rc -ne 0 ]] || fail "backup_dir 外逃的 rel 应被拒绝"
  assert_contains "$out" "恢复失败" "backup_dir escape"
  assert_file_not_exists "$home/.claude/restore.txt" "backup_dir escape 未写入目标"

  rm -rf "$home" "$backup_root"
  pass "restore 拒绝 backup_dir 外逃 rel"
}

test_restore_rejects_overlong_manifest_line() {
  local home backup out rc
  home="$(mktemp -d /tmp/ccfgtest.longline-home.XXXXXX)"
  backup="$(mktemp -d /tmp/ccbackup.longline.XXXXXX)"
  python3 - "$backup/manifest.tsv" <<'PY2'
from pathlib import Path
import sys
Path(sys.argv[1]).write_text("VERSION\t2\n" + "BROKEN\t" + ("A" * 17000) + "\n")
PY2

  set +e
  out="$(HOME="$home" "$BIN" restore --backup-dir "$backup" -y 2>&1)"
  rc=$?
  set -e
  [[ $rc -ne 0 ]] || fail "超长 manifest 行应被拒绝"
  assert_contains "$out" "manifest.tsv 包含超长行" "overlong manifest"

  rm -rf "$home" "$backup"
  pass "restore 拒绝超长 manifest 行"
}

test_extended_runtime_install_artifacts() {
  local home backup config json_out clean_out restore_out
  home="$(mktemp -d /tmp/ccfgtest.extended-home.XXXXXX)"
  backup="$(mktemp -d /tmp/ccbackup.extended.XXXXXX)"
  config="$home/.claude"
  mkdir -p "$config/local" "$config/uploads/u1" "$config/sessions/s1" \
           "$config/startup-perf" "$config/backups" "$config/plans" \
           "$config/cache" "$config/traces" "$config/ide" \
           "$config/shell-snapshots" "$config/jobs" "$config/tasks" \
           "$config/teams"
  printf 'wrapper' >"$config/local/claude"
  printf 'history' >"$config/history.jsonl"
  printf 'upload' >"$config/uploads/u1/file.txt"
  printf '{}' >"$config/server-sessions.json"
  printf '{}' >"$config/sessions/s1/state.json"
  printf '{}' >"$config/mcp-needs-auth-cache.json"
  printf '{}' >"$config/usage-data"
  printf 'perf' >"$config/startup-perf/run1.txt"
  printf 'bak' >"$config/backups/state.txt"
  printf 'plan' >"$config/plans/p1.txt"
  printf 'cache' >"$config/cache/changelog.md"
  printf '{}' >"$config/traces/t1.json"
  printf '{}' >"$config/ide/state.json"
  printf 'snap' >"$config/shell-snapshots/s1.txt"
  printf '{}' >"$config/jobs/j1.json"
  printf '{}' >"$config/tasks/t1.json"
  printf '{}' >"$config/teams/team1.json"
  printf 'completion' >"$config/completion.zsh"
  mkdir -p "$home/.local/share/claude/versions" "$home/.cache/claude/staging" \
           "$home/.local/state/claude/locks" "$home/.local/bin"
  printf 'bin' >"$home/.local/share/claude/versions/1.0.0"
  printf 'stage' >"$home/.cache/claude/staging/1.0.0"
  printf 'lock' >"$home/.local/state/claude/locks/1.0.0"
  printf 'exec' >"$home/.local/bin/claude"

  if [[ "$(uname -s)" == "Darwin" ]]; then
    mkdir -p "$home/Library/Application Support/Google/Chrome/NativeMessagingHosts"
    printf '{}' >"$home/Library/Application Support/Google/Chrome/NativeMessagingHosts/com.anthropic.claude_code_browser_extension.json"
  else
    mkdir -p "$home/.local/share/applications" "$home/.config/google-chrome/NativeMessagingHosts"
    printf '[Desktop Entry]\nName=Claude\n' >"$home/.local/share/applications/claude-code-url-handler.desktop"
    printf '{}' >"$home/.config/google-chrome/NativeMessagingHosts/com.anthropic.claude_code_browser_extension.json"
  fi

  json_out="$(HOME="$home" XDG_DATA_HOME="$home/.local/share" XDG_CACHE_HOME="$home/.cache" XDG_STATE_HOME="$home/.local/state" "$BIN" check --json)"
  assert_json_python "$json_out" "any(x['identifier'].endswith('/.claude/local') and x['exists'] is True for x in data['runtime'])" "extended local install detected"
  assert_json_python "$json_out" "any(x['identifier'].endswith('/.claude/history.jsonl') and x['exists'] is True for x in data['runtime'])" "extended history detected"
  assert_json_python "$json_out" "any(x['identifier'].endswith('/.local/bin/claude') and x['exists'] is True for x in data['runtime'])" "extended user bin detected"
  assert_json_python "$json_out" "any(x['identifier'].endswith('com.anthropic.claude_code_browser_extension.json') and x['exists'] is True for x in data['runtime'])" "extended native host detected"

  clean_out="$(HOME="$home" XDG_DATA_HOME="$home/.local/share" XDG_CACHE_HOME="$home/.cache" XDG_STATE_HOME="$home/.local/state" "$BIN" clean --backup-dir "$backup" -y)"
  assert_contains "$clean_out" "history.jsonl" "extended clean output"
  assert_file_not_exists "$config/history.jsonl" "extended history removed"
  assert_file_not_exists "$home/.local/bin/claude" "extended native installer removed"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    assert_file_not_exists "$home/Library/Application Support/Google/Chrome/NativeMessagingHosts/com.anthropic.claude_code_browser_extension.json" "extended mac manifest removed"
  else
    assert_file_not_exists "$home/.local/share/applications/claude-code-url-handler.desktop" "extended linux desktop removed"
    assert_file_not_exists "$home/.config/google-chrome/NativeMessagingHosts/com.anthropic.claude_code_browser_extension.json" "extended linux manifest removed"
  fi

  restore_out="$(HOME="$home" XDG_DATA_HOME="$home/.local/share" XDG_CACHE_HOME="$home/.cache" XDG_STATE_HOME="$home/.local/state" "$BIN" restore --backup-dir "$backup" -y)"
  assert_contains "$restore_out" "restored" "extended restore output"
  assert_file_exists "$config/history.jsonl" "extended history restored"
  assert_file_exists "$home/.local/bin/claude" "extended native installer restored"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    assert_file_exists "$home/Library/Application Support/Google/Chrome/NativeMessagingHosts/com.anthropic.claude_code_browser_extension.json" "extended mac manifest restored"
  else
    assert_file_exists "$home/.local/share/applications/claude-code-url-handler.desktop" "extended linux desktop restored"
    assert_file_exists "$home/.config/google-chrome/NativeMessagingHosts/com.anthropic.claude_code_browser_extension.json" "extended linux manifest restored"
  fi

  rm -rf "$home" "$backup"
  pass "扩展安装/运行残留回归通过"
}

test_shell_ide_npm_and_mime_cleanup() {
  local home backup config json_out clean_out restore_out npm_prefix zshrc bashrc fishrc
  home="$(mktemp -d /tmp/ccfgtest.shell-ide.XXXXXX)"
  backup="$(mktemp -d /tmp/ccbackup.shell-ide.XXXXXX)"
  config="$home/.claude"
  npm_prefix="$home/npm-global"
  zshrc="$home/.zshrc"
  bashrc="$home/.bashrc"
  fishrc="$home/.config/fish/config.fish"

  mkdir -p "$config/local" "$(dirname "$fishrc")"
  printf 'wrapper' >"$config/local/claude"
  cat >"$zshrc" <<EOF
export PATH="$home/bin:\$PATH"
# Claude Code shell completions
[[ -f "$config/completion.zsh" ]] && source "$config/completion.zsh"
alias claude="$config/local/claude"
alias keepclaude="echo keep"
EOF
  cat >"$bashrc" <<EOF
# Claude Code shell completions
[ -f "$config/completion.bash" ] && source "$config/completion.bash"
EOF
  cat >"$fishrc" <<EOF
# Claude Code shell completions
[ -f "$config/completion.fish" ] && source "$config/completion.fish"
EOF

  mkdir -p "$home/.vscode/extensions/anthropic.claude-code-1.0.0"
  printf '{}' >"$home/.vscode/extensions/anthropic.claude-code-1.0.0/package.json"

  if [[ "$(uname -s)" == "Darwin" ]]; then
    mkdir -p "$home/Library/Application Support/JetBrains/PyCharm2024.1/plugins/claude-code-jetbrains-plugin"
    printf 'plugin' >"$home/Library/Application Support/JetBrains/PyCharm2024.1/plugins/claude-code-jetbrains-plugin/plugin.txt"
  else
    mkdir -p "$home/.config/JetBrains/PyCharm2024.1/claude-code-jetbrains-plugin"
    printf 'plugin' >"$home/.config/JetBrains/PyCharm2024.1/claude-code-jetbrains-plugin/plugin.txt"
    mkdir -p "$home/.config" "$home/.local/share/applications"
    cat >"$home/.config/mimeapps.list" <<EOF
[Default Applications]
x-scheme-handler/claude-cli=claude-code-url-handler.desktop;other.desktop;
EOF
    cat >"$home/.local/share/applications/mimeapps.list" <<EOF
[Added Associations]
x-scheme-handler/claude-cli=claude-code-url-handler.desktop;
EOF
  fi

  mkdir -p "$npm_prefix/bin" "$npm_prefix/lib/node_modules/@anthropic-ai/claude-code"
  printf 'exec' >"$npm_prefix/bin/claude"
  printf '{}' >"$npm_prefix/lib/node_modules/@anthropic-ai/claude-code/package.json"

  json_out="$(HOME="$home" XDG_CONFIG_HOME="$home/.config" NPM_CONFIG_PREFIX="$npm_prefix" "$BIN" check --json)"
  assert_json_python "$json_out" "any(x['kind'] == 'shell_config' and x['identifier'].endswith('/.zshrc') and x['exists'] is True for x in data['runtime'])" "shell zsh detected"
  assert_json_python "$json_out" "any(x['identifier'].endswith('/anthropic.claude-code-1.0.0') and x['exists'] is True for x in data['runtime'])" "vscode extension detected"
  assert_json_python "$json_out" "any(x['identifier'].endswith('/@anthropic-ai/claude-code') and x['exists'] is True for x in data['runtime'])" "npm package detected"
  assert_json_python "$json_out" "any('claude-code-jetbrains-plugin' in x['identifier'] and x['exists'] is True for x in data['runtime'])" "jetbrains plugin detected"
  if [[ "$(uname -s)" != "Darwin" ]]; then
    assert_json_python "$json_out" "any(x['kind'] == 'mimeapps_config' and x['exists'] is True for x in data['runtime'])" "mimeapps detected"
  fi

  clean_out="$(HOME="$home" XDG_CONFIG_HOME="$home/.config" NPM_CONFIG_PREFIX="$npm_prefix" "$BIN" clean --backup-dir "$backup" -y)"
  assert_contains "$clean_out" ".zshrc" "shell clean output"
  assert_contains "$(cat "$zshrc")" 'alias keepclaude="echo keep"' "custom alias preserved"
  if grep -q 'Claude Code shell completions' "$zshrc"; then fail 'zshrc completion marker should be removed'; fi
  if grep -q 'alias claude=' "$zshrc"; then fail 'zshrc default alias should be removed'; fi
  if grep -q 'completion.bash' "$bashrc"; then fail 'bashrc completion line should be removed'; fi
  if grep -q 'completion.fish' "$fishrc"; then fail 'fishrc completion line should be removed'; fi
  assert_file_not_exists "$home/.vscode/extensions/anthropic.claude-code-1.0.0" "vscode extension removed"
  assert_file_not_exists "$npm_prefix/bin/claude" "npm bin removed"
  assert_file_not_exists "$npm_prefix/lib/node_modules/@anthropic-ai/claude-code" "npm package removed"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    assert_file_not_exists "$home/Library/Application Support/JetBrains/PyCharm2024.1/plugins/claude-code-jetbrains-plugin" "jetbrains plugin removed"
  else
    assert_contains "$(cat "$home/.config/mimeapps.list")" 'other.desktop' "mimeapps other preserved"
    if grep -q 'claude-code-url-handler.desktop' "$home/.config/mimeapps.list"; then fail 'mimeapps claude handler should be removed'; fi
    assert_file_not_exists "$home/.config/JetBrains/PyCharm2024.1/claude-code-jetbrains-plugin" "jetbrains plugin removed"
  fi

  restore_out="$(HOME="$home" XDG_CONFIG_HOME="$home/.config" NPM_CONFIG_PREFIX="$npm_prefix" "$BIN" restore --backup-dir "$backup" -y)"
  assert_contains "$restore_out" "restored" "restore output"
  assert_contains "$(cat "$zshrc")" 'Claude Code shell completions' "zshrc restored"
  assert_contains "$(cat "$zshrc")" "alias claude=\"$config/local/claude\"" "zsh alias restored"
  assert_file_exists "$home/.vscode/extensions/anthropic.claude-code-1.0.0/package.json" "vscode extension restored"
  assert_file_exists "$npm_prefix/bin/claude" "npm bin restored"
  assert_file_exists "$npm_prefix/lib/node_modules/@anthropic-ai/claude-code/package.json" "npm package restored"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    assert_file_exists "$home/Library/Application Support/JetBrains/PyCharm2024.1/plugins/claude-code-jetbrains-plugin/plugin.txt" "jetbrains plugin restored"
  else
    assert_contains "$(cat "$home/.config/mimeapps.list")" 'claude-code-url-handler.desktop' "mimeapps restored"
    assert_file_exists "$home/.config/JetBrains/PyCharm2024.1/claude-code-jetbrains-plugin/plugin.txt" "jetbrains plugin restored"
  fi

  rm -rf "$home" "$backup"
  pass "shell/IDE/npm/mimeapps 回归通过"
}

main() {
  build_if_needed
  if [[ $# -gt 0 ]]; then
    for test_name in "$@"; do
      "$test_name"
    done
    printf '指定回归测试通过。\n'
    return 0
  fi

  test_help_flags
  test_no_system_call
  test_purge_guard
  test_backup_and_restore_flow
  test_restore_json_output
  test_json_output_and_include_related
  test_strict_restore_removes_extras
  test_allow_unsafe_purge_on_temp_dir
  test_restore_rejects_dotdot_target
  test_restore_rejects_backup_escape
  test_restore_rejects_overlong_manifest_line
  test_extended_runtime_install_artifacts
  test_shell_ide_npm_and_mime_cleanup
  printf '全部回归测试通过。\n'
}

main "$@"
