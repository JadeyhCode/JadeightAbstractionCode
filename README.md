# JadeightAbstractionCode — 汇编层（文档导航）

Jadeight 生态的**汇编层**（ISA v3）：`jadeight_asm.hpp`（头文件库，jasm 文本 → `.bc` 模块）
与 `jasm` 命令行工具（汇编 + 反汇编）。j8c 编译器的 `driver.cpp` / `codegen.h` 直接复用本库，
一步产出 `.bc`。ISA 的唯一事实源是 [`../Jadeight2ReWrite/isa.hpp`](../Jadeight2ReWrite/isa.hpp)
（opcode 枚举、助记名 `opName`、长度表、`ModuleImage`、编码器 `Asm`）。

> ⚠️ **详细文档统一在启动器文档库**（`JadeightPoject/docs/`）。
> 入口：[JadeightPoject/docs/00-文档索引.md](../JadeightPoject/docs/00-文档索引.md)

| 想了解 | 看 |
|---|---|
| 汇编器 API、jasm 语法、.bc 布局 | [JadeightPoject/docs/03-汇编器与字节码格式.md](../JadeightPoject/docs/03-汇编器与字节码格式.md) |
| 全部 68 条 opcode 逐条参考 | [JadeightPoject/docs/06-指令集参考.md](../JadeightPoject/docs/06-指令集参考.md) |
| 生态总览与阅读路径 | [JadeightPoject/docs/00-文档索引.md](../JadeightPoject/docs/00-文档索引.md) |

## 本仓库内容

- `jadeight_asm.hpp` — 汇编器 / 反汇编器核心（头文件库，含 `Assembler` 类）
- `jasm.cpp` / `main.cpp` — `jasm` 命令行工具（文本 ↔ 字节码；两者只差一行错误信息文本）
- `jasm_dbg` — 调试版工具
- `tests/roundtrip.sh` — **反汇编回环回归**：`jasm -d` 的产物必须能被 jasm 原样再汇编回
  **逐字节相同**的 `.bc`（含码流里夹 `.BYTE` 数据、入口函数下标非 0 的情形）
- `tests/data_in_code.jasm` — 上述回环的夹具（函数体里夹着字符串数据，复现过 `SQRT ?` 断裂）

## 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target jasm -j4

tests/roundtrip.sh      # 反汇编回环：19 通过 / 0 失败（1 个夹具 + 编译器 9 用例 × -O0/-O2）
```
