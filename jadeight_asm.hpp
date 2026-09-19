// ============================================================================
// Jadeight / jadeight_asm.hpp —— ISA v3 汇编器 + 反汇编器（header-only 库）
// ============================================================================
// 与 v2 汇编器的差别（v3 是合并式指令集，ISA 定义见 Jadeight2ReWrite/isa.hpp）：
//   - 类型不再是 opcode 的一部分，而是操作数：`ADD u8`、`MOVI u64 R0, 123`
//   - 操作数一律小端（v2 是「12 字节头 LE + 指令流操作数 BE」的混搭）
//   - 跳转只有标签跳转：`JMP L1` / `BRANCH L1`，标签是**函数相对**偏移
//   - 多函数：`.FUNC <名> [参数字节] [返回字节]` 划分函数，产出函数目录
//   - 输出 = v3 模块文件（magic "J3BC"），不再是「12 字节头 + 单函数码流」
//
// 兼容旧调用方式：`pureBinary = true` 时 `assemble()` 只输出纯码流（j8c 用），
// 函数目录通过 `funcs` 取。
// ============================================================================
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "isa.hpp"

namespace jadeight {

[[gnu::always_inline]] inline uint64_t asmLoadRaw(const uint8_t* p, uint8_t w) {
    uint64_t r = 0;
    for (uint8_t i = 0; i < w; ++i) r |= static_cast<uint64_t>(p[i]) << (8 * i);
    return r;
}

// ============================================================================
// 助记名 / 操作数解析
// ============================================================================
// 助记名表只有一份：isa.hpp 的 opName（这里原本抄了一份，改指令集时两边容易漂）
inline bool mnemonicToOp(const std::string& s, uint8_t& op) {
    if (s.empty()) return false;
    static const uint8_t known[] = {
        OP_TRAP, OP_NOP, OP_HALT, OP_JMP, OP_BRANCH, OP_CALL, OP_CALL_IND, OP_RET, OP_JMP_IND,
        OP_STACK_INIT, OP_SCOPE_PUSH, OP_SCOPE_POP, OP_STACK_MOVE, OP_STACK_ALLOC,
        OP_LOAD, OP_STORE, OP_LEA, OP_NEW_HEAP, OP_DEL_HEAP, OP_NEW_ARRAY, OP_FREE_ARRAY,
        OP_MEMCPY, OP_GET_ADDRS, OP_MOVI, OP_MOV, OP_PUSH_REG, OP_POP_REG, OP_PUSH_IMM,
        OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_REM, OP_NEG, OP_SHL, OP_SHR, OP_AND, OP_OR,
        OP_XOR, OP_NOT, OP_CMP, OP_SQRT, OP_LOG, OP_CVT, OP_COUT, OP_GET_SYSTEM,
        OP_EXTERN_CALL, OP_DL_REG, OP_DL_CALL, OP_JIT_SUBMIT,
        OP_ATOMIC_LOAD, OP_ATOMIC_STORE, OP_ATOMIC_XCHG, OP_ATOMIC_CAS, OP_ATOMIC_ADD,
        OP_FUNC_ADDR, OP_THREAD_SPAWN, OP_THREAD_JOIN, OP_THREAD_EXIT, OP_THREAD_SELF,
        OP_THREAD_YIELD, OP_PROC_SPAWN, OP_PROC_WAIT, OP_PROC_EXIT, OP_PROC_SELF, OP_PROC_SHARE,
        OP_JIT_COMPILE, OP_CALL_NATIVE,
    };
    for (uint8_t k : known)
        if (s == opName(k)) { op = k; return true; }
    return false;
}

inline bool tdFromName(const std::string& s, uint8_t& td) {
    if (s == "u8") td = TD_U8;         else if (s == "u16") td = TD_U16;
    else if (s == "u32") td = TD_U32;  else if (s == "u64") td = TD_U64;
    else if (s == "i8") td = TD_I8;    else if (s == "i16") td = TD_I16;
    else if (s == "i32") td = TD_I32;  else if (s == "i64") td = TD_I64;
    else if (s == "f32") td = TD_F32;  else if (s == "f64") td = TD_F64;
    else if (s == "ptr") td = TD_PTR;
    else return false;
    return true;
}

inline bool cmpFromName(const std::string& s, uint8_t& sub) {
    if (s == "LT") sub = CMP_LT;      else if (s == "LE") sub = CMP_LE;
    else if (s == "EQ") sub = CMP_EQ; else if (s == "NE") sub = CMP_NE;
    else if (s == "GT") sub = CMP_GT; else if (s == "GE") sub = CMP_GE;
    else return false;
    return true;
}

inline bool parseU64(const std::string& s, uint64_t& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        v = std::strtoull(s.c_str() + 2, &end, 16);
        return end && *end == 0;
    }
    if (s[0] == '-' || s[0] == '+') { // 负数按二进制补码
        const long long x = std::strtoll(s.c_str(), &end, 0);
        if (end && *end == 0) { v = static_cast<uint64_t>(x); return true; }
        return false;
    }
    if (s.size() >= 3 && s.front() == '\'' && s.back() == '\'') { // 字符字面量
        v = static_cast<uint64_t>(static_cast<unsigned char>(s[1]));
        return true;
    }
    v = std::strtoull(s.c_str(), &end, 10);
    return end && *end == 0;
}

