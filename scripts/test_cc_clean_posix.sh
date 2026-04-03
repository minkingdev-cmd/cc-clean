#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT_DIR}/build-cc-clean/cc-clean"
SRC="${ROOT_DIR}/csrc/cc_clean.c"

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

assert_symlink_exists() {
  local path="$1"
  local name="$2"
  [[ -L "$path" ]] || fail "$name: 符号链接不存在 $path"
}

assert_equals() {
  local actual="$1"
  local expected="$2"
  local name="$3"
  [[ "$actual" == "$expected" ]] || fail "$name: 期望 [$expected] 实际 [$actual]"
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
    cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build-cc-clean"
    cmake --build "$ROOT_DIR/build-cc-clean"
  fi
}

test_help_flags() {
  local out
  out="$("$BIN" --help)"
  assert_contains "$out" "--allow-unsafe-purge" "帮助输出"
  assert_contains "$out" "--allow-unsafe-restore" "帮助输出"
  assert_contains "$out" "--purge-all" "帮助输出"
  pass "帮助输出包含危险开关"
}

test_no_system_call() {
  if grep -n 'system(' "$SRC" >/dev/null 2>&1; then
    fail "源码中仍存在 system()"
  fi
  pass "源码中不存在 system()"
}

test_security_hardening_markers() {
  grep -q 'PROCESS_CAPTURE_MAX_BYTES' "$SRC" || fail "缺少子进程输出大小上限"
  grep -q 'SYMLINK_TARGET_MAX_BYTES' "$SRC" || fail "缺少 symlink 目标大小上限"
  if grep -q 'snprintf(child, sizeof(child), "%s\\\\%s"' "$SRC"; then
    fail "Windows 递归删除仍存在固定缓冲区 child 拼接"
  fi
  if grep -q 'snprintf(pattern, sizeof(pattern), "%s\\\\\\*"' "$SRC"; then
    fail "Windows 路径通配拼接仍存在固定缓冲区 pattern 拼接"
  fi
  if grep -q 'snprintf(pattern, sizeof(pattern), "%s\\\\agents\\.backup\\.\\*"' "$SRC"; then
    fail "Windows agents.backup 通配拼接仍存在固定缓冲区 pattern 拼接"
  fi
  pass "安全加固关键标记存在"
}

test_purge_guard() {
  local tmpdir out
  tmpdir="$(mktemp -d /tmp/ccfgtest.purge.XXXXXX)"
  mkdir -p "$tmpdir/projects"
  printf 'secret' >"$tmpdir/.credentials.json"
  set +e
  out="$("$BIN" clean --config-dir "$tmpdir" --purge-config-home --dry-run 2>&1)"
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

  clean_out="$("$BIN" clean --config-dir "$tmpdir" --backup-dir "$backup" -y)"
  assert_contains "$clean_out" "[removed] file" "clean 输出"
  assert_contains "$clean_out" "[removed] dir" "clean 输出"
  assert_file_exists "$backup/manifest.tsv" "manifest"
  assert_file_exists "$backup/index.md" "index.md"
  assert_contains "$(cat "$backup/manifest.tsv")" $'VERSION\t2' "manifest 版本"
  assert_file_not_exists "$tmpdir/.credentials.json" "clean 删除 credentials"
  assert_file_not_exists "$tmpdir/projects" "clean 删除 projects"

  set +e
  restore_out="$("$BIN" restore --backup-dir "$backup" -y 2>&1)"
  local rc=$?
  set -e
  [[ $rc -ne 0 ]] || fail "未加 allow-unsafe-restore 时应拒绝恢复到 /tmp"
  assert_contains "$restore_out" "--allow-unsafe-restore" "restore 白名单保护"

  "$BIN" restore --backup-dir "$backup" --allow-unsafe-restore -y >/dev/null
  assert_file_exists "$tmpdir/.credentials.json" "restore 恢复 credentials"
  assert_file_exists "$tmpdir/projects/session1/msg.txt" "restore 恢复 projects"

  rm -rf "$tmpdir" "$backup"
  pass "clean/backup/restore 回归流程通过"
}

