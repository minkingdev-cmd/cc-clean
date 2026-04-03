# cc-clean

一个面向 Claude Code 的本地清理工具，目标是：

- 尽量完整地清理 Claude Code 已知本地痕迹
- 默认保守，不误删其他无关文件
- 支持备份与恢复
- 支持 Docker 端到端验证

## 功能

- `check`：检查已识别痕迹
- `clean`：执行清理
- `restore`：从备份恢复

支持的清理范围：

- 默认运行时痕迹
- `--include-related`
  - `settings.json`
  - `agents`
  - `agents.backup.*`
- `--include-installation`
  - completion 缓存
  - shell 配置中的 Claude Code completion 注入行
  - 本地 launcher / 本地安装目录
  - npm 全局安装入口与包目录
- `--purge-all`
  - 等价于 `--include-related --include-installation --purge-config-home`

## 构建

```bash
cmake -S . -B build-cc-clean
cmake --build build-cc-clean
```

产物：

```bash
./build-cc-clean/cc-clean
```

## 常用命令

检查：

```bash
./build-cc-clean/cc-clean check
```

默认清理：

```bash
./build-cc-clean/cc-clean clean -y
```

扩展清理用户相关配置：

```bash
./build-cc-clean/cc-clean clean --include-related -y
```

扩展清理安装与 shell 集成痕迹：

```bash
./build-cc-clean/cc-clean clean --include-installation -y
```

最彻底清理当前工具已精确识别到的 Claude Code 痕迹：

```bash
./build-cc-clean/cc-clean clean --purge-all -y
```

如果 `--config-dir` 不是默认 `~/.claude`，需要额外加：

```bash
--allow-unsafe-purge
```

## 自动发布

仓库已内置 GitHub Actions 发布流程：

- 触发方式：推送 `v*` tag，或手动执行 `release` workflow
- 自动动作：构建、测试、打包、计算 SHA256、创建/更新 GitHub Release
- 当前产物平台：`linux-x64`、`macos-x64`、`macos-arm64`、`windows-x64`

示例：

```bash
git tag v0.1.1
git push origin v0.1.1
```

## 测试

POSIX 回归：

```bash
bash scripts/test_cc_clean_posix.sh
```

Docker 端到端：

```bash
bash scripts/test_cc_clean_container_e2e.sh
```

Windows：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_cc_clean_windows.ps1
```

## 设计原则

- 只清理**已精确识别**的 Claude Code 痕迹
- shell 配置只删除**Claude Code 精确注入行**
- 不对不确定路径做宽泛删除
- 高风险操作必须显式开启
