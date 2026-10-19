# 上游 Fincept 工作流（已停用）

这些文件来自上游 FinceptTerminal，原样保留，但放在这里 GitHub 不会执行。

停用原因（2026-10-19）：本仓库是个人 A 股看板分支。上游工作流会在每次推送时构建完整终端（三平台）、
每晚跑 Sanitizer、同步标签与主题、发布版本等；其中多项与本分支无关，且会因上游自身代码（例如
cppcheck 在 `HyperliquidSigner.cpp` 的报错）持续失败，掩盖真正的问题。

当前生效的 CI 是 `.github/workflows/ashare.yml`：

- 用 clang-format 18.1.3 检查 A 股源码格式；
- 在 Windows（MinGW）、Linux（GCC）、macOS（Clang）上以「警告即错误」构建独立看板并运行 `AShareTests`。

需要恢复某个上游工作流时，把对应 `.yml` 移回 `.github/workflows/` 即可；
注意它们依赖上游的完整依赖（Qt WebEngine、Charts 等）与密钥配置，未必能直接在本仓库通过。
