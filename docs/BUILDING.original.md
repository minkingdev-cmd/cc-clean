# 构建说明（草案）

> 这份文档面向当前仓库快照。它的目标不是保证“立即成功构建”，而是说明：
>
> 1. 这个快照为什么**不能直接编译**
> 2. 目前已经补了哪些最小工程文件
> 3. 应该如何做**研究用途的最小构建尝试**
> 4. 当前最主要的阻塞点是什么

---

## 1. 当前状态

当前仓库是一个 `src/` 源码快照，不是完整的原始工程仓库。

缺失的关键元数据包括但不限于：

- 原始 `package.json`
- 原始 `bun.lock` / `package-lock.json` / `pnpm-lock.yaml`
- 原始 `tsconfig.json`
- 原始 `bunfig.toml`
- 原始 CI / build scripts
- 可能存在的代码生成步骤

因此：

> **本仓库不能保证直接成功构建。**

为了便于研究，我已补了两份最小草案：

- `package.json`
- `tsconfig.json`

它们只是“推断版构建清单”，不是官方构建配置。

---

## 2. 先决条件

本仓库源码大量使用：

- `bun:bundle`
- `feature('...')`
- Bun 风格 bundling

因此建议优先使用 **Bun**。

### 安装 Bun

参考官方文档：

- https://bun.sh/docs/installation

安装后确认：

```bash
bun --version
```

---

## 3. 当前已补的最小构建文件

### `package.json`

提供了以下脚本：

```bash
bun run typecheck
bun run build
bun run build:debug
bun run build:compile
```

含义：

- `typecheck`：只做 TypeScript 检查
- `build`：尝试将 `src/main.tsx` 打成 Bun 运行目标
- `build:debug`：带 sourcemap 的构建
- `build:compile`：尝试打成单文件可执行

### `tsconfig.json`

当前草案主要补了这些能力：

- `moduleResolution: "Bundler"`
- `jsx: "react-jsx"`
- `baseUrl: "."`
- `paths: { "src/*": ["src/*"] }`

这对当前源码很重要，因为代码中大量使用：

```ts
import ... from 'src/...'
```

---

## 4. 推荐的尝试顺序

### 步骤 1：安装依赖

```bash
bun install
```

### 步骤 2：先做类型检查

```bash
bun run typecheck
```

### 步骤 3：再做最小 bundling 尝试

```bash
bun run build
```

### 步骤 4：如果你只是想验证单文件可执行路径

```bash
bun run build:compile
```

---

## 5. 预期会遇到的问题

即使公共依赖都安装完成，构建仍然**大概率失败**。

最主要原因是：源码里存在看起来像**私有/内部包**的依赖。

当前已经在 `package.json` 中记录为：

```json
"x-unresolved-private-dependencies": [
  "@ant/claude-for-chrome-mcp",
  "@ant/computer-use-input",
  "@ant/computer-use-mcp",
  "@ant/computer-use-swift",
  "@anthropic-ai/claude-agent-sdk",
  "@anthropic-ai/mcpb",
  "@anthropic-ai/sandbox-runtime"
]
```

这些包如果不公开可安装，就会导致：

- `bun install` 失败
- 或 `bun build` 在解析 import 时失败

---

## 6. 为什么这个仓库不能直接“恢复原始构建”

当前缺少以下关键事实：

### 6.1 原始依赖版本

现在 `package.json` 使用的是推断式 `latest`。

这意味着：

- API 兼容性不确定
- React / Ink / MCP SDK / OpenTelemetry 版本可能不匹配
- 很容易出现类型错误或运行时错误

### 6.2 原始 feature flags

代码中大量使用：

```ts
import { feature } from 'bun:bundle'
```

而原始构建到底开启了哪些 feature，目前未知。

不同 feature 组合会影响：

- 哪些模块进入 bundle
- 哪些私有依赖被激活
- 哪些代码路径被裁剪

### 6.3 可能存在生成文件或预处理步骤

源码里能看到一些迹象，例如：

- proto / schema 生成
- React compiler runtime 相关输出
- 可能的内部 build transforms

如果这些步骤缺失，就算依赖齐全，也不一定能成功构建。

---

## 7. 研究用途下的三种构建目标

### 目标 A：只验证“源码大致能被 bundler 解析”

建议做法：

1. 保留当前最小 `package.json`
2. 先安装能安装的公共依赖
3. 不主动启用额外 feature flags
4. 执行：

