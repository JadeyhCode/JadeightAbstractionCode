// jadeight_asm.hpp — Jadeight VM 汇编器（header-only 库）
//
// 从 jasm main.cpp 抽取：Assembler 类 + 全部 opcode 定义/名称表/指令长度表。
// 供两类使用者复用：
//   1) jasm 命令行工具（main.cpp 里的薄 CLI）
//   2) JadeightCompiler —— 在汇编器之上封装 C 风格编译器（"封装一层"）
//
// 用法:
//   #include "jadeight_asm.hpp"
//   jadeight::Assembler asmblr;
//   asmblr.verbose = ...; asmblr.pureBinary = ...; asmblr.littleEndianHeader = ...;
//   std::vector<uint8_t> bytes; uint32_t argSize, retSize, entry;
//   asmblr.assemble(asmText, bytes, argSize, retSize, entry);
//
// 注意: 默认 .bc 头部为【大端序】(argSize,retSize,entry 各 u32 BE)，与汇编器自身
//       反汇编回环一致；设 littleEndianHeader=true 时头部改为【小端序】，与
//       Jadeight2 虚拟机 FunctionSave::loadFromFile 的文件格式一致。

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace jadeight {

// ==================== opcode 定义 (与 Jadeight2 VM 完全一致) ====================
enum : uint8_t {
    OP_ERR_END = 0,
    // REG 寄存器指令族 (1..21)
    OP_REG_MOVI_U8, OP_REG_MOVI_U16, OP_REG_MOVI_U32, OP_REG_MOVI_U64,
    OP_REG_MOV,
    OP_REG_PUSH_U8, OP_REG_PUSH_U16, OP_REG_PUSH_U32, OP_REG_PUSH_U64,
    OP_REG_POP_U8, OP_REG_POP_U16, OP_REG_POP_U32, OP_REG_POP_U64,
    OP_REG_LOAD_U8, OP_REG_LOAD_U16, OP_REG_LOAD_U32, OP_REG_LOAD_U64,
    OP_REG_STORE_U8, OP_REG_STORE_U16, OP_REG_STORE_U32, OP_REG_STORE_U64,
    // 基础指令 (22..34)
    OP_END, OP_STACK_INIT, OP_JMP, OP_SHORT_JMP,
    OP_SCOPE_PUSH, OP_SCOPE_POP, OP_STACK_PTR_MOVE, OP_NEW_STACK,
    OP_NEW_HEAP, OP_DEL_HEAP, OP_IF_GOTO,
    OP_LEA,
    OP_MOVI_U32,
    // 四则运算 (35..68)
    OP_ADD_U8, OP_ADD_U16, OP_ADD_U32, OP_ADD_U64, OP_ADD_F32, OP_ADD_F64,
    OP_SUB_U8, OP_SUB_U16, OP_SUB_U32, OP_SUB_U64, OP_SUB_F32, OP_SUB_F64,
    OP_MUL_U8, OP_MUL_I8, OP_MUL_U16, OP_MUL_I16, OP_MUL_U32, OP_MUL_I32,
    OP_MUL_U64, OP_MUL_I64, OP_MUL_F32, OP_MUL_F64,
    OP_DIV_U8, OP_DIV_I8, OP_DIV_U16, OP_DIV_I16, OP_DIV_U32, OP_DIV_I32,
    OP_DIV_U64, OP_DIV_I64, OP_DIV_F32, OP_DIV_F64,
    OP_ADD_PTR, OP_SUB_PTR,
    // SQRT / LOG (69..88)
    OP_SQRT_U8, OP_SQRT_I8, OP_SQRT_U16, OP_SQRT_I16, OP_SQRT_U32, OP_SQRT_I32,
    OP_SQRT_U64, OP_SQRT_I64, OP_SQRT_F32, OP_SQRT_F64,
    OP_LOG_U8, OP_LOG_I8, OP_LOG_U16, OP_LOG_I16, OP_LOG_U32, OP_LOG_I32,
    OP_LOG_U64, OP_LOG_I64, OP_LOG_F32, OP_LOG_F64,
    // COUT (89..91)
    OP_COUT_CHAR8, OP_COUT_CHAR16, OP_COUT_CHAR32,
    // 比较 (92..124)
    OP_CMP_LT_U8, OP_CMP_LT_I8, OP_CMP_LT_U16, OP_CMP_LT_I16, OP_CMP_LT_U32, OP_CMP_LT_I32,
    OP_CMP_LT_U64, OP_CMP_LT_I64, OP_CMP_LT_F32, OP_CMP_LT_F64, OP_CMP_LT_PTR,
    OP_CMP_EQ_U8, OP_CMP_EQ_I8, OP_CMP_EQ_U16, OP_CMP_EQ_I16, OP_CMP_EQ_U32, OP_CMP_EQ_I32,
    OP_CMP_EQ_U64, OP_CMP_EQ_I64, OP_CMP_EQ_F32, OP_CMP_EQ_F64, OP_CMP_EQ_PTR,
    OP_CMP_GT_U8, OP_CMP_GT_I8, OP_CMP_GT_U16, OP_CMP_GT_I16, OP_CMP_GT_U32, OP_CMP_GT_I32,
    OP_CMP_GT_U64, OP_CMP_GT_I64, OP_CMP_GT_F32, OP_CMP_GT_F64, OP_CMP_GT_PTR,
    // 位运算 (125..148)
    OP_SHL_U8, OP_SHL_U16, OP_SHL_U32, OP_SHL_U64,
    OP_AND_U8, OP_AND_U16, OP_AND_U32, OP_AND_U64,
    OP_OR_U8, OP_OR_U16, OP_OR_U32, OP_OR_U64,
    OP_NOT_U8, OP_NOT_U16, OP_NOT_U32, OP_NOT_U64,
    OP_SHR_U8, OP_SHR_U16, OP_SHR_U32, OP_SHR_U64,
    OP_SHR_I8, OP_SHR_I16, OP_SHR_I32, OP_SHR_I64,
    // 类型转换 (149..156)
    OP_CVT_F32_U32, OP_CVT_F32_I32, OP_CVT_F64_U32, OP_CVT_F64_I32,
    OP_CVT_U32_F32, OP_CVT_U32_F64, OP_CVT_I32_F32, OP_CVT_I32_F64,
    // 分配器 (157..158)
    OP_NEW_ARRAY, OP_FREE_ARRAY,
    // 系统地址 / 间接跳转 (159..160)
    OP_GET_ADDRS, OP_JMP_IND,
    // 原子变量 (161..170)
    OP_ATOMIC_LOAD_U32, OP_ATOMIC_LOAD_U64,
    OP_ATOMIC_STORE_U32, OP_ATOMIC_STORE_U64,
    OP_ATOMIC_XCHG_U32, OP_ATOMIC_XCHG_U64,
    OP_ATOMIC_CAS_U32, OP_ATOMIC_CAS_U64,
    OP_ATOMIC_ADD_U32, OP_ATOMIC_ADD_U64,
    // 外部调用 (171)
    OP_EXTERN_CALL,
    // VM 函数调用 (172)
    OP_FUNC_CALL,
    // JIT 提交 (173)
    OP_JIT_SUBMIT,
    // 内存拷贝 (174)
    OP_MEMCPY,
    // 获取系统信息 (175)：压入 u64（低 16 位=平台，次 16 位=架构）
    OP_GET_SYSTEM,
};

// ==================== 名称 -> opcode 映射 ====================
static const std::map<std::string, uint8_t> opNameMap = {
    {"END", OP_END},
    {"STACK_INIT", OP_STACK_INIT},
    {"JMP", OP_JMP},
    {"SHORT_JMP", OP_SHORT_JMP},
    {"SCOPE_PUSH", OP_SCOPE_PUSH},
    {"SCOPE_POP", OP_SCOPE_POP},
    {"STACK_PTR_MOVE", OP_STACK_PTR_MOVE},
    {"NEW_STACK", OP_NEW_STACK},
    {"NEW_HEAP", OP_NEW_HEAP},
    {"DEL_HEAP", OP_DEL_HEAP},
    {"IF_GOTO", OP_IF_GOTO},
    {"LEA", OP_LEA},
    {"MOVI_U32", OP_MOVI_U32},
    {"ADD_PTR", OP_ADD_PTR},
    {"SUB_PTR", OP_SUB_PTR},
    {"COUT_CHAR8", OP_COUT_CHAR8},
    {"COUT_CHAR16", OP_COUT_CHAR16},
    {"COUT_CHAR32", OP_COUT_CHAR32},
    {"NEW_ARRAY", OP_NEW_ARRAY},
    {"FREE_ARRAY", OP_FREE_ARRAY},
    {"GET_ADDRS", OP_GET_ADDRS},
    {"JMP_IND", OP_JMP_IND},
    {"EXTERN_CALL", OP_EXTERN_CALL},
    {"FUNC_CALL", OP_FUNC_CALL},
    {"JIT_SUBMIT", OP_JIT_SUBMIT},
    {"MEMCPY", OP_MEMCPY},
    {"GET_SYSTEM", OP_GET_SYSTEM},
};

// 生成所有类型化指令的名称映射
inline void buildOpNameMapFull(std::map<std::string, uint8_t>& map) {
    map = opNameMap;

    auto add_reg = [&](const char* prefix, uint8_t base) {
        const char* suffixes[] = {"U8","U16","U32","U64"};
        for (int i = 0; i < 4; ++i) {
            std::string name = std::string(prefix) + "_" + suffixes[i];
            map[name] = static_cast<uint8_t>(base + i);
        }
    };
    add_reg("REG_MOVI", OP_REG_MOVI_U8);
    map["REG_MOV"] = OP_REG_MOV;
    add_reg("REG_PUSH", OP_REG_PUSH_U8);
    add_reg("REG_POP", OP_REG_POP_U8);
    add_reg("REG_LOAD", OP_REG_LOAD_U8);
    add_reg("REG_STORE", OP_REG_STORE_U8);

    auto add_arith = [&](const char* prefix, uint8_t base, const std::vector<const char*>& types) {
        for (size_t i = 0; i < types.size(); ++i) {
            std::string name = std::string(prefix) + "_" + types[i];
            map[name] = static_cast<uint8_t>(base + i);
        }
    };
    add_arith("ADD", OP_ADD_U8, {"U8","U16","U32","U64","F32","F64"});
    add_arith("SUB", OP_SUB_U8, {"U8","U16","U32","U64","F32","F64"});
    add_arith("MUL", OP_MUL_U8, {"U8","I8","U16","I16","U32","I32","U64","I64","F32","F64"});
    add_arith("DIV", OP_DIV_U8, {"U8","I8","U16","I16","U32","I32","U64","I64","F32","F64"});
    add_arith("SQRT", OP_SQRT_U8, {"U8","I8","U16","I16","U32","I32","U64","I64","F32","F64"});
    add_arith("LOG", OP_LOG_U8, {"U8","I8","U16","I16","U32","I32","U64","I64","F32","F64"});
    add_arith("CMP_LT", OP_CMP_LT_U8, {"U8","I8","U16","I16","U32","I32","U64","I64","F32","F64","PTR"});
    add_arith("CMP_EQ", OP_CMP_EQ_U8, {"U8","I8","U16","I16","U32","I32","U64","I64","F32","F64","PTR"});
    add_arith("CMP_GT", OP_CMP_GT_U8, {"U8","I8","U16","I16","U32","I32","U64","I64","F32","F64","PTR"});
    add_arith("SHL", OP_SHL_U8, {"U8","U16","U32","U64"});
    add_arith("AND", OP_AND_U8, {"U8","U16","U32","U64"});
    add_arith("OR", OP_OR_U8, {"U8","U16","U32","U64"});
    add_arith("NOT", OP_NOT_U8, {"U8","U16","U32","U64"});
    add_arith("SHR", OP_SHR_U8, {"U8","U16","U32","U64"});
    add_arith("SHR_I", OP_SHR_I8, {"I8","I16","I32","I64"});
    add_arith("CVT", OP_CVT_F32_U32, {"F32_U32","F32_I32","F64_U32","F64_I32",
                                       "U32_F32","U32_F64","I32_F32","I32_F64"});
    add_arith("ATOMIC_LOAD", OP_ATOMIC_LOAD_U32, {"U32","U64"});
    add_arith("ATOMIC_STORE", OP_ATOMIC_STORE_U32, {"U32","U64"});
    add_arith("ATOMIC_XCHG", OP_ATOMIC_XCHG_U32, {"U32","U64"});
    add_arith("ATOMIC_CAS", OP_ATOMIC_CAS_U32, {"U32","U64"});
    add_arith("ATOMIC_ADD", OP_ATOMIC_ADD_U32, {"U32","U64"});
}

// ==================== 指令长度表 (用于反汇编/偏移计算) ====================
inline size_t instrLen(uint8_t op) {
    switch (op) {
    case OP_REG_MOVI_U8: return 3;
    case OP_REG_MOVI_U16: return 4;
    case OP_REG_MOVI_U32: return 6;
    case OP_REG_MOVI_U64: return 10;
    case OP_REG_MOV: return 3;
    case OP_REG_PUSH_U8: case OP_REG_PUSH_U16: case OP_REG_PUSH_U32: case OP_REG_PUSH_U64: return 2;
    case OP_REG_POP_U8: case OP_REG_POP_U16: case OP_REG_POP_U32: case OP_REG_POP_U64: return 2;
    case OP_REG_LOAD_U8: case OP_REG_LOAD_U16: case OP_REG_LOAD_U32: case OP_REG_LOAD_U64: return 11;
    case OP_REG_STORE_U8: case OP_REG_STORE_U16: case OP_REG_STORE_U32: case OP_REG_STORE_U64: return 11;
    case OP_STACK_INIT: return 9;
    case OP_JMP: return 5;
    case OP_SHORT_JMP: return 2;
    case OP_STACK_PTR_MOVE: return 5;
    case OP_NEW_STACK: return 9;
    case OP_NEW_HEAP: return 13;
    case OP_DEL_HEAP: return 5;
    case OP_IF_GOTO: return 6;
    case OP_LEA: return 10;
    case OP_MOVI_U32: return 5;
    case OP_NEW_ARRAY: return 13;
    case OP_FREE_ARRAY: return 5;
    case OP_ATOMIC_LOAD_U32: case OP_ATOMIC_LOAD_U64:
    case OP_ATOMIC_STORE_U32: case OP_ATOMIC_STORE_U64:
    case OP_ATOMIC_XCHG_U32: case OP_ATOMIC_XCHG_U64:
    case OP_ATOMIC_CAS_U32: case OP_ATOMIC_CAS_U64:
    case OP_ATOMIC_ADD_U32: case OP_ATOMIC_ADD_U64: return 11;
    case OP_EXTERN_CALL: return 10;
    case OP_FUNC_CALL: return 13;
    case OP_JIT_SUBMIT: return 9;
    case OP_MEMCPY: return 27;
    case OP_GET_SYSTEM: return 1;
    default: return 1;
    }
}

// ==================== 大端序/小端序读写工具 ====================
template<class T>
inline T rdBE(const uint8_t* p) {
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof(T); ++i) v = (v << 8) | p[i];
    return static_cast<T>(v);
}

