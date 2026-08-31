// jasm.cpp — Jadeight VM 汇编器（薄命令行入口，核心逻辑在 jadeight_asm.hpp）
// 用法: ./jasm input.asm [-o output.bc] [-d] [-v] [-f] [-e]
//   -d  反汇编模式: 输入 .bc 文件，输出可读指令清单
//   -o  输出文件名 (默认 input.bc)
//   -v  详细输出 (打印汇编行/反汇编结果)
//   -f  输出纯字节码 (无 FunctionSave 头部 [argSize,retSize,entry])
//   -e  头部按【小端序】读写（与 Jadeight2 VM FunctionSave::loadFromFile 兼容；
//       默认大端序，与自身反汇编回环一致）
//
// 汇编器支持全部 177 条 opcode，标签，多种数值格式，
// 伪指令 .STACK .ARGS .RETS .ENTRY .ENTRYOFF .BYTE .FILL
//
// 编译: g++ -std=c++20 -O2 jasm.cpp -o jasm

#include "jadeight_asm.hpp"

using namespace jadeight;

int main(int argc, char* argv[]) {
    std::string inputPath;
    std::string outputPath;
    bool disasm = false;
    bool verbose = false;
    bool pureBinary = false;
    bool leHeader = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-d") disasm = true;
        else if (arg == "-v") verbose = true;
        else if (arg == "-f") pureBinary = true;
        else if (arg == "-e" || arg == "--le") leHeader = true;
        else if (arg == "-o") {
            if (i+1 < argc) outputPath = argv[++i];
            else { std::cerr << "Missing output filename after -o\n"; return 1; }
        } else {
            inputPath = arg;
        }
    }

    if (inputPath.empty()) {
        std::cerr << "Usage: " << argv[0] << " input.asm [-o output.bc] [-d] [-v] [-f] [-e]\n";
        return 1;
    }

    std::ifstream in(inputPath, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open input file: " << inputPath << "\n";
        return 1;
    }

    if (disasm) {
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
        in.close();
        Assembler asmblr;
        asmblr.verbose = verbose;
        asmblr.littleEndianHeader = leHeader;
        bool hasHeader = !pureBinary;
        std::ostream* out = &std::cout;
        std::ofstream ofs;
        if (!outputPath.empty()) {
            ofs.open(outputPath);
            if (!ofs) { std::cerr << "Cannot open output file: " << outputPath << "\n"; return 1; }
            out = &ofs;
        }
        asmblr.disassemble(bytes, *out, hasHeader);
        return 0;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();
    std::string asmText = buffer.str();

    Assembler asmblr;
    asmblr.verbose = verbose;
    asmblr.pureBinary = pureBinary;
    asmblr.littleEndianHeader = leHeader;
    asmblr.inputPath = inputPath;
    if (outputPath.empty()) {
        size_t dot = inputPath.rfind('.');
        if (dot != std::string::npos) outputPath = inputPath.substr(0, dot) + ".bc";
        else outputPath = inputPath + ".bc";
    }
    asmblr.outputPath = outputPath;

    std::vector<uint8_t> outBytes;
    uint32_t argSize, retSize, entry;
    if (!asmblr.assemble(asmText, outBytes, argSize, retSize, entry)) {
        std::cerr << "Assembly failed.\n";
        return 1;
    }

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot write output file: " << outputPath << "\n";
        return 1;
    }
    out.write(reinterpret_cast<const char*>(outBytes.data()), outBytes.size());
    out.close();

    if (verbose) {
        std::cerr << "Assembled successfully.\n";
        std::cerr << "Output: " << outputPath << "\n";
        std::cerr << "Size: " << outBytes.size() << " bytes\n";
        if (!pureBinary) {
            std::cerr << "Header (" << (leHeader ? "LE" : "BE") << "): argSize=" << argSize
                      << " retSize=" << retSize << " entry=" << entry << "\n";
        }
    }

    return 0;
}
