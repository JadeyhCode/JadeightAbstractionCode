# JadeightAbstractionCode — 汇编层（文档导航）

Jadeight 生态的**汇编层**：`jadeight_asm.hpp`（头文件库，jasm 文本 → `.bc` 字节码）
与 `jasm` 命令行工具。j8c 编译器的 `driver.cpp` 直接复用本库，一步产出 `.bc`。

> ⚠️ **详细文档统一在启动器文档库**（`JadeightPoject/docs/`）。
> 入口：[JadeightPoject/docs/00-文档索引.md](../JadeightPoject/docs/00-文档索引.md)

| 想了解 | 看 |
|---|---|
| 汇编器 API、jasm 语法、.bc 布局 | [JadeightPoject/docs/03-汇编器与字节码格式.md](../JadeightPoject/docs/03-汇编器与字节码格式.md) |
| 全部 177 条 opcode 逐条参考 | [JadeightPoject/docs/06-指令集参考.md](../JadeightPoject/docs/06-指令集参考.md) |
| 生态总览与阅读路径 | [JadeightPoject/docs/00-文档索引.md](../JadeightPoject/docs/00-文档索引.md) |

## 本仓库内容

- `jadeight_asm.hpp` — 汇编器核心（头文件库，含 `Assembler` 类）
- `jasm.cpp` / `main.cpp` — `jasm` 命令行工具（文本 → 字节码）
- `jasm_dbg` — 调试版工具