template<class T>
inline void wrBE(uint8_t* p, T v) {
    for (size_t i = 0; i < sizeof(T); ++i) {
        p[sizeof(T)-1-i] = static_cast<uint8_t>(v & 0xFF);
        v = static_cast<T>(static_cast<uint64_t>(v) >> 8);
    }
}

template<class T>
inline T rdLE(const uint8_t* p) {
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof(T); ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return static_cast<T>(v);
}

template<class T>
inline void wrLE(uint8_t* p, T v) {
    for (size_t i = 0; i < sizeof(T); ++i) {
        p[i] = static_cast<uint8_t>(v & 0xFF);
        v = static_cast<T>(static_cast<uint64_t>(v) >> 8);
    }
}

// ==================== 数值解析 ====================
inline uint64_t parseNum(const std::string& s, bool& ok) {
    ok = true;
    if (s.empty()) { ok = false; return 0; }
    if (s.size() >= 2 && s[0] == '\'' && s.back() == '\'') {
        if (s.size() == 3) return static_cast<uint8_t>(s[1]);
        if (s.size() == 4 && s[1] == '\\') {
            switch (s[2]) {
                case 'n': return '\n';
                case 't': return '\t';
                case '0': return '\0';
                case '\\': return '\\';
                case '\'': return '\'';
                default: ok = false; return 0;
            }
        }
        ok = false; return 0;
    }
    try {
        size_t pos = 0;
        uint64_t v = std::stoull(s, &pos, 0);
        if (pos != s.size()) { ok = false; return 0; }
        return v;
    } catch (...) { ok = false; return 0; }
}