test_backup_restore_long_symlink_target() {
  local tmpdir backup long_target restored
  tmpdir="$(mktemp -d /tmp/ccfgtest.symlink.XXXXXX)"
  backup="$(mktemp -d /tmp/ccbackup.symlink.XXXXXX)"
  mkdir -p "$tmpdir/projects/session1"
  long_target="$(python3 - <<'PY'
print("target/" + "nested-segment/" * 40 + "final-destination")
PY
)"
  ln -s "$long_target" "$tmpdir/projects/session1/link-to-long-target"

  "$BIN" clean --config-dir "$tmpdir" --backup-dir "$backup" -y >/dev/null
  "$BIN" restore --backup-dir "$backup" --allow-unsafe-restore -y >/dev/null

  assert_symlink_exists "$tmpdir/projects/session1/link-to-long-target" "long symlink 恢复"
  restored="$(readlink "$tmpdir/projects/session1/link-to-long-target")"
  assert_equals "$restored" "$long_target" "long symlink 目标保持完整"

  rm -rf "$tmpdir" "$backup"
  pass "超长 symlink 目标备份恢复通过"
}

test_restore_json_output() {
  local tmpdir backup json_out
  tmpdir="$(mktemp -d /tmp/ccfgtest.restorejson.XXXXXX)"
  backup="$(mktemp -d /tmp/ccbackup.restorejson.XXXXXX)"
  mkdir -p "$tmpdir/projects/session1"
  printf 'secret' >"$tmpdir/.credentials.json"
  printf 'hello' >"$tmpdir/projects/session1/msg.txt"

  "$BIN" clean --config-dir "$tmpdir" --backup-dir "$backup" -y >/dev/null
  json_out="$("$BIN" restore --backup-dir "$backup" --allow-unsafe-restore --json -y)"
  assert_json_python "$json_out" "'cleanup_results' in data and isinstance(data['cleanup_results'], list) and len(data['cleanup_results']) >= 2" "restore json cleanup_results"
  assert_json_python "$json_out" "all(x['status'] == 'restored' for x in data['cleanup_results'])" "restore json restored 状态"
  assert_json_python "$json_out" "any(x['kind'] == 'filesystem' and x['identifier'] == '$tmpdir/.credentials.json' for x in data['cleanup_results'])" "restore json credentials 结果"
  assert_file_exists "$tmpdir/.credentials.json" "restore json 恢复 credentials"
  assert_file_exists "$tmpdir/projects/session1/msg.txt" "restore json 恢复 msg"

  rm -rf "$tmpdir" "$backup"
  pass "restore JSON 回归通过"
}

test_restore_rejects_invalid_manifest_version() {
  local backup out
  backup="$(mktemp -d /tmp/ccbackup.badver.XXXXXX)"
  printf 'VERSION\tabc\n' >"$backup/manifest.tsv"

  set +e
  out="$("$BIN" restore --backup-dir "$backup" --allow-unsafe-restore -y 2>&1)"
  local rc=$?
  set -e
  [[ $rc -ne 0 ]] || fail "无效 manifest 版本应失败"
  assert_contains "$out" "版本字段无效" "invalid manifest version"

  rm -rf "$backup"
  pass "无效 manifest 版本会被拒绝"
}

test_restore_rejects_overlong_manifest_line() {
  local backup out
  backup="$(mktemp -d /tmp/ccbackup.longline.XXXXXX)"
  python3 - <<'PY' >"$backup/manifest.tsv"
print("FS\t" + "a" * 20000 + "\tfiles/demo\tfile")
PY

  set +e
  out="$("$BIN" restore --backup-dir "$backup" --allow-unsafe-restore -y 2>&1)"
  local rc=$?
  set -e
  [[ $rc -ne 0 ]] || fail "超长 manifest 行应失败"
  assert_contains "$out" "存在超长行" "overlong manifest line"

  rm -rf "$backup"
  pass "超长 manifest 行会被拒绝"
}

