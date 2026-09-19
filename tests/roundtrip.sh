#!/usr/bin/env bash
# roundtrip.sh — jasm 反汇编回环回归测试
#
#   不变式：`jasm -d` 的产物必须能被 jasm 原样再汇编回**逐字节相同**的 .bc
#   （含码流里夹着数据、入口函数下标非 0 的情形）。
#
#   用例 A：tests/*.jasm        —— 汇编 → -d 反汇编 → 再汇编，逐字节比对
#   用例 B：../JadeightCompiler/tests/*.j8（找到 j8c 时）
#                               —— j8c 直出 .bc → -d → 再汇编，逐字节比对
#                                  （真实的「码流里带字符串数据」场景）
#
# 用法: tests/roundtrip.sh          （可用 JASM= / J8C= 覆盖可执行文件路径）
set -u

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
JASM="${JASM:-$ROOT/build/jasm}"
J8C="${J8C:-$ROOT/../JadeightCompiler/build/j8c}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ ! -x "$JASM" ]; then
    echo "构建 jasm ..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "configure 失败"; exit 2; }
    cmake --build build --target jasm >/dev/null || { echo "构建失败"; exit 2; }
fi

pass=0
fail=0
failed=()

check() { # $1 = 用例名, $2 = 原始 .bc
    local tag="$1" bc="$2"
    if ! "$JASM" "$bc" -d -o "$TMP/dis.jasm" >/dev/null 2>"$TMP/err"; then
        echo "FAIL $tag：反汇编失败（$(head -1 "$TMP/err")）"
        fail=$((fail + 1)); failed+=("$tag(反汇编)"); return
    fi
    if ! "$JASM" "$TMP/dis.jasm" -o "$TMP/re.bc" >/dev/null 2>"$TMP/err"; then
        echo "FAIL $tag：反汇编产物无法再汇编（$(head -1 "$TMP/err")）"
        fail=$((fail + 1)); failed+=("$tag(再汇编)"); return
    fi
    if cmp -s "$bc" "$TMP/re.bc"; then
        pass=$((pass + 1))
    else
        echo "FAIL $tag：再汇编结果与原始 .bc 逐字节不一致"
        fail=$((fail + 1)); failed+=("$tag(字节不一致)")
    fi
}

# ---- 用例 A：手写 .jasm ----
for src in tests/*.jasm; do
    [ -f "$src" ] || continue
    base="$(basename "$src" .jasm)"
    if ! "$JASM" "$src" -o "$TMP/$base.bc" >/dev/null 2>"$TMP/err"; then
        echo "FAIL $base：汇编失败（$(head -1 "$TMP/err")）"
        fail=$((fail + 1)); failed+=("$base(汇编)"); continue
    fi
    check "$base（手写 .jasm）" "$TMP/$base.bc"
done

# ---- 用例 B：编译器直出的模块 ----
if [ -x "$J8C" ]; then
    for src in ../JadeightCompiler/tests/*.j8; do
        [ -f "$src" ] || continue
        base="$(basename "$src" .j8)"
        for opt in 0 2; do
            if ! "$J8C" "$src" -o "$TMP/$base.bc" -O$opt >/dev/null 2>&1; then
                echo "SKIP $base -O$opt（j8c 编译失败）"
                continue
            fi
            check "$base -O$opt（j8c 产出）" "$TMP/$base.bc"
        done
    done
else
    echo "（未找到 j8c，跳过编译器产物回环：$J8C）"
fi

echo "===== 反汇编回环：$pass 通过 / $fail 失败 ====="
[ "$fail" -eq 0 ] || printf '失败用例：%s\n' "${failed[*]}"
exit $([ "$fail" -eq 0 ] && echo 0 || echo 1)