// ==================== 寄存器解析 ====================
inline int parseReg(const std::string& s, bool& ok) {
    ok = false;
    if (s.size() < 2 || (s[0] != 'R' && s[0] != 'r')) return 0;
    try {
        int r = std::stoi(s.substr(1));
        if (r >= 0 && r <= 15) { ok = true; return r; }
    } catch (...) {}
    return 0;
}

// ==================== 指令解析 ====================
struct Instruction {
    uint8_t opcode;
    std::vector<uint64_t> operands;
    std::vector<std::string> labelRefs;
    size_t offset = 0;
};

// ==================== 汇编器 ====================
class Assembler {
public:
    bool verbose = false;
    bool pureBinary = false;      // -f: 无 FunctionSave 头部
    bool littleEndianHeader = false; // -e: 头部按小端序读写（VM loadFromFile 兼容）
    std::string inputPath;
    std::string outputPath;

    Assembler() { buildOpNameMapFull(fullOpNameMap); }

    bool assemble(const std::string& asmText, std::vector<uint8_t>& outBytes,
                  uint32_t& argSize, uint32_t& retSize, uint32_t& entry) {
        lines = splitLines(asmText);
        if (!firstPass()) return false;
        if (!resolveLabels()) return false;
        if (!emit(outBytes, argSize, retSize, entry)) return false;
        return true;
    }

