# cc-clean 构建与测试说明

`cc-clean` 是一个用于**彻底卸载 Claude Code** 的跨平台清理工具。

本文档说明这个独立项目的：

- 构建方式
- 测试方式
- CTest 用例与标签组织
- 适合在自动化环境中执行的典型命令

## 项目结构

- `csrc/cc_clean_main.c / cc_clean_cli.c / cc_clean_core.c / cc_clean_report.c`：C 主程序
- `CMakeLists.txt`：跨平台构建配置
- `README.md`：中文项目说明
- `README-EN.md`：英文项目说明
- `CHANGELOG.md`：版本变更记录
- `CONTRIBUTING.md`：贡献说明
- `SECURITY.md`：安全策略
- `scripts/test_cc_clean_posix.sh`：macOS / Linux 回归测试
- `scripts/test_cc_clean_windows.ps1`：Windows 回归测试
- `scripts/cc_clean.py`：早期 Python 参考实现
- `docs/releases/`：版本发布说明
- `docs/releases/TEMPLATE.md`：新版本发布说明模板
- `docs/BUILDING.original.md`：从原仓库保留的历史说明

## 构建

### macOS / Linux

```bash
cmake -S . -B build
cmake --build build
```

生成的可执行文件通常位于：

```bash
./build/cc-clean
```

### Windows

```powershell
cmake -S . -B build
cmake --build build --config Release
```

生成的可执行文件通常位于：

```powershell
.\build\Release\cc-clean.exe
```

## 测试

### 直接运行脚本

#### macOS / Linux

```bash
chmod +x scripts/test_cc_clean_posix.sh
./scripts/test_cc_clean_posix.sh
```

#### Windows

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_cc_clean_windows.ps1
```

### 使用 CTest

```bash
cmake -S . -B build
cmake --build build
cd build
cmake ..
ctest --output-on-failure
```

## 按名称运行单个测试

```bash
cd build
ctest -R cfg_restore_json --output-on-failure
```

## 按 label 运行测试

```bash
cd build
ctest -L json -V
ctest -L restore -V
ctest -L safety -V
ctest -L dangerous -V
```

## 当前 CTest 用例

- `cfg_help_flags`
- `cfg_no_system_call`
- `cfg_purge_guard`
- `cfg_backup_restore_flow`
- `cfg_restore_json`
- `cfg_json_include_related`
- `cfg_strict_restore`
- `cfg_allow_unsafe_purge`
- `cfg_extended_runtime_install_artifacts`
- `cfg_shell_ide_npm_artifacts`
- Windows 额外：`cfg_windows_registry_credential`

## 当前 label

- `cli`
- `source`
- `json`
- `cleanup`
- `backup`
- `restore`
- `filesystem`
- `safety`
- `unsafe`
- `dangerous`
- `manifest`
- Windows 额外：
  - `windows`
  - `windows-only`
  - `registry`
  - `credential`

## label 测试矩阵

| Label | 含义 | 典型场景 |
|---|---|---|
| `cleanup` | 清理相关行为 | 卸载前后的残留清理 |
| `backup` | 备份链路 | 清理前备份 |
| `restore` | 恢复链路 | 卸载回滚 |
| `json` | JSON 输出结构 | 自动化脚本集成 |
| `safety` | 默认安全保护 | 防误删、防误恢复 |
| `dangerous` | 高风险路径 | 危险开关与严格恢复 |
| `manifest` | 备份索引与 manifest 语义 | 备份/恢复数据一致性 |
| `windows-only` | Windows 专项 | 注册表 / Credential Manager |

## 并行执行说明

CTest 已为共享状态测试添加：

- `TIMEOUT`
- `RESOURCE_LOCK`

其中：

- `fg_fs_state`
  - 串行化会修改临时文件系统状态的测试
- `fg_windows_state`
  - 串行化会修改 Windows 注册表 / Credential Manager 的测试

因此在并行执行时，可使用：

```bash
ctest -j 2 --output-on-failure
```

## 自动化场景推荐命令

### 仅检查 JSON 协议输出

```bash
cd build
ctest -L json -V
```

### 仅检查卸载/恢复主流程

```bash
cd build
ctest -L restore -V
```

### 仅检查默认安全保护

```bash
cd build
ctest -L safety -V
```

### 仅检查高风险卸载路径

```bash
cd build
ctest -L dangerous -V
```

### Windows 主机上检查系统集成清理与恢复

```bash
cd build
ctest -L windows-only -V
```

## 注意事项

- 建议在 `build/` 目录中先执行一次 `cmake ..`，再运行 `ctest`
- Windows 注册表 / Credential Manager 专项测试适合在测试环境执行
- 卸载与恢复相关测试已经针对共享状态做了串行化处理

## 相关文档

- `README.md`：中文使用说明
- `README-EN.md`：英文使用说明
- `CHANGELOG.md`：版本变更记录
- `CONTRIBUTING.md`：贡献说明
- `SECURITY.md`：安全策略
- `docs/releases/v0.1.0.md`：当前发布说明

## 自动发布

- `CI` workflow：负责主分支与 PR 的构建、测试
- `Release` workflow：负责 tag 对应版本的构建、打包、校验和上传
- 新版本建议先编写 `docs/releases/<tag>.md`，再推送 tag