```bash
bun run build
```

这是最低成本路径。

---

### 目标 B：做“研究版可编译裁剪构建”

建议做法：

1. 为私有包建立本地 stub
2. 或通过 patch 暂时移除相关 import
3. 尽量关闭会激活私有能力的 feature
4. 只保留核心主链：
   - `main.tsx`
   - `commands.ts`
   - `tools.ts`
   - `QueryEngine.ts`
   - `query.ts`

这是目前最现实的路线。

---

### 目标 C：完整还原官方可运行 CLI

当前**基本不可行**，除非你还能拿到：

- 原始 `package.json`
- 原始 lockfile
- 原始 Bun/TS 配置
- 原始 build scripts

---

## 8. `claude_fingerprint_guard` 回归测试

当前仓库已为 C 版指纹工具补了回归测试脚本：

- macOS / Linux：
  - `scripts/test_fingerprint_guard_posix.sh`
- Windows：
  - `scripts/test_fingerprint_guard_windows.ps1`

### 8.1 本机直接运行

#### macOS / Linux

```bash
chmod +x scripts/test_fingerprint_guard_posix.sh
./scripts/test_fingerprint_guard_posix.sh
```

#### Windows PowerShell

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_fingerprint_guard_windows.ps1
```

### 8.2 通过 CMake / CTest 运行

先构建：

```bash
cmake -S . -B build-c-fingerprint
cmake --build build-c-fingerprint
```

然后进入构建目录执行：

```bash
cd build-c-fingerprint
ctest --output-on-failure
```

> 注意：某些环境下 `ctest --test-dir build-c-fingerprint` 可能不会正确发现测试，  
> 更稳妥的方式是先 `cd build-c-fingerprint` 再执行 `ctest`。

当前 CTest 已拆成多个细粒度 case，例如：

- `cfg_help_flags`
- `cfg_no_system_call`
- `cfg_purge_guard`
- `cfg_backup_restore_flow`
- `cfg_restore_json`
- `cfg_json_include_related`
- `cfg_strict_restore`
- `cfg_allow_unsafe_purge`
- Windows 额外：
  - `cfg_windows_registry_credential`

可按名称单独运行，例如：

```bash
cd build-c-fingerprint
ctest -R cfg_restore_json --output-on-failure
```

也可以按 label 过滤，例如：

```bash
cd build-c-fingerprint
ctest -L json --output-on-failure
ctest -L restore --output-on-failure
ctest -L safety --output-on-failure
```

当前标签大致包括：

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

此外，CTest 还加了：

- `TIMEOUT`
  - 轻量检查通常为 `15s`
  - 文件系统/恢复类通常为 `30s ~ 45s`
  - Windows 注册表/凭据专项通常为 `90s`
- `RESOURCE_LOCK`
  - `fg_fs_state`
    - 串行化会修改临时文件系统状态的测试
  - `fg_windows_state`
    - 串行化会修改 Windows 注册表 / Credential Manager 的测试

这意味着在并行执行 `ctest -j` 时，涉及共享状态的危险测试不会互相踩踏。

### 8.2.1 按 label 的测试矩阵说明

| Label | 含义 | 当前覆盖的典型 case |
|---|---|---|
| `cli` | 命令行帮助/入口行为 | `cfg_help_flags` |
| `source` | 源码静态约束 | `cfg_no_system_call` |
| `json` | JSON 输出结构与字段正确性 | `cfg_restore_json`, `cfg_json_include_related` |
| `cleanup` | 清理计划/清理执行相关 | `cfg_purge_guard`, `cfg_json_include_related`, `cfg_allow_unsafe_purge` |
| `backup` | 备份产物与备份链路 | `cfg_backup_restore_flow` |
| `restore` | 恢复链路与恢复结果 | `cfg_backup_restore_flow`, `cfg_restore_json`, `cfg_strict_restore` |
| `filesystem` | 文件/目录备份恢复与严格还原 | `cfg_backup_restore_flow`, `cfg_strict_restore` |
| `manifest` | `manifest.tsv` / `index.md` 相关语义 | `cfg_backup_restore_flow`, `cfg_restore_json` |
| `safety` | 默认安全保护是否生效 | `cfg_help_flags`, `cfg_no_system_call`, `cfg_purge_guard` |
| `unsafe` | 显式危险开关路径 | `cfg_allow_unsafe_purge` |
| `dangerous` | 会触发高风险删除/严格恢复的测试 | `cfg_purge_guard`, `cfg_strict_restore`, `cfg_allow_unsafe_purge` |
| `windows` | Windows 平台能力相关 | `cfg_windows_registry_credential` |
| `windows-only` | 仅应在 Windows 主机执行 | `cfg_windows_registry_credential` |
| `registry` | Windows 注册表检测/备份/恢复 | `cfg_windows_registry_credential` |
| `credential` | Windows Credential Manager 检测/恢复 | `cfg_windows_registry_credential` |

### 8.2.2 常用执行组合

#### 只跑安全保护相关

```bash
cd build-c-fingerprint
ctest -L safety -V
```

#### 只跑 JSON 协议相关

```bash
cd build-c-fingerprint
ctest -L json -V
```

#### 只跑备份/恢复主链路

```bash
cd build-c-fingerprint
ctest -L backup -V
ctest -L restore -V
```

#### 只跑高风险路径

```bash
cd build-c-fingerprint
ctest -L dangerous -V
```

#### Windows 主机上只跑注册表/凭据专项

```bash
cd build-c-fingerprint
ctest -L windows-only -V
ctest -L registry -V
ctest -L credential -V
```

> 建议先在构建目录执行一次 `cmake .`，再用 `ctest -L ...` 过滤运行。

### 8.3 当前覆盖的回归点

- `--help` 是否包含危险开关说明
- 源码中是否仍残留 `system()`
- 非默认目录的 `--purge-config-home` 是否默认被拒绝
- `--allow-unsafe-purge` 是否可在临时目录上生效
- `clean --backup-dir` 是否会生成：
  - `manifest.tsv`
  - `index.md`
- `manifest.tsv` 版本是否为 `VERSION\t2`
- `check --json` 是否输出核心字段
- `check --json` 的关键结构是否正确：
  - `config_home`
  - `runtime`
  - `related`
  - 代表性条目的 `exists/safe_clean`
- `--include-related --dry-run` 是否包含 `settings.json` / `agents.backup.*`
- `clean --dry-run --json` 是否包含：
  - `cleanup_targets`
  - `cleanup_results`
  - 且 `cleanup_results[*].status == skipped`
- `restore --json` 是否包含：
  - `cleanup_results`
  - 且恢复成功条目的 `status == restored`
- `restore` 是否默认拒绝恢复到白名单之外路径
- `--allow-unsafe-restore` 下是否可完成备份恢复
- `strict restore` 是否会移除恢复前额外混入的文件
- Windows 专项（在 Windows 主机执行）：
  - `claude-cli` 注册表键/值检测
  - `REG_EXPAND_SZ` 类型备份/恢复
  - `manifest.tsv` 中 `REGVAL` / `WINCRED` 条目存在
  - Credential Manager 通用凭据清理与恢复
- 私有 registry 或内部包
- 原始 feature flag 组合
- 可能的生成文件与内部构建步骤

---

## 8. 下一步建议

如果你的目标是继续推进构建，建议按这个优先级做：

### 方案 1：先跑一遍安装/构建，拿到第一批真实报错

```bash
bun install
bun run build
```

拿到报错后，再分为：

- 公共依赖缺失
- 私有依赖缺失
- 类型/路径问题
- Bun feature/bundling 问题

逐个修。

### 方案 2：直接开始做私有依赖 stub

适合你已经明确知道：

- 私有包拿不到
- 目标是做“研究版可编译恢复”

---

## 9. 补充说明：为什么当前默认不加 feature flags

当前的 `build` 脚本没有主动添加 `--feature ...`。

原因是：

1. 我们不知道原始 feature 组合
2. 不加 feature 时，很多 `feature('...')` 路径会被视为 false
3. 这通常能减少被激活的模块和私有依赖面

所以这是一个更保守、更适合“先 smoke test”的默认策略。

如果后续确认了某些 feature 必须打开，再逐个补即可。

---

## 10. 参考

### Bun 官方文档

- 安装：https://bun.sh/docs/installation
- Bundler：https://bun.sh/docs/bundler

### 当前仓库中已补的文件

- `package.json`
- `tsconfig.json`

---

## 11. 当前结论

一句话总结：

> 当前仓库已经具备“最小构建尝试”的基础文件，但距离“成功构建”还差一段距离，最大的阻塞点是**私有依赖与缺失的原始构建元数据**。