test_json_output_and_include_related() {
  local tmpdir json_out plan_out
  tmpdir="$(mktemp -d /tmp/ccfgtest.json.XXXXXX)"
  mkdir -p "$tmpdir/agents" "$tmpdir/agents.backup.demo"
  printf '{}' >"$tmpdir/settings.json"
  printf 'secret' >"$tmpdir/.credentials.json"

  json_out="$("$BIN" check --config-dir "$tmpdir" --json)"
  assert_contains "$json_out" '"config_home"' "check json"
  assert_contains "$json_out" '"runtime"' "check json"
  assert_contains "$json_out" '"related"' "check json"
  assert_json_python "$json_out" "data['config_home'] == '$tmpdir'" "check json config_home"
  assert_json_python "$json_out" "isinstance(data['runtime'], list) and len(data['runtime']) >= 10" "check json runtime 数量"
  assert_json_python "$json_out" "any(x['identifier'] == '$tmpdir/.credentials.json' and x['exists'] is True and x['safe_clean'] is True for x in data['runtime'])" "check json credentials 条目"
  assert_json_python "$json_out" "any(x['identifier'] == '$tmpdir/settings.json' and x['safe_clean'] is False for x in data['related'])" "check json related settings"

  plan_out="$("$BIN" clean --config-dir "$tmpdir" --include-related --dry-run)"
  assert_contains "$plan_out" 'settings.json' "include-related 计划"
  assert_contains "$plan_out" 'agents.backup.demo' "include-related 计划"
  assert_contains "$plan_out" '[file]' "include-related 计划"

  local clean_json
  clean_json="$("$BIN" clean --config-dir "$tmpdir" --include-related --dry-run --json)"
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

  "$BIN" clean --config-dir "$tmpdir" --backup-dir "$backup" -y >/dev/null
  mkdir -p "$tmpdir/projects/session1"
  printf 'extra' >"$tmpdir/projects/session1/extra.txt"
  "$BIN" restore --backup-dir "$backup" --allow-unsafe-restore -y >/dev/null

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
  out="$("$BIN" clean --config-dir "$tmpdir" --purge-config-home --allow-unsafe-purge -y)"
  assert_contains "$out" "整个配置根目录" "unsafe purge 输出"
  assert_file_not_exists "$tmpdir" "unsafe purge 删除目录"
  pass "allow-unsafe-purge 临时目录回归通过"
}

test_purge_all_implies_full_scope_in_plan() {
  local tmpdir plan
  tmpdir="$(mktemp -d /tmp/ccfgtest.purgeall.XXXXXX)"
  mkdir -p "$tmpdir/agents" "$tmpdir/agents.backup.demo" "$tmpdir/local"
  printf '{}' >"$tmpdir/settings.json"
  printf 'secret' >"$tmpdir/.credentials.json"

  plan="$("$BIN" clean --config-dir "$tmpdir" --purge-all --allow-unsafe-purge --dry-run --json)"
  assert_json_python "$plan" "any(x['identifier'] == '$tmpdir' for x in data['cleanup_targets'])" "purge-all 包含配置根目录"
  assert_json_python "$plan" "any(x['identifier'] == '$tmpdir/settings.json' and x['exists'] is True for x in data['related'])" "purge-all 仍识别 settings"
  assert_json_python "$plan" "any(x['identifier'] == '$tmpdir/agents' and x['exists'] is True for x in data['related'])" "purge-all 仍识别 agents"
  assert_json_python "$plan" "any(x['identifier'] == '$tmpdir/local' and x['exists'] is True for x in data['installation'])" "purge-all 仍识别本地安装目录"

  rm -rf "$tmpdir"
  pass "purge-all 计划包含完整清理范围"
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
  test_security_hardening_markers
  test_purge_guard
  test_backup_and_restore_flow
  test_backup_restore_long_symlink_target
  test_restore_json_output
  test_restore_rejects_invalid_manifest_version
  test_restore_rejects_overlong_manifest_line
  test_json_output_and_include_related
  test_strict_restore_removes_extras
  test_allow_unsafe_purge_on_temp_dir
  test_purge_all_implies_full_scope_in_plan
  printf '全部回归测试通过。\n'
}

main "$@"