    bool disassemble(const std::vector<uint8_t>& bytes, std::ostream& os, bool hasHeader) {
        size_t off = 0;
        uint32_t argSize=0, retSize=0, entry=0;
        if (hasHeader && bytes.size() >= 12) {
            argSize = littleEndianHeader ? rdLE<uint32_t>(bytes.data())
                                         : rdBE<uint32_t>(bytes.data());
            retSize = littleEndianHeader ? rdLE<uint32_t>(bytes.data()+4)
                                         : rdBE<uint32_t>(bytes.data()+4);
            entry   = littleEndianHeader ? rdLE<uint32_t>(bytes.data()+8)
                                         : rdBE<uint32_t>(bytes.data()+8);
            os << "; FunctionSave header:\n";
            os << ".ARGS " << argSize << "\n";
            os << ".RETS " << retSize << "\n";
            os << ".ENTRY " << entry << "\n\n";
            off = 12;
        }
        while (off < bytes.size()) {
            uint8_t op = bytes[off];
            size_t len = instrLen(op);
            if (len == 0) { os << "???"; break; }
            if (off + len > bytes.size()) break;
            std::string name;
            for (auto& [n, o] : fullOpNameMap) {
                if (o == op) { name = n; break; }
            }
            if (name.empty()) name = "OP_" + std::to_string(op);
            os << name;
            const uint8_t* p = bytes.data() + off;
            switch (op) {
            case OP_REG_MOVI_U8:
                os << " R" << (p[1] & 0x0F) << ", " << (int)p[2];
                break;
            case OP_REG_MOVI_U16:
                os << " R" << (p[1] & 0x0F) << ", " << rdBE<uint16_t>(p+2);
                break;
            case OP_REG_MOVI_U32:
                os << " R" << (p[1] & 0x0F) << ", " << rdBE<uint32_t>(p+2);
                break;
            case OP_REG_MOVI_U64:
                os << " R" << (p[1] & 0x0F) << ", " << rdBE<uint64_t>(p+2);
                break;
            case OP_REG_MOV:
                os << " R" << (p[1] & 0x0F) << ", R" << (p[2] & 0x0F);
                break;
            case OP_REG_PUSH_U8: case OP_REG_PUSH_U16: case OP_REG_PUSH_U32: case OP_REG_PUSH_U64:
            case OP_REG_POP_U8: case OP_REG_POP_U16: case OP_REG_POP_U32: case OP_REG_POP_U64:
                os << " R" << (p[1] & 0x0F);
                break;
            case OP_REG_LOAD_U8: case OP_REG_LOAD_U16: case OP_REG_LOAD_U32: case OP_REG_LOAD_U64:
            case OP_REG_STORE_U8: case OP_REG_STORE_U16: case OP_REG_STORE_U32: case OP_REG_STORE_U64:
                os << " R" << (p[1] & 0x0F) << ", mode" << (int)p[2] << ", " << rdBE<uint64_t>(p+3);
                break;
            case OP_STACK_INIT:
                os << " " << rdBE<uint32_t>(p+1) << ", " << rdBE<uint32_t>(p+5);
                break;
            case OP_JMP:
                os << " @" << rdBE<uint32_t>(p+1);
                break;
            case OP_SHORT_JMP:
                os << " " << (int8_t)p[1];
                break;
            case OP_STACK_PTR_MOVE:
                os << " " << rdBE<uint32_t>(p+1);
                break;
            case OP_NEW_STACK:
                os << " " << rdBE<uint64_t>(p+1);
                break;
            case OP_NEW_HEAP:
                os << " slotOff=" << rdBE<uint32_t>(p+1) << ", size=" << rdBE<uint64_t>(p+5);
                break;
            case OP_DEL_HEAP:
                os << " slotOff=" << rdBE<uint32_t>(p+1);
                break;
            case OP_IF_GOTO:
                os << " cond=" << (int)p[1] << ", @" << rdBE<uint32_t>(p+2);
                break;
            case OP_LEA:
                os << " mode" << (int)p[1] << ", " << rdBE<uint64_t>(p+2);
                break;
            case OP_MOVI_U32:
                os << " " << rdBE<uint32_t>(p+1);
                break;
            case OP_NEW_ARRAY:
                os << " size=" << rdBE<uint64_t>(p+1) << ", slotOff=" << rdBE<uint32_t>(p+9);
                break;
            case OP_FREE_ARRAY:
                os << " slotOff=" << rdBE<uint32_t>(p+1);
                break;
            case OP_ATOMIC_LOAD_U32: case OP_ATOMIC_LOAD_U64:
            case OP_ATOMIC_STORE_U32: case OP_ATOMIC_STORE_U64:
            case OP_ATOMIC_XCHG_U32: case OP_ATOMIC_XCHG_U64:
            case OP_ATOMIC_CAS_U32: case OP_ATOMIC_CAS_U64:
            case OP_ATOMIC_ADD_U32: case OP_ATOMIC_ADD_U64:
                os << " R" << (p[1] & 0x0F) << ", mode" << (int)p[2] << ", " << rdBE<uint64_t>(p+3);
                break;
            case OP_EXTERN_CALL:
                os << " idx=" << (int)p[1] << ", argOff=" << rdBE<uint32_t>(p+2) << ", retOff=" << rdBE<uint32_t>(p+6);
                break;
            case OP_FUNC_CALL:
                os << " fnOff=" << rdBE<uint32_t>(p+1) << ", argOff=" << rdBE<uint32_t>(p+5) << ", retOff=" << rdBE<uint32_t>(p+9);
                break;
            case OP_JIT_SUBMIT:
                os << " fnOff=" << rdBE<uint32_t>(p+1) << ", retOff=" << rdBE<uint32_t>(p+5);
                break;
            case OP_MEMCPY:
                os << " dstMode=" << (int)p[1] << ", dstOff=" << rdBE<uint64_t>(p+2)
                   << ", srcMode=" << (int)p[10] << ", srcOff=" << rdBE<uint64_t>(p+11)
                   << ", size=" << rdBE<uint64_t>(p+19);
                break;
            default:
                break;
            }
            os << "\n";
            off += len;
        }
        return true;
    }

private:
    std::vector<std::string> lines;
    std::vector<Instruction> instructions;
    std::map<std::string, size_t> labels;
    std::set<size_t> unresolvedJumps;
    std::map<std::string, uint8_t> fullOpNameMap;

    std::vector<std::string> splitLines(const std::string& text) {
        std::vector<std::string> result;
        std::istringstream iss(text);
        std::string line;
        while (std::getline(iss, line)) {
            size_t commentPos = line.find(';');
            if (commentPos != std::string::npos) line = line.substr(0, commentPos);
            // 容忍逗号分隔操作数（与反汇编输出格式一致，便于回环）
            std::replace(line.begin(), line.end(), ',', ' ');
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            size_t end = line.find_last_not_of(" \t\r\n");
            line = line.substr(start, end - start + 1);
            if (line.empty()) continue;
            result.push_back(line);
        }
        return result;
    }

