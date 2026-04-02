#!/usr/bin/env python3
"""
Claude / Claude Code 指纹检查与清理脚本。

目标：
1. 检查当前机器上是否存在常见本地指纹
2. 提供保守清理与激进清理两种操作

说明：
- 默认只清理“高置信度运行痕迹”
- 不会默认删除 ~/.claude/settings.json、~/.claude/agents 等可能是用户主动维护的数据
- 如需更激进的清理，可使用 --include-related 或 --purge-config-home
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


@dataclass
class Artifact:
    kind: str  # file | dir | symlink | keychain
    identifier: str
    exists: bool
    confidence: str  # high | medium | low
    safe_clean: bool
    note: str


def expand(path: str) -> Path:
    return Path(path).expanduser().resolve()


def tilde(path: Path) -> str:
    try:
        home = Path.home().resolve()
        resolved = path.resolve()
        return "~" + os.sep + str(resolved.relative_to(home))
    except Exception:
        return str(path)


def get_default_config_home() -> Path:
    env_override = os.environ.get("CLAUDE_CONFIG_DIR")
    if env_override:
      return expand(env_override)
    return expand("~/.claude")


def get_env_paths(app_name: str = "claude-cli-nodejs") -> dict[str, Path]:
    home = Path.home()
    system = platform.system().lower()

    if system == "darwin":
        return {
            "data": home / "Library" / "Application Support" / app_name,
            "config": home / "Library" / "Preferences" / app_name,
            "cache": home / "Library" / "Caches" / app_name,
            "log": home / "Library" / "Logs" / app_name,
        }

    if system == "windows":
        appdata = Path(os.environ.get("APPDATA", home / "AppData" / "Roaming"))
        local = Path(os.environ.get("LOCALAPPDATA", home / "AppData" / "Local"))
        return {
            "data": local / app_name / "Data",
            "config": appdata / app_name / "Config",
            "cache": local / app_name / "Cache",
            "log": local / app_name / "Log",
        }

    xdg_config = Path(os.environ.get("XDG_CONFIG_HOME", home / ".config"))
    xdg_data = Path(os.environ.get("XDG_DATA_HOME", home / ".local" / "share"))
    xdg_cache = Path(os.environ.get("XDG_CACHE_HOME", home / ".cache"))
    xdg_state = Path(os.environ.get("XDG_STATE_HOME", home / ".local" / "state"))
    return {
        "data": xdg_data / app_name,
        "config": xdg_config / app_name,
        "cache": xdg_cache / app_name,
        "log": xdg_state / app_name,
    }


def get_keychain_service_names(config_home: Path) -> list[str]:
    """
    基于源码逻辑生成常见服务名。
    外部 build 默认为 prod，因此先覆盖最常见的无 suffix 版本。
    """
    default_home = expand("~/.claude")
    is_default_dir = config_home == default_home and "CLAUDE_CONFIG_DIR" not in os.environ
    dir_hash = ""
    if not is_default_dir:
        digest = hashlib.sha256(str(config_home).encode("utf-8")).hexdigest()[:8]
        dir_hash = f"-{digest}"

    oauth_suffixes = ["", "-staging-oauth", "-local-oauth", "-custom-oauth"]
    services: list[str] = []
    for oauth_suffix in oauth_suffixes:
        services.append(f"Claude Code{oauth_suffix}{dir_hash}")
        services.append(f"Claude Code{oauth_suffix}-credentials{dir_hash}")
    # 去重但保持顺序
    return list(dict.fromkeys(services))


def path_kind(path: Path) -> str:
    if path.is_symlink():
        return "symlink"
    if path.is_dir():
        return "dir"
    return "file"


def path_artifact(
    path: Path,
    *,
    confidence: str,
    safe_clean: bool,
    note: str,
) -> Artifact:
    return Artifact(
        kind=path_kind(path) if path.exists() or path.is_symlink() else "file",
        identifier=tilde(path),
        exists=path.exists() or path.is_symlink(),
        confidence=confidence,
        safe_clean=safe_clean,
        note=note,
    )


def scan_keychain(service_name: str) -> Artifact:
    system = platform.system().lower()
    exists = False
    note = "仅在 macOS 上检查"

    if system == "darwin":
        note = "macOS Keychain 服务项"
        try:
            result = subprocess.run(
                ["security", "find-generic-password", "-s", service_name],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
            exists = result.returncode == 0
        except FileNotFoundError:
            note = "当前机器没有 security 命令，无法检查 Keychain"

    return Artifact(
        kind="keychain",
        identifier=service_name,
        exists=exists,
        confidence="medium",
        safe_clean=True,
        note=note,
    )


def collect_artifacts(config_home: Path) -> tuple[list[Artifact], list[Artifact]]:
    runtime: list[Artifact] = []
    related: list[Artifact] = []
    env_paths = get_env_paths()

    # 高置信度：运行时/缓存/日志/协议/凭据
    high_confidence_paths: list[tuple[Path, str, bool, str]] = [
        (expand("~/.claude.json"), "high", True, "全局配置文件（历史/兼容路径）"),
        (expand("~/.claude.oauth.json"), "high", True, "OAuth 全局配置文件"),
        (config_home / ".credentials.json", "high", True, "明文凭据 fallback 文件"),
        (config_home / ".deep-link-register-failed", "high", True, "deep-link 注册失败标记"),
        (config_home / "remote", "high", True, "远程/CCR 相关本地状态"),
        (config_home / "projects", "high", True, "会话 transcript / session 历史"),
        (config_home / "debug", "high", True, "debug 日志"),
        (config_home / "telemetry", "high", True, "1P telemetry 失败批次/缓存"),
        (config_home / "plugins", "high", True, "插件安装/缓存目录"),
        (config_home / "cowork_plugins", "high", True, "cowork 插件目录"),
        (env_paths["data"], "high", True, "env-paths 数据目录"),
        (env_paths["config"], "high", True, "env-paths 配置目录"),
        (env_paths["cache"], "high", True, "env-paths 缓存目录"),
        (env_paths["log"], "high", True, "env-paths 日志目录"),
    ]

    if platform.system().lower() == "darwin":
        high_confidence_paths.append(
            (
                expand("~/Applications/Claude Code URL Handler.app"),
                "high",
                True,
                "macOS deep-link 协议处理器",
            )
        )

    for path, confidence, safe_clean, note in high_confidence_paths:
        runtime.append(
            path_artifact(
                path,
                confidence=confidence,
                safe_clean=safe_clean,
                note=note,
            )
        )

    for service_name in get_keychain_service_names(config_home):
        runtime.append(scan_keychain(service_name))

    # 低/中置信度：相关但可能是用户主动维护的数据
    related_paths: list[tuple[Path, str, bool, str]] = [
        (config_home, "low", False, "Claude 相关配置根目录，可能包含用户自定义内容"),
        (config_home / "settings.json", "low", False, "用户设置，默认不建议自动删除"),
        (config_home / "agents", "low", False, "用户 agents 定义，默认不建议自动删除"),
    ]

    for path, confidence, safe_clean, note in related_paths:
        related.append(
            path_artifact(
                path,
                confidence=confidence,
                safe_clean=safe_clean,
                note=note,
            )
        )

    for backup_dir in sorted(config_home.glob("agents.backup.*")):
        related.append(
            path_artifact(
                backup_dir,
                confidence="low",
                safe_clean=False,
                note="agents 备份目录，默认不建议自动删除",
            )
        )

    return runtime, related


def remove_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink(missing_ok=True)
        return
    if path.is_dir():
        shutil.rmtree(path, ignore_errors=False)


def remove_keychain_service(service_name: str) -> None:
    if platform.system().lower() != "darwin":
        return
    subprocess.run(
        ["security", "delete-generic-password", "-s", service_name],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def confirm(prompt: str) -> bool:
    try:
        answer = input(f"{prompt} [y/N]: ").strip().lower()
    except EOFError:
        return False
    return answer in {"y", "yes"}


def artifact_to_path(artifact: Artifact) -> Path | None:
    if artifact.kind == "keychain":
        return None
    return expand(artifact.identifier)


def build_cleanup_targets(
    runtime: list[Artifact],
    related: list[Artifact],
    *,
    include_related: bool,
    purge_config_home: bool,
    config_home: Path,
) -> list[Artifact]:
    targets: list[Artifact] = []

    for artifact in runtime:
        if artifact.exists and artifact.safe_clean:
            targets.append(artifact)

    if include_related:
        for artifact in related:
            if artifact.exists and artifact.identifier != tilde(config_home):
                targets.append(
                    Artifact(
                        kind=artifact.kind,
                        identifier=artifact.identifier,
                        exists=artifact.exists,
                        confidence=artifact.confidence,
                        safe_clean=True,
                        note=f"{artifact.note}（用户显式要求 include-related）",
                    )
                )

    if purge_config_home and config_home.exists():
        # purge-config-home 优先级最高，避免再逐个重复删子项。
        targets = [
            item
            for item in targets
            if not (
                item.kind != "keychain"
                and str(artifact_to_path(item) or "").startswith(str(config_home))
            )
        ]
        targets.append(
            Artifact(
                kind="dir",
                identifier=tilde(config_home),
                exists=True,
                confidence="low",
                safe_clean=True,
                note="整个配置根目录（高风险操作）",
            )
        )

    dedup: dict[tuple[str, str], Artifact] = {}
    for artifact in targets:
        dedup[(artifact.kind, artifact.identifier)] = artifact
    return list(dedup.values())


def print_report(runtime: list[Artifact], related: list[Artifact]) -> None:
    found_runtime = [a for a in runtime if a.exists]
    found_related = [a for a in related if a.exists]

    print("== 检查结果 ==")
    print(f"高置信度运行痕迹：{len(found_runtime)} 个")
    print(f"相关但不默认清理的痕迹：{len(found_related)} 个")
    print()

    if found_runtime:
        print("[高置信度运行痕迹]")
        for artifact in found_runtime:
            print(f"- [{artifact.kind}] {artifact.identifier}")
            print(f"  说明: {artifact.note}")
        print()
    else:
        print("[高置信度运行痕迹] 未发现")
        print()

    if found_related:
        print("[相关但不默认清理的痕迹]")
        for artifact in found_related:
            print(f"- [{artifact.kind}] {artifact.identifier}")
            print(f"  说明: {artifact.note}")
        print()
    else:
        print("[相关但不默认清理的痕迹] 未发现")
        print()


def print_cleanup_plan(targets: list[Artifact], dry_run: bool) -> None:
    action = "将删除" if dry_run else "待删除"
    print(f"== 清理计划（{action}）==")
    if not targets:
        print("没有需要清理的目标。")
        return
    for artifact in targets:
        print(f"- [{artifact.kind}] {artifact.identifier}")
        print(f"  说明: {artifact.note}")


def run_cleanup(targets: Iterable[Artifact], dry_run: bool) -> list[dict[str, str]]:
    results: list[dict[str, str]] = []
    for artifact in targets:
        result = {
            "kind": artifact.kind,
            "identifier": artifact.identifier,
            "status": "skipped" if dry_run else "unknown",
            "message": artifact.note,
        }

        if dry_run:
            results.append(result)
            continue

        try:
            if artifact.kind == "keychain":
                remove_keychain_service(artifact.identifier)
            else:
                path = artifact_to_path(artifact)
                if path is not None and (path.exists() or path.is_symlink()):
                    remove_path(path)
            result["status"] = "removed"
        except Exception as exc:  # noqa: BLE001
            result["status"] = "failed"
            result["message"] = str(exc)

        results.append(result)
    return results


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="检查并清理 Claude / Claude Code 常见本地指纹"
    )
    parser.add_argument(
        "action",
        nargs="?",
        choices=["check", "clean"],
        default="check",
        help="check=仅检查，clean=执行清理",
    )
    parser.add_argument(
        "--config-dir",
        default=None,
        help="指定配置根目录，默认读取 CLAUDE_CONFIG_DIR 或 ~/.claude",
    )
    parser.add_argument(
        "--include-related",
        action="store_true",
        help="清理低置信度相关项，如 settings.json / agents / agents.backup.*",
    )
    parser.add_argument(
        "--purge-config-home",
        action="store_true",
        help="直接删除整个配置根目录（高风险）",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="只展示检查/清理计划，不真正删除",
    )
    parser.add_argument(
        "-y",
        "--yes",
        action="store_true",
        help="执行 clean 时跳过确认",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="输出 JSON 结果",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    config_home = expand(args.config_dir) if args.config_dir else get_default_config_home()

    runtime, related = collect_artifacts(config_home)

    if args.json:
        payload = {
            "config_home": str(config_home),
            "runtime": [asdict(a) for a in runtime],
            "related": [asdict(a) for a in related],
        }
        if args.action == "check":
            print(json.dumps(payload, ensure_ascii=False, indent=2))
            return 0
    else:
        print(f"配置根目录: {tilde(config_home)}")
        print_report(runtime, related)

    if args.action == "check":
        return 0

    targets = build_cleanup_targets(
        runtime,
        related,
        include_related=args.include_related,
        purge_config_home=args.purge_config_home,
        config_home=config_home,
    )

    if args.json:
        payload["cleanup_targets"] = [asdict(a) for a in targets]
        if args.dry_run:
            payload["cleanup_results"] = []
            print(json.dumps(payload, ensure_ascii=False, indent=2))
            return 0
    else:
        print_cleanup_plan(targets, args.dry_run)
        print()

    if not targets:
        if args.json:
            payload["cleanup_results"] = []
            print(json.dumps(payload, ensure_ascii=False, indent=2))
        return 0

    if not args.dry_run and not args.yes:
        warning = "将执行保守清理"
        if args.include_related:
            warning = "将执行扩展清理（包含用户相关配置）"
        if args.purge_config_home:
            warning = "将执行高风险清理（删除整个配置根目录）"
        if not confirm(f"{warning}，是否继续？"):
            print("已取消。")
            return 1

    cleanup_results = run_cleanup(targets, args.dry_run)

    if args.json:
        payload["cleanup_results"] = cleanup_results
        print(json.dumps(payload, ensure_ascii=False, indent=2))
        return 0

    print("== 清理结果 ==")
    for item in cleanup_results:
        print(f"- [{item['status']}] {item['kind']} {item['identifier']}")
        if item.get("message"):
            print(f"  说明: {item['message']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
