#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${FG_DOCKER_IMAGE:-node:20}"

pass() {
  printf 'PASS: %s\n' "$1"
}

fail() {
  printf 'FAIL: %s\n' "$1" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "缺少命令: $1"
}

run_in_container() {
  local test_name="$1"
  docker run --rm -v "${ROOT_DIR}:/work" -w /work "$IMAGE" bash -s -- "$test_name" <<'SH'
set -euo pipefail

TEST_NAME="$1"

prepare_workspace() {
  npm install -g @anthropic-ai/claude-code >/dev/null
  cc -O2 -Wall -Wextra -Wpedantic -o /tmp/cc-clean /work/csrc/cc_clean.c

  export HOME=/tmp/fg-home
  export XDG_DATA_HOME=/tmp/fg-xdg/data
  export XDG_CONFIG_HOME=/tmp/fg-xdg/config
  export XDG_CACHE_HOME=/tmp/fg-xdg/cache
  export XDG_STATE_HOME=/tmp/fg-xdg/state
  export CONFIG_HOME="$HOME/.claude"

  rm -rf "$HOME" /tmp/fg-xdg
  mkdir -p "$CONFIG_HOME/projects/session1" \
           "$CONFIG_HOME/debug" \
           "$CONFIG_HOME/telemetry" \
           "$CONFIG_HOME/plugins" \
           "$CONFIG_HOME/cowork_plugins" \
           "$CONFIG_HOME/agents" \
           "$CONFIG_HOME/remote" \
           "$CONFIG_HOME/agents.backup.demo"
  mkdir -p "$XDG_DATA_HOME/claude-cli-nodejs" \
           "$XDG_CONFIG_HOME/claude-cli-nodejs" \
           "$XDG_CACHE_HOME/claude-cli-nodejs" \
           "$XDG_STATE_HOME/claude-cli-nodejs"

  printf 'secret' > "$CONFIG_HOME/.credentials.json"
  printf '{"theme":"dark"}' > "$CONFIG_HOME/settings.json"
  printf 'name: demo\n' > "$CONFIG_HOME/agents/demo.yaml"
  printf 'backup' > "$CONFIG_HOME/agents.backup.demo/info.txt"
  printf 'hello' > "$CONFIG_HOME/projects/session1/msg.txt"
  printf 'dbg' > "$CONFIG_HOME/debug/log.txt"
  printf 'tlm' > "$CONFIG_HOME/telemetry/batch.json"
  printf 'plugin' > "$CONFIG_HOME/plugins/cache.txt"
  printf 'cowork' > "$CONFIG_HOME/cowork_plugins/cache.txt"
  printf 'remote' > "$CONFIG_HOME/remote/state.json"
  printf '{"legacy":true}' > "$HOME/.claude.json"
  printf '{"oauth":true}' > "$HOME/.claude.oauth.json"
  printf 'data' > "$XDG_DATA_HOME/claude-cli-nodejs/data.txt"
  printf 'config' > "$XDG_CONFIG_HOME/claude-cli-nodejs/config.txt"
  printf 'cache' > "$XDG_CACHE_HOME/claude-cli-nodejs/cache.txt"
  printf 'state' > "$XDG_STATE_HOME/claude-cli-nodejs/state.txt"
  printf 'user-note' > "$CONFIG_HOME/user-note.txt"
  printf 'complete' > "$CONFIG_HOME/completion.bash"
  mkdir -p "$HOME/.local/bin" "$CONFIG_HOME/local/node_modules/.bin"
  printf '#!/bin/sh\nexit 0\n' > "$HOME/.local/bin/claude"
  chmod +x "$HOME/.local/bin/claude"
  printf '#!/bin/sh\nexec "%s" "$@"\n' "$CONFIG_HOME/local/node_modules/.bin/claude" > "$CONFIG_HOME/local/claude"
  chmod +x "$CONFIG_HOME/local/claude"
  printf 'export PATH="$HOME/bin:$PATH"\n# Claude Code shell completions\n[ -f "%s" ] && source "%s"\n' \
    "$CONFIG_HOME/completion.bash" "$CONFIG_HOME/completion.bash" > "$HOME/.bashrc"

  claude --help >/dev/null 2>&1 || true
}

assert_default_clean() {
  /tmp/cc-clean check --config-dir "$CONFIG_HOME" --json > /tmp/before.json
  /tmp/cc-clean clean --config-dir "$CONFIG_HOME" --json -y > /tmp/clean.json
  /tmp/cc-clean check --config-dir "$CONFIG_HOME" --json > /tmp/after.json

  node <<'NODE'
const fs = require('fs');
const before = JSON.parse(fs.readFileSync('/tmp/before.json', 'utf8'));
const clean = JSON.parse(fs.readFileSync('/tmp/clean.json', 'utf8'));
const after = JSON.parse(fs.readFileSync('/tmp/after.json', 'utf8'));

const runtimeTargets = [
  '~/.claude.json',
  '~/.claude.oauth.json',
  '~/.claude/.credentials.json',
  '~/.claude/projects',
  '~/.claude/debug',
  '~/.claude/telemetry',
  '~/.claude/plugins',
  '~/.claude/cowork_plugins',
  '~/.claude/remote',
  process.env.XDG_DATA_HOME + '/claude-cli-nodejs',
  process.env.XDG_CONFIG_HOME + '/claude-cli-nodejs',
  process.env.XDG_CACHE_HOME + '/claude-cli-nodejs',
  process.env.XDG_STATE_HOME + '/claude-cli-nodejs',
];

function findById(list, id) {
  return (list || []).find(x => x.identifier === id);
}

for (const id of runtimeTargets) {
  const e = findById(before.runtime, id);
  if (!e || e.exists !== true || e.safe_clean !== true) {
    console.error('清理前缺少运行时痕迹:', id, e);
    process.exit(1);
  }
}
for (const id of runtimeTargets) {
  const e = findById(after.runtime, id);
  if (!e || e.exists !== false) {
    console.error('默认清理后仍有残留:', id, e);
    process.exit(1);
  }
}
const removed = new Map((clean.cleanup_results || []).map(x => [x.identifier, x.status]));
for (const id of runtimeTargets) {
  if (removed.get(id) !== 'removed') {
    console.error('默认清理结果不是 removed:', id, removed.get(id));
    process.exit(1);
  }
}
for (const id of ['~/.claude/settings.json', '~/.claude/agents', '~/.claude/agents.backup.demo']) {
  const e = findById(after.related, id);
  if (!e || e.exists !== true || e.safe_clean !== false) {
    console.error('默认清理误删了用户内容:', id, e);
    process.exit(1);
  }
}
for (const id of ['~/.claude/completion.bash', '~/.bashrc', '~/.claude/local', '~/.local/bin/claude', '/usr/local/bin/claude', '/usr/local/lib/node_modules/@anthropic-ai/claude-code']) {
  const e = findById(after.installation, id);
  if (!e || e.exists !== true) {
    console.error('默认清理不应删除安装痕迹:', id, e);
    process.exit(1);
  }
}
NODE

  [ -f "$CONFIG_HOME/settings.json" ]
  [ -f "$CONFIG_HOME/agents/demo.yaml" ]
  [ -f "$CONFIG_HOME/agents.backup.demo/info.txt" ]
  [ -f "$CONFIG_HOME/user-note.txt" ]
  [ ! -e "$CONFIG_HOME/.credentials.json" ]
  [ ! -e "$CONFIG_HOME/projects" ]
  [ ! -e "$CONFIG_HOME/debug" ]
  [ ! -e "$CONFIG_HOME/telemetry" ]
  [ ! -e "$CONFIG_HOME/plugins" ]
  [ ! -e "$CONFIG_HOME/cowork_plugins" ]
  [ ! -e "$CONFIG_HOME/remote" ]
  [ ! -e "$HOME/.claude.json" ]
  [ ! -e "$HOME/.claude.oauth.json" ]
  [ ! -e "$XDG_DATA_HOME/claude-cli-nodejs" ]
  [ ! -e "$XDG_CONFIG_HOME/claude-cli-nodejs" ]
  [ ! -e "$XDG_CACHE_HOME/claude-cli-nodejs" ]
  [ ! -e "$XDG_STATE_HOME/claude-cli-nodejs" ]
  grep -q 'Claude Code shell completions' "$HOME/.bashrc"
  [ -f "$CONFIG_HOME/completion.bash" ]
  [ -f "$HOME/.local/bin/claude" ]
  [ -f "$CONFIG_HOME/local/claude" ]
  [ -e /usr/local/bin/claude ]
}

assert_include_related_clean() {
  /tmp/cc-clean clean --config-dir "$CONFIG_HOME" --include-related --json -y > /tmp/include-clean.json
  /tmp/cc-clean check --config-dir "$CONFIG_HOME" --json > /tmp/include-after.json

  node <<'NODE'
const fs = require('fs');
const clean = JSON.parse(fs.readFileSync('/tmp/include-clean.json', 'utf8'));
const after = JSON.parse(fs.readFileSync('/tmp/include-after.json', 'utf8'));

const expectedRemoved = new Set([
  '~/.claude/settings.json',
  '~/.claude/agents',
  '~/.claude/agents.backup.demo',
]);

const removed = new Map((clean.cleanup_results || []).map(x => [x.identifier, x.status]));
for (const id of expectedRemoved) {
  if (removed.get(id) !== 'removed') {
    console.error('--include-related 未删除目标:', id, removed.get(id));
    process.exit(1);
  }
}
for (const id of expectedRemoved) {
  const e = (after.related || []).find(x => x.identifier === id);
  if (!e || e.exists !== false) {
    console.error('--include-related 清理后仍存在:', id, e);
    process.exit(1);
  }
}
NODE

  [ ! -e "$CONFIG_HOME/settings.json" ]
  [ ! -e "$CONFIG_HOME/agents" ]
  [ ! -e "$CONFIG_HOME/agents.backup.demo" ]
  [ -f "$CONFIG_HOME/user-note.txt" ]
}

assert_include_installation_clean() {
  /tmp/cc-clean clean --config-dir "$CONFIG_HOME" \
    --include-related --include-installation --purge-config-home --json -y > /tmp/full-clean.json
  /tmp/cc-clean check --config-dir "$CONFIG_HOME" --json > /tmp/full-after.json

  node <<'NODE'
const fs = require('fs');
const clean = JSON.parse(fs.readFileSync('/tmp/full-clean.json', 'utf8'));
const after = JSON.parse(fs.readFileSync('/tmp/full-after.json', 'utf8'));

const expectedRemoved = new Set([
  '~/.claude',
  '~/.bashrc',
  '~/.local/bin/claude',
  '/usr/local/bin/claude',
  '/usr/local/lib/node_modules/@anthropic-ai/claude-code',
]);

const removed = new Map((clean.cleanup_results || []).map(x => [x.identifier, x.status]));
for (const id of expectedRemoved) {
  if (removed.get(id) !== 'removed') {
    console.error('完全清理未删除目标:', id, removed.get(id));
    process.exit(1);
  }
}
for (const id of ['~/.claude/settings.json', '~/.claude/agents', '~/.claude/agents.backup.demo',
                  '~/.claude/completion.bash', '~/.bashrc', '~/.claude/local',
                  '~/.local/bin/claude', '/usr/local/bin/claude',
                  '/usr/local/lib/node_modules/@anthropic-ai/claude-code']) {
  const lists = [after.runtime || [], after.related || [], after.installation || []];
  const e = lists.flat().find(x => x.identifier === id);
  if (e && e.exists !== false) {
    console.error('完全清理后仍存在:', id, e);
    process.exit(1);
  }
}
NODE

  [ ! -e "$CONFIG_HOME" ]
  [ ! -e "$HOME/.claude.json" ]
  [ ! -e "$HOME/.claude.oauth.json" ]
  [ ! -e "$HOME/.local/bin/claude" ]
  [ ! -e /usr/local/bin/claude ]
  [ ! -e /usr/local/lib/node_modules/@anthropic-ai/claude-code ]
  grep -q 'export PATH="$HOME/bin:$PATH"' "$HOME/.bashrc"
  ! grep -q 'Claude Code shell completions' "$HOME/.bashrc"
  ! grep -q 'completion.bash' "$HOME/.bashrc"
}

assert_purge_all_clean() {
  /tmp/cc-clean clean --config-dir "$CONFIG_HOME" \
    --purge-all --json -y > /tmp/purge-all.json
  /tmp/cc-clean check --config-dir "$CONFIG_HOME" --json > /tmp/purge-all-after.json

  node <<'NODE'
const fs = require('fs');
const clean = JSON.parse(fs.readFileSync('/tmp/purge-all.json', 'utf8'));
const after = JSON.parse(fs.readFileSync('/tmp/purge-all-after.json', 'utf8'));
const removed = new Map((clean.cleanup_results || []).map(x => [x.identifier, x.status]));
for (const id of ['~/.claude', '~/.bashrc', '~/.local/bin/claude', '/usr/local/bin/claude', '/usr/local/lib/node_modules/@anthropic-ai/claude-code']) {
  if (removed.get(id) !== 'removed') {
    console.error('purge-all 未删除目标:', id, removed.get(id));
    process.exit(1);
  }
}
for (const listName of ['runtime', 'related', 'installation']) {
  for (const item of after[listName] || []) {
    if ((item.identifier === '~/.claude' ||
         item.identifier === '~/.bashrc' ||
         item.identifier === '~/.local/bin/claude' ||
         item.identifier === '/usr/local/bin/claude' ||
         item.identifier === '/usr/local/lib/node_modules/@anthropic-ai/claude-code') &&
        item.exists !== false) {
      console.error('purge-all 后仍有残留:', item);
      process.exit(1);
    }
  }
}
NODE

  [ ! -e "$CONFIG_HOME" ]
  [ ! -e "$HOME/.local/bin/claude" ]
  [ ! -e /usr/local/bin/claude ]
  [ ! -e /usr/local/lib/node_modules/@anthropic-ai/claude-code ]
  grep -q 'export PATH="$HOME/bin:$PATH"' "$HOME/.bashrc"
  ! grep -q 'Claude Code shell completions' "$HOME/.bashrc"
}

prepare_workspace

case "$TEST_NAME" in
  default_clean)
    assert_default_clean
    ;;
  include_related)
    assert_include_related_clean
    ;;
  include_installation)
    assert_include_installation_clean
    ;;
  purge_all)
    assert_purge_all_clean
    ;;
  *)
    echo "未知测试: $TEST_NAME" >&2
    exit 2
    ;;
esac
SH
}

test_container_default_clean() {
  run_in_container default_clean
  pass "容器端到端默认清理：删干净且不误删"
}

test_container_include_related() {
  run_in_container include_related
  pass "容器端到端 include-related：额外目标清理正确"
}

test_container_include_installation() {
  run_in_container include_installation
  pass "容器端到端 include-installation：安装与 shell 痕迹清理正确"
}

test_container_purge_all() {
  run_in_container purge_all
  pass "容器端到端 purge-all：完整清理正确"
}

main() {
  need_cmd docker
  if [[ $# -gt 0 ]]; then
    for test_name in "$@"; do
      "$test_name"
    done
    printf '指定容器端到端测试通过。\n'
    return 0
  fi

  test_container_default_clean
  test_container_include_related
  test_container_include_installation
  test_container_purge_all
  printf '全部容器端到端测试通过。\n'
}

main "$@"