    bool firstPass() {
        instructions.clear();
        labels.clear();
        unresolvedJumps.clear();

        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string& line = lines[i];
            if (verbose) std::cerr << "[pass1] " << line << "\n";

            if (line.back() == ':') {
                std::string label = line.substr(0, line.size()-1);
                if (label.empty()) { error("Empty label"); return false; }
                if (labels.count(label)) { error("Duplicate label '" + label + "'"); return false; }
                labels[label] = instructions.size();
                continue;
            }

            if (line[0] == '.') {
                std::istringstream iss(line);
                std::string directive;
                iss >> directive;
                if (directive == ".STACK" || directive == ".ARGS" || directive == ".RETS"
                    || directive == ".ENTRY" || directive == ".ENTRYOFF") {
                    continue;
                } else if (directive == ".BYTE") {
                    std::vector<uint64_t> vals;
                    std::string token;
                    while (iss >> token) {
                        bool ok;
                        uint64_t v = parseNum(token, ok);
                        if (!ok) { error("Invalid .BYTE value: " + token); return false; }
                        vals.push_back(v);
                    }
                    Instruction inst;
                    inst.opcode = 0xFF;
                    inst.operands = vals;
                    instructions.push_back(inst);
                    continue;
                } else if (directive == ".FILL") {
                    std::string cntStr, valStr;
                    if (!(iss >> cntStr >> valStr)) { error("Usage: .FILL count, value"); return false; }
                    bool ok;
                    uint64_t cnt = parseNum(cntStr, ok);
                    if (!ok) { error("Invalid .FILL count"); return false; }
                    uint64_t val = parseNum(valStr, ok);
                    if (!ok) { error("Invalid .FILL value"); return false; }
                    Instruction inst;
                    inst.opcode = 0xFE;
                    inst.operands = {cnt, val};
                    instructions.push_back(inst);
                    continue;
                } else {
                    error("Unknown directive: " + directive);
                    return false;
                }
            }

            std::istringstream iss(line);
            std::string mnemonic;
            iss >> mnemonic;
            for (char& c : mnemonic) c = toupper(c);

            auto it = fullOpNameMap.find(mnemonic);
            if (it == fullOpNameMap.end()) {
                error("Unknown instruction: " + mnemonic);
                return false;
            }
            uint8_t opcode = it->second;

            std::vector<std::string> tokens;
            std::string tok;
            while (iss >> tok) tokens.push_back(tok);

            Instruction inst;
            inst.opcode = opcode;

            if (!parseOperands(inst, opcode, tokens, i)) return false;

            instructions.push_back(inst);
        }