inline bool parseReg(const std::string& s, uint8_t& r) {
    std::string t = s;
    if (!t.empty() && (t[0] == 'R' || t[0] == 'r')) t = t.substr(1);
    uint64_t v = 0;
    if (!parseU64(t, v) || v > 15) return false;
    r = static_cast<uint8_t>(v);
    return true;
}

// ============================================================================
// 词法：一行汇编
// ============================================================================
struct AsmLine {
    std::string label;      // 可空
    std::string directive;  // ".FUNC" 等（含点）；可空
    std::string mnemonic;   // 可空
    std::vector<std::string> args;
    std::string raw;
};

inline void asmSplitTokens(const std::string& s, std::vector<std::string>& out) {
    std::string cur;
    for (char ch : s) {
        if (ch == ' ' || ch == '\t' || ch == ',') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur.push_back(ch);
    }
    if (!cur.empty()) out.push_back(cur);
}

inline std::vector<AsmLine> parseAsmLines(const std::string& text) {
    std::vector<AsmLine> out;
    std::istringstream is(text);
    std::string raw;
    while (std::getline(is, raw)) {
        std::string line = raw;
        const size_t sc = line.find(';');
        if (sc != std::string::npos) line = line.substr(0, sc);
        const size_t sl = line.find("//");
        if (sl != std::string::npos) line = line.substr(0, sl);
        const size_t b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        const size_t e = line.find_last_not_of(" \t\r\n");
        line = line.substr(b, e - b + 1);

        AsmLine L;
        L.raw = line;
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string head = line.substr(0, colon);
            bool looksLabel = !head.empty();
            if (looksLabel && (isdigit(static_cast<unsigned char>(head[0])))) looksLabel = false;
            for (char ch : head)
                if (!(isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.')) { looksLabel = false; break; }
            if (looksLabel) {
                L.label = head;
                line = line.substr(colon + 1);
                const size_t b2 = line.find_first_not_of(" \t");
                line = (b2 == std::string::npos) ? "" : line.substr(b2);
            }
        }
        if (!line.empty()) {
            std::vector<std::string> toks;
            asmSplitTokens(line, toks);
            if (!toks.empty()) {
                if (toks[0][0] == '.') L.directive = toks[0];
                else L.mnemonic = toks[0];
                L.args.assign(toks.begin() + 1, toks.end());
            }
        }
        if (L.label.empty() && L.directive.empty() && L.mnemonic.empty()) continue;
        out.push_back(L);
    }
    return out;
}

// ============================================================================
// 汇编器
// ============================================================================
class Assembler {
public:
    bool verbose = false;
    bool pureBinary = false;        // true：只输出纯码流（j8c 内部用）
    bool littleEndianHeader = true; // v2 兼容开关；v3 一律小端，无实际作用
    std::vector<ModuleFunc> funcs;  // 汇编产物：函数目录
    std::string inputPath;          // 兼容旧 CLI 的字段（汇编器本身不用）
    std::string outputPath;         // 兼容旧 CLI 的字段（汇编器本身不用）
    std::string error;

    bool assemble(const std::string& asmText, std::vector<uint8_t>& outBytes,
                  uint32_t& argSize, uint32_t& retSize, uint32_t& entry) {
        ModuleImage img;
        if (!assembleModule(asmText, img)) return false;
        argSize = entryArgSize_;
        retSize = entryRetSize_;
        entry = img.funcs.empty() ? 0 : img.funcs[entryFunc_].entry;
        outBytes = pureBinary ? img.code : img.serialize();
        return true;
    }

    bool assembleModule(const std::string& asmText, ModuleImage& img) {
        error.clear();
        funcs.clear();
        img.funcs.clear();
        img.code.clear();

        const std::vector<AsmLine> lines = parseAsmLines(asmText);

        struct Pending { uint32_t at; int funcIdx; std::string name; bool isFunc; };
        std::vector<Pending> pending;
        std::vector<std::map<std::string, uint32_t>> labels;

        struct FuncDef { std::string name; uint32_t start = 0, argSize = 0, retSize = 0; };
        std::vector<FuncDef> defs;
        std::map<std::string, int> funcIndex;
        int cur = -1;
        uint32_t argsLegacy = 0, retsLegacy = 0;
        bool haveEntryNum = false;
        uint32_t entryNum = 0;
        std::string entryName;

        auto emitU8 = [&](uint8_t v) { img.code.push_back(v); };
        auto emitU16 = [&](uint16_t v) { emitU8(uint8_t(v)); emitU8(uint8_t(v >> 8)); };
        auto emitU32 = [&](uint32_t v) { emitU16(uint16_t(v)); emitU16(uint16_t(v >> 16)); };
        auto emitU64 = [&](uint64_t v) { emitU32(uint32_t(v)); emitU32(uint32_t(v >> 32)); };

        auto startFunc = [&](const std::string& name, uint32_t as, uint32_t rs) {
            FuncDef d;
            d.name = name.empty() ? ("fn" + std::to_string(defs.size())) : name;
            d.start = static_cast<uint32_t>(img.code.size());
            d.argSize = as;
            d.retSize = rs;
            defs.push_back(d);
            labels.emplace_back();
            funcIndex[d.name] = static_cast<int>(defs.size() - 1);
            cur = static_cast<int>(defs.size() - 1);
        };

        for (const AsmLine& L : lines) {
            if (!L.label.empty()) {
                if (cur < 0) startFunc("main", 0, 0);
                labels[static_cast<size_t>(cur)][L.label] =
                    static_cast<uint32_t>(img.code.size()) - defs[static_cast<size_t>(cur)].start;
            }
            if (!L.directive.empty()) {
                const std::string& d = L.directive;
                if (d == ".FUNC") {
                    uint64_t as = 0, rs = 0;
                    if (L.args.size() >= 2) parseU64(L.args[1], as);
                    if (L.args.size() >= 3) parseU64(L.args[2], rs);
                    startFunc(L.args.empty() ? "" : L.args[0], static_cast<uint32_t>(as), static_cast<uint32_t>(rs));
                } else if (d == ".ARGS") {
                    uint64_t t = 0;
                    if (!L.args.empty() && parseU64(L.args[0], t)) argsLegacy = static_cast<uint32_t>(t);
                } else if (d == ".RETS") {
                    uint64_t t = 0;
                    if (!L.args.empty() && parseU64(L.args[0], t)) retsLegacy = static_cast<uint32_t>(t);
                } else if (d == ".ENTRY") {
                    if (!L.args.empty()) {
                        uint64_t v = 0;
                        if (parseU64(L.args[0], v)) { haveEntryNum = true; entryNum = static_cast<uint32_t>(v); }
                        else entryName = L.args[0];
                    }
                } else if (d == ".STACK" || d == ".SCOPE") {
                    // 运行时由 STACK_INIT 决定；这里接受并忽略
                } else if (d == ".BYTE") {
                    for (const auto& a : L.args) {
                        uint64_t v = 0;
                        if (!parseU64(a, v)) { error = "非法 .BYTE 操作数: " + a; return false; }
                        emitU8(static_cast<uint8_t>(v));
                    }
                } else if (d == ".FILL") {
                    uint64_t n = 0, v = 0;
                    if (L.args.empty() || !parseU64(L.args[0], n)) { error = "非法 .FILL"; return false; }
                    if (L.args.size() >= 2) parseU64(L.args[1], v);
                    for (uint64_t i = 0; i < n; ++i) emitU8(static_cast<uint8_t>(v));
                } else {
                    error = "未知伪指令: " + d;
                    return false;
                }
                continue;
            }
            if (L.mnemonic.empty()) continue;
            if (cur < 0) startFunc("main", 0, 0);

            uint8_t op = 0;
            if (!mnemonicToOp(L.mnemonic, op)) { error = "未知指令: " + L.mnemonic; return false; }
            const int fidx = cur;
            const uint32_t before = static_cast<uint32_t>(img.code.size());
            emitU8(op);

            auto argErr = [&](const char* what) {
                error = std::string("指令缺少/非法操作数: ") + L.mnemonic + " 需要 " + what + "  (" + L.raw + ")";
                return false;
            };
            uint8_t td = 0, r = 0, sub = 0;
            uint64_t v = 0, a = 0, b = 0, c = 0, d2 = 0, e2 = 0;

            switch (op) {
                case OP_TRAP: case OP_NOP: case OP_HALT: case OP_RET: case OP_JMP_IND:
                case OP_SCOPE_PUSH: case OP_SCOPE_POP: case OP_GET_ADDRS: case OP_GET_SYSTEM:
                case OP_THREAD_EXIT: case OP_THREAD_YIELD: case OP_PROC_EXIT:
                    break;

                case OP_JMP: case OP_BRANCH: {
                    if (L.args.size() < 1) return argErr("label");
                    if (parseU64(L.args[0], v)) emitU32(static_cast<uint32_t>(v));
                    else { pending.push_back({static_cast<uint32_t>(img.code.size()), fidx, L.args[0], false}); emitU32(0); }
                    break;
                }
                case OP_CALL: case OP_THREAD_SPAWN: {
                    if (L.args.size() < 3) return argErr("func, argOff, retOff");
                    if (parseU64(L.args[0], v)) emitU16(static_cast<uint16_t>(v));
                    else { pending.push_back({static_cast<uint32_t>(img.code.size()), fidx, L.args[0], true}); emitU16(0); }
                    parseU64(L.args[1], a); parseU64(L.args[2], b);
                    emitU32(static_cast<uint32_t>(a)); emitU32(static_cast<uint32_t>(b));
                    break;
                }
                case OP_JIT_COMPILE: {
                    if (L.args.size() < 3) return argErr("ptrOff, lenOff, outOff");
                    parseU64(L.args[0], a); parseU64(L.args[1], b); parseU64(L.args[2], c);
                    emitU32(static_cast<uint32_t>(a)); emitU32(static_cast<uint32_t>(b)); emitU32(static_cast<uint32_t>(c));
                    break;
                }
                case OP_CALL_NATIVE: {
                    if (L.args.size() < 3) return argErr("reg, argBaseOff, retOff");
                    if (!parseReg(L.args[0], r)) { error = "非法寄存器: " + L.args[0]; return false; }
                    parseU64(L.args[1], a); parseU64(L.args[2], b);
                    emitU8(r); emitU32(static_cast<uint32_t>(a)); emitU32(static_cast<uint32_t>(b));
                    break;
                }
                case OP_JIT_SUBMIT: {
                    if (L.args.size() < 2) return argErr("func, outOff");
                    if (parseU64(L.args[0], v)) emitU16(static_cast<uint16_t>(v));
                    else { pending.push_back({static_cast<uint32_t>(img.code.size()), fidx, L.args[0], true}); emitU16(0); }
                    parseU64(L.args[1], a); emitU32(static_cast<uint32_t>(a));
                    break;
                }
                case OP_CALL_IND: {
                    if (L.args.size() < 3) return argErr("reg, argBaseOff, retOff");
                    if (!parseReg(L.args[0], r)) { error = "非法寄存器: " + L.args[0]; return false; }
                    parseU64(L.args[1], a); parseU64(L.args[2], b);
                    emitU8(r); emitU32(static_cast<uint32_t>(a)); emitU32(static_cast<uint32_t>(b));
                    break;
                }
                case OP_PROC_WAIT: {
                    if (L.args.size() < 2) return argErr("off, off");
                    parseU64(L.args[0], a); parseU64(L.args[1], b);
                    emitU32(static_cast<uint32_t>(a)); emitU32(static_cast<uint32_t>(b));
                    break;
                }
                case OP_STACK_INIT: {
                    if (L.args.size() < 2) return argErr("size, scopeSize");
                    parseU64(L.args[0], a); parseU64(L.args[1], b);
                    emitU32(static_cast<uint32_t>(a)); emitU32(static_cast<uint32_t>(b));
                    break;
                }
                case OP_STACK_ALLOC:
                    if (L.args.size() < 1) return argErr("n");
                    parseU64(L.args[0], v); emitU64(v);
                    break;
                case OP_STACK_MOVE: case OP_DEL_HEAP: case OP_FREE_ARRAY:
                case OP_THREAD_JOIN: case OP_THREAD_SELF: case OP_PROC_SELF:
                    if (L.args.size() < 1) return argErr("off");
                    parseU64(L.args[0], v); emitU32(static_cast<uint32_t>(v));
                    break;
                case OP_LEA:
                    if (L.args.size() < 2) return argErr("mode, off");
                    parseU64(L.args[0], a); parseU64(L.args[1], b);
                    emitU8(static_cast<uint8_t>(a)); emitU32(static_cast<uint32_t>(b));
                    break;
                case OP_PROC_SPAWN: {
                    if (L.args.size() < 2) return argErr("func, argOff");
                    if (parseU64(L.args[0], v)) emitU16(static_cast<uint16_t>(v));
                    else { pending.push_back({static_cast<uint32_t>(img.code.size()), fidx, L.args[0], true}); emitU16(0); }
                    parseU64(L.args[1], a); emitU32(static_cast<uint32_t>(a));
                    break;
                }
                case OP_LOAD: case OP_STORE:
                case OP_ATOMIC_LOAD: case OP_ATOMIC_STORE: case OP_ATOMIC_XCHG:
                case OP_ATOMIC_CAS: case OP_ATOMIC_ADD: {
                    if (L.args.size() < 4) return argErr("td, reg, mode, off");
                    if (!tdFromName(L.args[0], td)) { error = "非法类型: " + L.args[0]; return false; }
                    if (!parseReg(L.args[1], r)) { error = "非法寄存器: " + L.args[1]; return false; }
                    parseU64(L.args[2], a); parseU64(L.args[3], b);
                    emitU8(td); emitU8(r); emitU8(static_cast<uint8_t>(a)); emitU32(static_cast<uint32_t>(b));
                    break;
                }
                case OP_MOVI: {
                    if (L.args.size() < 3) return argErr("td, reg, imm");
                    if (!tdFromName(L.args[0], td)) { error = "非法类型: " + L.args[0]; return false; }
                    if (!parseReg(L.args[1], r)) { error = "非法寄存器: " + L.args[1]; return false; }
                    parseU64(L.args[2], v);
                    emitU8(td); emitU8(r);
                    for (uint8_t i = 0; i < tdBytes(td); ++i) emitU8(static_cast<uint8_t>(v >> (8 * i)));
                    break;
                }
                case OP_PUSH_IMM: {
                    if (L.args.size() < 2) return argErr("td, imm");
                    if (!tdFromName(L.args[0], td)) { error = "非法类型: " + L.args[0]; return false; }
                    parseU64(L.args[1], v);
                    emitU8(td);
                    for (uint8_t i = 0; i < tdBytes(td); ++i) emitU8(static_cast<uint8_t>(v >> (8 * i)));
                    break;
                }
                case OP_MOV: {
                    if (L.args.size() < 2) return argErr("dst, src");
                    uint8_t d0 = 0, s0 = 0;
                    if (!parseReg(L.args[0], d0) || !parseReg(L.args[1], s0)) { error = "非法寄存器"; return false; }
                    emitU8(d0); emitU8(s0);
                    break;
                }
                case OP_PUSH_REG: case OP_POP_REG: {
                    if (L.args.size() < 2) return argErr("td, reg");
                    if (!tdFromName(L.args[0], td)) { error = "非法类型: " + L.args[0]; return false; }
                    if (!parseReg(L.args[1], r)) { error = "非法寄存器: " + L.args[1]; return false; }
                    emitU8(td); emitU8(r);
                    break;
                }
                case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_REM: case OP_NEG:
                case OP_SHL: case OP_SHR: case OP_AND: case OP_OR: case OP_XOR: case OP_NOT:
                case OP_SQRT: case OP_LOG: case OP_COUT: {
                    if (L.args.size() < 1) return argErr("td");
                    if (!tdFromName(L.args[0], td)) { error = "非法类型: " + L.args[0]; return false; }
                    emitU8(td);
                    break;
                }
                case OP_CMP: {
                    if (L.args.size() < 2) return argErr("sub, td");
                    if (!cmpFromName(L.args[0], sub)) { error = "非法比较: " + L.args[0]; return false; }
                    if (!tdFromName(L.args[1], td)) { error = "非法类型: " + L.args[1]; return false; }
                    emitU8(sub); emitU8(td);
                    break;
                }
                case OP_CVT: {
                    if (L.args.size() < 2) return argErr("srcTd, dstTd");
                    uint8_t s0 = 0, d0 = 0;
                    if (!tdFromName(L.args[0], s0) || !tdFromName(L.args[1], d0)) { error = "非法类型"; return false; }
                    emitU8(s0); emitU8(d0);
                    break;
                }
                case OP_EXTERN_CALL: {
                    if (L.args.size() < 3) return argErr("idx, argBase, retOff");
                    parseU64(L.args[0], a); parseU64(L.args[1], b); parseU64(L.args[2], c);
                    emitU8(static_cast<uint8_t>(a)); emitU32(static_cast<uint32_t>(b)); emitU32(static_cast<uint32_t>(c));
                    break;
                }
                case OP_DL_REG: case OP_DL_CALL: {
                    if (L.args.size() < 3) return argErr("off, off, off");
                    parseU64(L.args[0], a); parseU64(L.args[1], b); parseU64(L.args[2], c);
                    emitU32(static_cast<uint32_t>(a)); emitU32(static_cast<uint32_t>(b)); emitU32(static_cast<uint32_t>(c));
                    break;
                }
                case OP_NEW_HEAP:
                    if (L.args.size() < 2) return argErr("off, size");
                    parseU64(L.args[0], a); parseU64(L.args[1], b);
                    emitU32(static_cast<uint32_t>(a)); emitU64(b);
                    break;
                case OP_NEW_ARRAY: case OP_PROC_SHARE:
                    if (L.args.size() < 2) return argErr("size, off");
                    parseU64(L.args[0], a); parseU64(L.args[1], b);
                    emitU64(a); emitU32(static_cast<uint32_t>(b));
                    break;
                case OP_MEMCPY:
                    if (L.args.size() < 5) return argErr("dstMode, dstOff, srcMode, srcOff, size");
                    parseU64(L.args[0], a); parseU64(L.args[1], b); parseU64(L.args[2], c);
                    parseU64(L.args[3], d2); parseU64(L.args[4], e2);
                    emitU8(static_cast<uint8_t>(a)); emitU64(b);
                    emitU8(static_cast<uint8_t>(c)); emitU64(d2); emitU64(e2);
                    break;
                case OP_FUNC_ADDR:
                    if (L.args.size() < 1) return argErr("func");
                    if (parseU64(L.args[0], v)) emitU16(static_cast<uint16_t>(v));
                    else { pending.push_back({static_cast<uint32_t>(img.code.size()), fidx, L.args[0], true}); emitU16(0); }
                    break;
                default:
                    error = std::string("未实现的指令编码: ") + L.mnemonic;
                    return false;
            }
            if (verbose) {
                std::fprintf(stderr, "  [%04u] %s (%u 字节)\n", before, L.raw.c_str(),
                             static_cast<unsigned>(img.code.size() - before));
            }
        }

        if (defs.empty()) startFunc("main", argsLegacy, retsLegacy);
        if (defs.size() == 1 && defs[0].name == "main") {
            defs[0].argSize = argsLegacy;
            defs[0].retSize = retsLegacy;
        }

        // ---- 第二趟：回填标签 / 函数名 ----
        for (const auto& p : pending) {
            if (p.isFunc) {
                auto it = funcIndex.find(p.name);
                if (it == funcIndex.end()) { error = "未知函数: " + p.name; return false; }
                const uint16_t idx = static_cast<uint16_t>(it->second);
                img.code[p.at] = uint8_t(idx);
                img.code[p.at + 1] = uint8_t(idx >> 8);
            } else {
                if (p.funcIdx < 0) continue;
                auto& lm = labels[static_cast<size_t>(p.funcIdx)];
                auto it = lm.find(p.name);
                if (it == lm.end()) { error = "未知标签: " + p.name; return false; }
                const uint32_t off = it->second;
                img.code[p.at] = uint8_t(off); img.code[p.at + 1] = uint8_t(off >> 8);
                img.code[p.at + 2] = uint8_t(off >> 16); img.code[p.at + 3] = uint8_t(off >> 24);
            }
        }

        // ---- 函数目录 ----
        for (size_t i = 0; i < defs.size(); ++i) {
            ModuleFunc f;
            f.offset = defs[i].start;
            f.size = (i + 1 < defs.size() ? defs[i + 1].start : static_cast<uint32_t>(img.code.size())) - defs[i].start;
            f.argSize = defs[i].argSize;
            f.retSize = defs[i].retSize;
            f.entry = f.offset;
            img.funcs.push_back(f);
        }
        funcs = img.funcs;

        // ---- 入口函数 ----
        uint32_t ef = 0;
        if (!entryName.empty()) {
            auto it = funcIndex.find(entryName);
            if (it != funcIndex.end()) ef = static_cast<uint32_t>(it->second);
            else {
                auto l0 = labels[0].find(entryName);
                if (l0 == labels[0].end()) { error = "未知入口: " + entryName; return false; }
            }
        } else if (haveEntryNum) {
            if (entryNum < img.funcs.size()) ef = entryNum;
            else {
                for (size_t i = 0; i < img.funcs.size(); ++i)
                    if (entryNum >= img.funcs[i].offset && entryNum < img.funcs[i].offset + img.funcs[i].size) { ef = static_cast<uint32_t>(i); break; }
            }
        }
        if (img.funcs.empty()) { error = "空模块"; return false; }
        if (ef >= img.funcs.size()) ef = 0;
        entryFunc_ = ef;
        img.entryFunc = ef;
        entryArgSize_ = img.funcs[ef].argSize;
        entryRetSize_ = img.funcs[ef].retSize;
        return true;
    }

    // ---------------- 反汇编 ----------------
    // 不变式：`-d` 的产物必须能被本汇编器原样再汇编回**逐字节相同**的模块。
    // 但码流里可能夹着数据（j8c 把字符串常量以 .BYTE 直接写进函数体），线性扫描
    // 会把数据当指令解码；因此凡是「解出来后无法精确还原」的行（类型字节不是已知
    // TD、CMP 子操作数越界、跳转目标标签不存在、指令被函数尾截断），一律退化成按
    // 字节输出的 .BYTE，保证字节流不变。入口函数下标也要显式写回，否则重新汇编
    // 出的模块会丢掉 entryFunc。
    bool disassemble(const std::vector<uint8_t>& bytes, std::ostream& os, bool hasHeader) {
        ModuleImage img;
        if (hasHeader && bytes.size() >= 20 && bytes[0] == 'J' && bytes[1] == '3') {
            if (!img.deserialize(bytes.data(), bytes.size())) { error = "模块头解析失败"; return false; }
        } else {
            img.code = bytes;
            ModuleFunc f;
            f.size = static_cast<uint32_t>(bytes.size());
            img.funcs.push_back(f);
        }
        if (!img.funcs.empty()) {
            const uint32_t ef = img.entryFunc < img.funcs.size()
                                    ? img.entryFunc : static_cast<uint32_t>(img.funcs.size() - 1);
            os << ".ENTRY fn" << ef << "\n";
        }
        for (size_t fi = 0; fi < img.funcs.size(); ++fi) {
            const ModuleFunc& f = img.funcs[fi];
            os << "; .FUNC fn" << fi << " argSize=" << f.argSize << " retSize=" << f.retSize
               << " offset=" << f.offset << " size=" << f.size << " entry=" << f.entry << "\n";
            os << ".FUNC fn" << fi << " " << f.argSize << " " << f.retSize << "\n";

            // 第一遍：贪婪线性扫描，把每行渲染到缓冲区（先不输出，跳转目标要等标签集齐）
            struct Line { uint32_t at = 0, len = 0, target = 0; std::string text; bool exact = true, jump = false; };
            std::vector<Line> lines;
            uint32_t off = f.offset;
            const uint32_t end = f.offset + f.size;
            while (off < end) {
                std::ostringstream tmp;
                Line L;
                L.at = off;
                uint32_t next = disasmOne(img.code.data(), off, end, tmp, L.exact, L.jump, L.target);
                if (next <= off || next > end) next = off + 1;
                L.len = next - off;
                L.text = tmp.str();
                lines.push_back(std::move(L));
                off = next;
            }

            // 第二遍：行首都会打印标签，故行首偏移即合法跳转目标集合
            std::set<uint32_t> labels;
            for (const auto& L : lines) labels.insert(L.at - f.offset);
            for (auto& L : lines)
                if (L.jump && labels.find(L.target) == labels.end()) L.exact = false;

            // 第三遍：输出。不可精确还原的行按字节铺开成 .BYTE
            for (const auto& L : lines) {
                os << "L" << (L.at - f.offset) << ":\t";
                if (L.exact) os << L.text;
                else
                    for (uint32_t k = 0; k < L.len; ++k)
                        os << (k ? "\n\t" : "") << ".BYTE " << unsigned(img.code[L.at + k]);
                os << "\n";
            }
        }
        return true;
    }

private:
    uint32_t entryFunc_ = 0;
    uint32_t entryArgSize_ = 0;
    uint32_t entryRetSize_ = 0;

    static uint32_t rdU32(const uint8_t* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }
    static uint64_t rdU64(const uint8_t* p) { return uint64_t(rdU32(p)) | (uint64_t(rdU32(p + 4)) << 32); }

    // 类型字节能否「渲染成名字再解析回来」得到同一个字节（否则反汇编必须退化成 .BYTE）
    static bool tdRoundTrips(uint8_t td) {
        uint8_t back = 0;
        return tdFromName(std::string(tdName(td)), back) && back == td;
    }

    // 渲染一条指令：返回下一条指令的偏移；exact=false 表示这一行无法精确还原
    // （调用方会把它改写成 .BYTE 序列）；jump=true 时 target 是函数相对的标签偏移。
    static uint32_t disasmOne(const uint8_t* c, uint32_t at, uint32_t end, std::ostream& os,
                              bool& exact, bool& jump, uint32_t& target) {
        const uint8_t op = c[at];
        const std::string_view n = opName(op);
        if (n.empty() || n[0] == '?') { os << ".BYTE " << unsigned(c[at]); return at + 1; }
        os << n;
        const uint8_t* p = c + at + 1;
        auto guard = [&](uint32_t need) { return at + 1 + need <= end; };
        auto guarded = [&](uint32_t need) { if (!guard(need)) { exact = false; return false; } return true; };
        auto typed = [&](uint8_t td) { if (!tdRoundTrips(td)) exact = false; };
        auto cmpName = [](uint8_t s) {
            return s == CMP_LT ? "LT" : s == CMP_LE ? "LE" : s == CMP_EQ ? "EQ"
                 : s == CMP_NE ? "NE" : s == CMP_GT ? "GT" : "GE";
        };
        switch (op) {
            case OP_JMP: case OP_BRANCH:
                if (!guarded(4)) return at + 1;
                jump = true; target = rdU32(p);
                os << " L" << target; return at + 5;
            case OP_CALL:
                if (!guarded(10)) return at + 1;
                os << " " << (rdU32(p) & 0xFFFF) << ", " << rdU32(p + 2) << ", " << rdU32(p + 6);
                return at + 11;
            case OP_JIT_SUBMIT:
                if (!guarded(6)) return at + 1;
                os << " " << (rdU32(p) & 0xFFFF) << ", " << rdU32(p + 2); return at + 7;
            case OP_JIT_COMPILE:
                if (!guarded(12)) return at + 1;
                os << " " << rdU32(p) << ", " << rdU32(p + 4) << ", " << rdU32(p + 8); return at + 13;
            case OP_CALL_NATIVE:
                if (!guarded(9)) return at + 1;
                os << " R" << unsigned(p[0]) << ", " << rdU32(p + 1) << ", " << rdU32(p + 5); return at + 10;
            case OP_CALL_IND:
                if (!guarded(9)) return at + 1;
                os << " R" << unsigned(p[0]) << ", " << rdU32(p + 1) << ", " << rdU32(p + 5); return at + 10;
            case OP_PROC_WAIT:
                if (!guarded(8)) return at + 1;
                os << " " << rdU32(p) << ", " << rdU32(p + 4); return at + 9;
            case OP_STACK_INIT:
                if (!guarded(8)) return at + 1;
                os << " " << rdU32(p) << " " << rdU32(p + 4); return at + 9;
            case OP_STACK_ALLOC:
                if (!guarded(8)) return at + 1;
                os << " " << rdU64(p); return at + 9;
            case OP_STACK_MOVE: case OP_DEL_HEAP: case OP_FREE_ARRAY:
            case OP_THREAD_JOIN: case OP_THREAD_SELF: case OP_PROC_SELF:
                if (!guarded(4)) return at + 1;
                os << " " << rdU32(p); return at + 5;
            case OP_LEA:
                if (!guarded(5)) return at + 1;
                os << " " << unsigned(p[0]) << ", " << rdU32(p + 1); return at + 6;
            case OP_PROC_SPAWN:
                if (!guarded(6)) return at + 1;
                os << " " << (rdU32(p) & 0xFFFF) << ", " << rdU32(p + 2); return at + 7;
            case OP_LOAD: case OP_STORE:
            case OP_ATOMIC_LOAD: case OP_ATOMIC_STORE: case OP_ATOMIC_XCHG:
            case OP_ATOMIC_CAS: case OP_ATOMIC_ADD:
                if (!guarded(7)) return at + 1;
                typed(p[0]);
                os << " " << tdName(p[0]) << " R" << unsigned(p[1]) << ", " << unsigned(p[2]) << ", " << rdU32(p + 3);
                return at + 8;
            case OP_MOVI: {
                if (!guarded(2)) return at + 1;
                const uint8_t td = p[0], r = p[1], w = tdBytes(td);
                if (!guarded(2u + w)) return at + 1;
                typed(td);
                os << " " << tdName(td) << " R" << unsigned(r) << ", " << asmLoadRaw(p + 2, w);
                return at + 3 + w;
            }
            case OP_PUSH_IMM: {
                if (!guarded(1)) return at + 1;
                const uint8_t td = p[0], w = tdBytes(td);
                if (!guarded(1u + w)) return at + 1;
                typed(td);
                os << " " << tdName(td) << " " << asmLoadRaw(p + 1, w);
                return at + 2 + w;
            }
            case OP_MOV:
                if (!guarded(2)) return at + 1;
                os << " R" << unsigned(p[0]) << ", R" << unsigned(p[1]); return at + 3;
            case OP_PUSH_REG: case OP_POP_REG:
                if (!guarded(2)) return at + 1;
                typed(p[0]);
                os << " " << tdName(p[0]) << " R" << unsigned(p[1]); return at + 3;
            case OP_CMP:
                if (!guarded(2)) return at + 1;
                if (p[0] > CMP_GE) exact = false;   // cmpName 会把越界值一律写成 GE
                typed(p[1]);
                os << " " << cmpName(p[0]) << " " << tdName(p[1]); return at + 3;
            case OP_CVT:
                if (!guarded(2)) return at + 1;
                typed(p[0]); typed(p[1]);
                os << " " << tdName(p[0]) << ", " << tdName(p[1]); return at + 3;
            case OP_EXTERN_CALL:
                if (!guarded(9)) return at + 1;
                os << " " << unsigned(p[0]) << ", " << rdU32(p + 1) << ", " << rdU32(p + 5); return at + 10;
            case OP_DL_REG: case OP_DL_CALL:
                if (!guarded(12)) return at + 1;
                os << " " << rdU32(p) << ", " << rdU32(p + 4) << ", " << rdU32(p + 8); return at + 13;
            case OP_NEW_HEAP:
                if (!guarded(12)) return at + 1;
                os << " " << rdU32(p) << ", " << rdU64(p + 4); return at + 13;
            case OP_NEW_ARRAY: case OP_PROC_SHARE:
                if (!guarded(12)) return at + 1;
                os << " " << rdU64(p) << ", " << rdU32(p + 8); return at + 13;
            case OP_MEMCPY:
                if (!guarded(26)) return at + 1;
                os << " " << unsigned(p[0]) << ", " << rdU64(p + 1) << ", " << unsigned(p[9])
                   << ", " << rdU64(p + 10) << ", " << rdU64(p + 18);
                return at + 27;
            case OP_FUNC_ADDR:
                if (!guarded(2)) return at + 1;
                os << " " << (rdU32(p) & 0xFFFF); return at + 3;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_REM: case OP_NEG:
            case OP_SHL: case OP_SHR: case OP_AND: case OP_OR: case OP_XOR: case OP_NOT:
            case OP_SQRT: case OP_LOG: case OP_COUT:
                if (!guarded(1)) return at + 1;
                typed(p[0]);
                os << " " << tdName(p[0]); return at + 2;
            default:
                return at + 1;
        }
    }
};

} // namespace jadeight