        return true;
    }

    bool parseOperands(Instruction& inst, uint8_t opcode, const std::vector<std::string>& tokens, size_t lineIdx) {
        auto expectCount = [&](size_t n) -> bool {
            if (tokens.size() != n) {
                error("Expected " + std::to_string(n) + " operand(s) at line " + std::to_string(lineIdx+1));
                return false;
            }
            return true;
        };

        switch (opcode) {
        case OP_REG_MOVI_U8: case OP_REG_MOVI_U16: case OP_REG_MOVI_U32: case OP_REG_MOVI_U64: {
            if (!expectCount(2)) return false;
            bool ok; int r = parseReg(tokens[0], ok);
            if (!ok) { error("Bad register: " + tokens[0]); return false; }
            uint64_t imm = parseNum(tokens[1], ok);
            if (!ok) { error("Bad immediate: " + tokens[1]); return false; }
            inst.operands = {static_cast<uint64_t>(r), imm};
            return true;
        }
        case OP_REG_MOV: {
            if (!expectCount(2)) return false;
            bool ok; int r1 = parseReg(tokens[0], ok);
            if (!ok) { error("Bad register: " + tokens[0]); return false; }
            int r2 = parseReg(tokens[1], ok);
            if (!ok) { error("Bad register: " + tokens[1]); return false; }
            inst.operands = {static_cast<uint64_t>(r1), static_cast<uint64_t>(r2)};
            return true;
        }
        case OP_REG_PUSH_U8: case OP_REG_PUSH_U16: case OP_REG_PUSH_U32: case OP_REG_PUSH_U64:
        case OP_REG_POP_U8: case OP_REG_POP_U16: case OP_REG_POP_U32: case OP_REG_POP_U64: {
            if (!expectCount(1)) return false;
            bool ok; int r = parseReg(tokens[0], ok);
            if (!ok) { error("Bad register: " + tokens[0]); return false; }
            inst.operands = {static_cast<uint64_t>(r)};
            return true;
        }
        case OP_REG_LOAD_U8: case OP_REG_LOAD_U16: case OP_REG_LOAD_U32: case OP_REG_LOAD_U64:
        case OP_REG_STORE_U8: case OP_REG_STORE_U16: case OP_REG_STORE_U32: case OP_REG_STORE_U64: {
            if (!expectCount(3)) return false;
            bool ok; int r = parseReg(tokens[0], ok);
            if (!ok) { error("Bad register: " + tokens[0]); return false; }
            uint64_t mode = parseNum(tokens[1], ok);
            if (!ok || mode > 1) { error("Bad mode (0 or 1): " + tokens[1]); return false; }
            uint64_t off = parseNum(tokens[2], ok);
            if (!ok) { error("Bad offset: " + tokens[2]); return false; }
            inst.operands = {static_cast<uint64_t>(r), mode, off};
            return true;
        }
        case OP_STACK_INIT: {
            if (!expectCount(2)) return false;
            bool ok;
            uint64_t s1 = parseNum(tokens[0], ok);
            if (!ok) { error("Bad stack size: " + tokens[0]); return false; }
            uint64_t s2 = parseNum(tokens[1], ok);
            if (!ok) { error("Bad scope size: " + tokens[1]); return false; }
            inst.operands = {s1, s2};
            return true;
        }
        case OP_JMP: {
            if (!expectCount(1)) return false;
            if (tokens[0][0] == '@') {
                inst.labelRefs.push_back(tokens[0].substr(1));
                unresolvedJumps.insert(instructions.size());
            } else {
                bool ok;
                uint64_t addr = parseNum(tokens[0], ok);
                if (!ok) { error("Bad jump target: " + tokens[0]); return false; }
                inst.operands = {addr};
            }
            return true;
        }
        case OP_SHORT_JMP: {
            if (!expectCount(1)) return false;
            bool ok;
            int64_t rel = static_cast<int64_t>(parseNum(tokens[0], ok));
            if (!ok) { error("Bad relative offset: " + tokens[0]); return false; }
            inst.operands = {static_cast<uint64_t>(static_cast<uint8_t>(rel & 0xFF))};
            return true;
        }
        case OP_STACK_PTR_MOVE: {
            if (!expectCount(1)) return false;
            bool ok;
            uint64_t dec = parseNum(tokens[0], ok);
            if (!ok) { error("Bad decrement: " + tokens[0]); return false; }
            inst.operands = {dec};
            return true;
        }
        case OP_NEW_STACK: {
            if (!expectCount(1)) return false;
            bool ok;
            uint64_t bytes = parseNum(tokens[0], ok);
            if (!ok) { error("Bad bytes: " + tokens[0]); return false; }
            inst.operands = {bytes};
            return true;
        }
        case OP_NEW_HEAP: {
            if (!expectCount(2)) return false;
            bool ok;
            uint64_t slotOff = parseNum(tokens[0], ok);
            if (!ok) { error("Bad slotOff: " + tokens[0]); return false; }
            uint64_t size = parseNum(tokens[1], ok);
            if (!ok) { error("Bad size: " + tokens[1]); return false; }
            inst.operands = {slotOff, size};
            return true;
        }
        case OP_DEL_HEAP: {
            if (!expectCount(1)) return false;
            bool ok;
            uint64_t slotOff = parseNum(tokens[0], ok);
            if (!ok) { error("Bad slotOff: " + tokens[0]); return false; }
            inst.operands = {slotOff};
            return true;
        }
        case OP_IF_GOTO: {
            if (!expectCount(2)) return false;
            bool ok;
            uint64_t cond = parseNum(tokens[0], ok);
            if (!ok || cond > 255) { error("Bad condition: " + tokens[0]); return false; }
            if (tokens[1][0] == '@') {
                inst.labelRefs.push_back(tokens[1].substr(1));
                unresolvedJumps.insert(instructions.size());
                inst.operands = {cond};
            } else {
                uint64_t addr = parseNum(tokens[1], ok);
                if (!ok) { error("Bad jump target: " + tokens[1]); return false; }
                inst.operands = {cond, addr};
            }
            return true;
        }
        case OP_LEA: {
            if (!expectCount(2)) return false;
            bool ok;
            uint64_t mode = parseNum(tokens[0], ok);
            if (!ok || mode > 1) { error("Bad mode: " + tokens[0]); return false; }
            uint64_t off = parseNum(tokens[1], ok);
            if (!ok) { error("Bad offset: " + tokens[1]); return false; }
            inst.operands = {mode, off};
            return true;
        }
        case OP_MOVI_U32: {
            if (!expectCount(1)) return false;
            bool ok;
            uint64_t imm = parseNum(tokens[0], ok);
            if (!ok) { error("Bad immediate: " + tokens[0]); return false; }
            inst.operands = {imm};
            return true;
        }
        case OP_NEW_ARRAY: {
            if (!expectCount(2)) return false;
            bool ok;
            uint64_t size = parseNum(tokens[0], ok);
            if (!ok) { error("Bad size: " + tokens[0]); return false; }
            uint64_t slotOff = parseNum(tokens[1], ok);
            if (!ok) { error("Bad slotOff: " + tokens[1]); return false; }
            inst.operands = {size, slotOff};
            return true;
        }
        case OP_FREE_ARRAY: {
            if (!expectCount(1)) return false;
            bool ok;
            uint64_t slotOff = parseNum(tokens[0], ok);
            if (!ok) { error("Bad slotOff: " + tokens[0]); return false; }
            inst.operands = {slotOff};
            return true;
        }
        case OP_ATOMIC_LOAD_U32: case OP_ATOMIC_LOAD_U64:
        case OP_ATOMIC_STORE_U32: case OP_ATOMIC_STORE_U64:
        case OP_ATOMIC_XCHG_U32: case OP_ATOMIC_XCHG_U64:
        case OP_ATOMIC_CAS_U32: case OP_ATOMIC_CAS_U64:
        case OP_ATOMIC_ADD_U32: case OP_ATOMIC_ADD_U64: {
            if (!expectCount(3)) return false;
            bool ok; int r = parseReg(tokens[0], ok);
            if (!ok) { error("Bad register: " + tokens[0]); return false; }
            uint64_t mode = parseNum(tokens[1], ok);
            if (!ok || mode > 1) { error("Bad mode: " + tokens[1]); return false; }
            uint64_t off = parseNum(tokens[2], ok);
            if (!ok) { error("Bad offset: " + tokens[2]); return false; }
            inst.operands = {static_cast<uint64_t>(r), mode, off};
            return true;
        }
        case OP_EXTERN_CALL: {
            if (!expectCount(3)) return false;
            bool ok;
            uint64_t idx = parseNum(tokens[0], ok);
            if (!ok || idx > 255) { error("Bad idx: " + tokens[0]); return false; }
            uint64_t argOff = parseNum(tokens[1], ok);
            if (!ok) { error("Bad argOff: " + tokens[1]); return false; }
            uint64_t retOff = parseNum(tokens[2], ok);
            if (!ok) { error("Bad retOff: " + tokens[2]); return false; }
            inst.operands = {idx, argOff, retOff};
            return true;
        }
        case OP_JIT_SUBMIT: {
            if (!expectCount(2)) return false;
            bool ok;
            uint64_t fnOff = parseNum(tokens[0], ok);
            if (!ok) { error("Bad fnOff: " + tokens[0]); return false; }
            uint64_t retOff = parseNum(tokens[1], ok);
            if (!ok) { error("Bad retOff: " + tokens[1]); return false; }
            inst.operands = {fnOff, retOff};
            return true;
        }
        case OP_MEMCPY: {
            if (!expectCount(5)) return false;
            bool ok;
            uint64_t dstMode = parseNum(tokens[0], ok);
            if (!ok || dstMode > 1) { error("Bad dstMode: " + tokens[0]); return false; }
            uint64_t dstOff = parseNum(tokens[1], ok);
            if (!ok) { error("Bad dstOff: " + tokens[1]); return false; }
            uint64_t srcMode = parseNum(tokens[2], ok);
            if (!ok || srcMode > 1) { error("Bad srcMode: " + tokens[2]); return false; }
            uint64_t srcOff = parseNum(tokens[3], ok);
            if (!ok) { error("Bad srcOff: " + tokens[3]); return false; }
            uint64_t size = parseNum(tokens[4], ok);
            if (!ok) { error("Bad size: " + tokens[4]); return false; }
            inst.operands = {dstMode, dstOff, srcMode, srcOff, size};
            return true;
        }
        default: {
            if (!tokens.empty()) {
                error("Instruction " + std::to_string(opcode) + " does not take operands");
                return false;
            }
            return true;
        }
        }
        return true;
    }

    bool resolveLabels() {
        size_t offset = 0;
        for (size_t i = 0; i < instructions.size(); ++i) {
            instructions[i].offset = offset;
            if (instructions[i].opcode == 0xFF) {
                offset += instructions[i].operands.size();
            } else if (instructions[i].opcode == 0xFE) {
                offset += instructions[i].operands[0];
            } else {
                offset += instrLen(instructions[i].opcode);
            }
        }

        for (size_t i : unresolvedJumps) {
            Instruction& inst = instructions[i];
            if (inst.labelRefs.empty()) continue;
            const std::string& label = inst.labelRefs[0];
            auto it = labels.find(label);
            if (it == labels.end()) {
                error("Undefined label: " + label);
                return false;
            }
            size_t targetIdx = it->second;
            if (targetIdx >= instructions.size()) {
                error("Label points to invalid instruction index");
                return false;
            }
            size_t targetOffset = instructions[targetIdx].offset;
            if (inst.opcode == OP_JMP) {
                inst.operands = {static_cast<uint64_t>(targetOffset)};
            } else if (inst.opcode == OP_IF_GOTO) {
                if (inst.operands.size() == 1) {
                    inst.operands.push_back(static_cast<uint64_t>(targetOffset));
                } else {
                    inst.operands[1] = targetOffset;
                }
            } else {
                error("Unsupported label reference for opcode " + std::to_string(inst.opcode));
                return false;
            }
        }
        return true;
    }

    bool emit(std::vector<uint8_t>& outBytes, uint32_t& argSize, uint32_t& retSize, uint32_t& entry) {
        argSize = 0;
        retSize = 0;
        entry = 0;
        bool hasEntry = false;
        for (const auto& line : lines) {
            if (line[0] == '.') {
                std::istringstream iss(line);
                std::string dir;
                iss >> dir;
                if (dir == ".ARGS") {
                    std::string val; iss >> val;
                    bool ok; argSize = static_cast<uint32_t>(parseNum(val, ok));
                    if (!ok) { error("Bad .ARGS value"); return false; }
                } else if (dir == ".RETS") {
                    std::string val; iss >> val;
                    bool ok; retSize = static_cast<uint32_t>(parseNum(val, ok));
                    if (!ok) { error("Bad .RETS value"); return false; }
                } else if (dir == ".ENTRY") {
                    std::string val; iss >> val;
                    bool ok; entry = static_cast<uint32_t>(parseNum(val, ok));
                    if (!ok) { error("Bad .ENTRY value"); return false; }
                    hasEntry = true;
                } else if (dir == ".ENTRYOFF") {
                    std::string label; iss >> label;
                    auto it = labels.find(label);
                    if (it == labels.end()) { error("Undefined label for .ENTRYOFF"); return false; }
                    size_t idx = it->second;
                    if (idx >= instructions.size()) { error("Label index out of range"); return false; }
                    entry = static_cast<uint32_t>(instructions[idx].offset);
                    hasEntry = true;
                }
            }
        }
        if (!hasEntry) {
            entry = 0;
        }

        if (!pureBinary) {
            uint8_t header[12];
            if (littleEndianHeader) {
                wrLE<uint32_t>(header, argSize);
                wrLE<uint32_t>(header+4, retSize);
                wrLE<uint32_t>(header+8, entry);
            } else {
                wrBE<uint32_t>(header, argSize);
                wrBE<uint32_t>(header+4, retSize);
                wrBE<uint32_t>(header+8, entry);
            }
            outBytes.insert(outBytes.end(), header, header+12);
        }

        for (const auto& inst : instructions) {
            if (inst.opcode == 0xFF) {
                for (uint64_t v : inst.operands) {
                    outBytes.push_back(static_cast<uint8_t>(v & 0xFF));
                }
            } else if (inst.opcode == 0xFE) {
                uint64_t count = inst.operands[0];
                uint64_t val = inst.operands[1];
                for (uint64_t j = 0; j < count; ++j) {
                    outBytes.push_back(static_cast<uint8_t>(val & 0xFF));
                }
            } else {
                uint8_t op = inst.opcode;
                outBytes.push_back(op);
                switch (op) {
                case OP_REG_MOVI_U8: {
                    uint8_t reg = static_cast<uint8_t>(inst.operands[0] & 0x0F);
                    uint8_t imm = static_cast<uint8_t>(inst.operands[1] & 0xFF);
                    outBytes.push_back(reg);
                    outBytes.push_back(imm);
                    break;
                }
                case OP_REG_MOVI_U16: {
                    uint8_t reg = static_cast<uint8_t>(inst.operands[0] & 0x0F);
                    uint16_t imm = static_cast<uint16_t>(inst.operands[1]);
                    outBytes.push_back(reg);
                    outBytes.resize(outBytes.size()+2);
                    wrBE<uint16_t>(&outBytes[outBytes.size()-2], imm);
                    break;
                }
                case OP_REG_MOVI_U32: {
                    uint8_t reg = static_cast<uint8_t>(inst.operands[0] & 0x0F);
                    uint32_t imm = static_cast<uint32_t>(inst.operands[1]);
                    outBytes.push_back(reg);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], imm);
                    break;
                }
                case OP_REG_MOVI_U64: {
                    uint8_t reg = static_cast<uint8_t>(inst.operands[0] & 0x0F);
                    uint64_t imm = inst.operands[1];
                    outBytes.push_back(reg);
                    outBytes.resize(outBytes.size()+8);
                    wrBE<uint64_t>(&outBytes[outBytes.size()-8], imm);
                    break;
                }
                case OP_REG_MOV: {
                    uint8_t dst = static_cast<uint8_t>(inst.operands[0] & 0x0F);
                    uint8_t src = static_cast<uint8_t>(inst.operands[1] & 0x0F);
                    outBytes.push_back(dst);
                    outBytes.push_back(src);
                    break;
                }
                case OP_REG_PUSH_U8: case OP_REG_PUSH_U16: case OP_REG_PUSH_U32: case OP_REG_PUSH_U64:
                case OP_REG_POP_U8: case OP_REG_POP_U16: case OP_REG_POP_U32: case OP_REG_POP_U64: {
                    uint8_t reg = static_cast<uint8_t>(inst.operands[0] & 0x0F);
                    outBytes.push_back(reg);
                    break;
                }
                case OP_REG_LOAD_U8: case OP_REG_LOAD_U16: case OP_REG_LOAD_U32: case OP_REG_LOAD_U64:
                case OP_REG_STORE_U8: case OP_REG_STORE_U16: case OP_REG_STORE_U32: case OP_REG_STORE_U64: {
                    uint8_t reg = static_cast<uint8_t>(inst.operands[0] & 0x0F);
                    uint8_t mode = static_cast<uint8_t>(inst.operands[1] & 0xFF);
                    uint64_t off = inst.operands[2];
                    outBytes.push_back(reg);
                    outBytes.push_back(mode);
                    outBytes.resize(outBytes.size()+8);
                    wrBE<uint64_t>(&outBytes[outBytes.size()-8], off);
                    break;
                }
                case OP_STACK_INIT: {
                    uint32_t stackSize = static_cast<uint32_t>(inst.operands[0]);
                    uint32_t scopeSize = static_cast<uint32_t>(inst.operands[1]);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], stackSize);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], scopeSize);
                    break;
                }
                case OP_JMP: {
                    uint32_t addr = static_cast<uint32_t>(inst.operands[0]);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], addr);
                    break;
                }
                case OP_SHORT_JMP: {
                    uint8_t rel = static_cast<uint8_t>(inst.operands[0] & 0xFF);
                    outBytes.push_back(rel);
                    break;
                }
                case OP_STACK_PTR_MOVE: {
                    uint32_t dec = static_cast<uint32_t>(inst.operands[0]);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], dec);
                    break;
                }
                case OP_NEW_STACK: {
                    uint64_t bytes = inst.operands[0];
                    outBytes.resize(outBytes.size()+8);
                    wrBE<uint64_t>(&outBytes[outBytes.size()-8], bytes);
                    break;
                }
                case OP_NEW_HEAP: {
                    uint32_t slotOff = static_cast<uint32_t>(inst.operands[0]);
                    uint64_t size = inst.operands[1];
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], slotOff);
                    outBytes.resize(outBytes.size()+8);
                    wrBE<uint64_t>(&outBytes[outBytes.size()-8], size);
                    break;
                }
                case OP_DEL_HEAP: {
                    uint32_t slotOff = static_cast<uint32_t>(inst.operands[0]);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], slotOff);
                    break;
                }
                case OP_IF_GOTO: {
                    uint8_t cond = static_cast<uint8_t>(inst.operands[0] & 0xFF);
                    uint32_t addr = static_cast<uint32_t>(inst.operands[1]);
                    outBytes.push_back(cond);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], addr);
                    break;
                }
                case OP_LEA: {
                    uint8_t mode = static_cast<uint8_t>(inst.operands[0] & 0xFF);
                    uint64_t off = inst.operands[1];
                    outBytes.push_back(mode);
                    outBytes.resize(outBytes.size()+8);
                    wrBE<uint64_t>(&outBytes[outBytes.size()-8], off);
                    break;
                }
                case OP_MOVI_U32: {
                    uint32_t imm = static_cast<uint32_t>(inst.operands[0]);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], imm);
                    break;
                }
                case OP_NEW_ARRAY: {
                    uint64_t size = inst.operands[0];
                    uint32_t slotOff = static_cast<uint32_t>(inst.operands[1]);
                    outBytes.resize(outBytes.size()+8);
                    wrBE<uint64_t>(&outBytes[outBytes.size()-8], size);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], slotOff);
                    break;
                }
                case OP_FREE_ARRAY: {
                    uint32_t slotOff = static_cast<uint32_t>(inst.operands[0]);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], slotOff);
                    break;
                }
                case OP_ATOMIC_LOAD_U32: case OP_ATOMIC_LOAD_U64:
                case OP_ATOMIC_STORE_U32: case OP_ATOMIC_STORE_U64:
                case OP_ATOMIC_XCHG_U32: case OP_ATOMIC_XCHG_U64:
                case OP_ATOMIC_CAS_U32: case OP_ATOMIC_CAS_U64:
                case OP_ATOMIC_ADD_U32: case OP_ATOMIC_ADD_U64: {
                    uint8_t reg = static_cast<uint8_t>(inst.operands[0] & 0x0F);
                    uint8_t mode = static_cast<uint8_t>(inst.operands[1] & 0xFF);
                    uint64_t off = inst.operands[2];
                    outBytes.push_back(reg);
                    outBytes.push_back(mode);
                    outBytes.resize(outBytes.size()+8);
                    wrBE<uint64_t>(&outBytes[outBytes.size()-8], off);
                    break;
                }
                case OP_EXTERN_CALL: {
                    uint8_t idx = static_cast<uint8_t>(inst.operands[0] & 0xFF);
                    uint32_t argOff = static_cast<uint32_t>(inst.operands[1]);
                    uint32_t retOff = static_cast<uint32_t>(inst.operands[2]);
                    outBytes.push_back(idx);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], argOff);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], retOff);
                    break;
                }
                case OP_FUNC_CALL: {
                    uint32_t fnOff = static_cast<uint32_t>(inst.operands[0]);
                    uint32_t argOff = static_cast<uint32_t>(inst.operands[1]);
                    uint32_t retOff = static_cast<uint32_t>(inst.operands[2]);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], fnOff);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], argOff);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], retOff);
                    break;
                }
                case OP_JIT_SUBMIT: {
                    uint32_t fnOff = static_cast<uint32_t>(inst.operands[0]);
                    uint32_t retOff = static_cast<uint32_t>(inst.operands[1]);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], fnOff);
                    outBytes.resize(outBytes.size()+4);
                    wrBE<uint32_t>(&outBytes[outBytes.size()-4], retOff);
                    break;
                }
                case OP_MEMCPY: {
                    uint8_t dstMode = static_cast<uint8_t>(inst.operands[0] & 0xFF);
                    uint64_t dstOff = inst.operands[1];
                    uint8_t srcMode = static_cast<uint8_t>(inst.operands[2] & 0xFF);
                    uint64_t srcOff = inst.operands[3];
                    uint64_t size = inst.operands[4];
                    outBytes.push_back(dstMode);
                    outBytes.resize(outBytes.size()+8);
                    wrBE<uint64_t>(&outBytes[outBytes.size()-8], dstOff);
                    outBytes.push_back(srcMode);
                    outBytes.resize(outBytes.size()+8);
                    wrBE<uint64_t>(&outBytes[outBytes.size()-8], srcOff);
                    outBytes.resize(outBytes.size()+8);
                    wrBE<uint64_t>(&outBytes[outBytes.size()-8], size);
                    break;
                }
                default:
                    break;
                }
            }
        }
        return true;
    }

    void error(const std::string& msg) {
        std::cerr << "Error: " << msg << "\n";
    }
};

} // namespace jadeight
