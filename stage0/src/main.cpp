#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "typecheck.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("nie mozna otworzyc pliku: " + path);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static const char* tokenKindName(lg::TokenKind kind) {
    using K = lg::TokenKind;
    switch (kind) {
        case K::Identifier: return "Identifier";
        case K::IntLiteral: return "IntLiteral";
        case K::FloatLiteral: return "FloatLiteral";
        case K::StringLiteral: return "StringLiteral";
        case K::CharLiteral: return "CharLiteral";
        case K::ShellLiteral: return "ShellLiteral";
        case K::KwLet: return "KwLet";
        case K::KwMut: return "KwMut";
        case K::KwConst: return "KwConst";
        case K::KwFn: return "KwFn";
        case K::KwStruct: return "KwStruct";
        case K::KwImpl: return "KwImpl";
        case K::KwReturn: return "KwReturn";
        case K::KwIf: return "KwIf";
        case K::KwElse: return "KwElse";
        case K::KwFor: return "KwFor";
        case K::KwIn: return "KwIn";
        case K::KwWhile: return "KwWhile";
        case K::KwMatch: return "KwMatch";
        case K::KwImport: return "KwImport";
        case K::KwFrom: return "KwFrom";
        case K::KwAs: return "KwAs";
        case K::KwExtern: return "KwExtern";
        case K::KwSpawn: return "KwSpawn";
        case K::KwIsolate: return "KwIsolate";
        case K::KwOn: return "KwOn";
        case K::KwProfile: return "KwProfile";
        case K::KwRegion: return "KwRegion";
        case K::KwTrue: return "KwTrue";
        case K::KwFalse: return "KwFalse";
        case K::DocComment: return "DocComment";
        case K::LineComment: return "LineComment";
        case K::BlockComment: return "BlockComment";
        case K::LParen: return "LParen";
        case K::RParen: return "RParen";
        case K::LBrace: return "LBrace";
        case K::RBrace: return "RBrace";
        case K::LBracket: return "LBracket";
        case K::RBracket: return "RBracket";
        case K::Colon: return "Colon";
        case K::DoubleColon: return "DoubleColon";
        case K::Comma: return "Comma";
        case K::Dot: return "Dot";
        case K::Arrow: return "Arrow";
        case K::FatArrow: return "FatArrow";
        case K::Plus: return "Plus";
        case K::Minus: return "Minus";
        case K::Star: return "Star";
        case K::StarStar: return "StarStar";
        case K::Slash: return "Slash";
        case K::Percent: return "Percent";
        case K::Eq: return "Eq";
        case K::EqEq: return "EqEq";
        case K::NotEq: return "NotEq";
        case K::Lt: return "Lt";
        case K::LtEq: return "LtEq";
        case K::Gt: return "Gt";
        case K::GtEq: return "GtEq";
        case K::AndAnd: return "AndAnd";
        case K::OrOr: return "OrOr";
        case K::Not: return "Not";
        case K::At: return "At";
        case K::Dollar: return "Dollar";
        case K::DotDotEq: return "DotDotEq";
        case K::NewLine: return "NewLine";
        case K::Eof: return "Eof";
        default: return "Unknown";
    }
}

static void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("nie mozna zapisac pliku: " + path);
    out << content;
}

// ---------------- Kompilacja wieloplikowa (lokalne importy) ----------------
//
// `import std::X` i `import extern "..."` sa zewnetrzne (obsluguje je Codegen
// przez `namespace X = lg::X;` / `#include`). Kazdy INNY `import` (np.
// `import lexer` albo `import ast::nodes`) jest traktowany jako lokalny modul
// projektu: rozwiazywany na plik `<katalog_glownego_pliku>/lexer.lg` (lub
// `ast/nodes.lg`), parsowany rekurencyjnie i SCALANY do jednego Programu -
// stage0 nie generuje osobnych .cpp per modul, tylko jedna plaska jednostke
// translacji. To swiadome uproszczenie (patrz README), ale wystarczajace,
// zeby napisac wieloplikowy `lpm` w samym Legendary Lang.

static bool isLocalImport(const lg::ast::ImportDecl& imp) {
    if (imp.isExtern) return false;
    return imp.path.rfind("std::", 0) != 0;
}

static fs::path resolveLocalImportPath(const fs::path& fromDir, const std::string& importPath) {
    std::string rel = importPath;
    size_t pos;
    while ((pos = rel.find("::")) != std::string::npos) rel.replace(pos, 2, "/");
    return fromDir / (rel + ".lg");
}

static void mergeDecls(lg::ast::Program& target, lg::ast::Program&& src) {
    for (auto& s : src.structs) target.structs.push_back(std::move(s));
    for (auto& e : src.enums) target.enums.push_back(std::move(e));
    for (auto& i : src.impls) target.impls.push_back(std::move(i));
    for (auto& f : src.fns) target.fns.push_back(std::move(f));
}

// Znakuje kazda deklaracje jej plikiem zrodlowym - potrzebne, zeby Codegen
// mogl emitowac poprawne `#line N "plik.lg"` nawet po scaleniu wielu plikow
// w jedna jednostke translacji C++.
static void stampSourceFile(lg::ast::Program& prog, const std::string& file) {
    for (auto& s : prog.structs) s.sourceFile = file;
    for (auto& e : prog.enums) e.sourceFile = file;
    for (auto& i : prog.impls) { i.sourceFile = file; for (auto& m : i.methods) m.sourceFile = file; }
    for (auto& f : prog.fns) f.sourceFile = file;
}

static bool sameImport(const lg::ast::ImportDecl& a, const lg::ast::ImportDecl& b) {
    return a.isExtern == b.isExtern && a.externHeader == b.externHeader &&
           a.path == b.path && a.alias == b.alias && a.isFrom == b.isFrom &&
           a.selective == b.selective;
}

static void collectExternalImport(std::vector<lg::ast::ImportDecl>& externals,
                                   const lg::ast::ImportDecl& imp) {
    for (auto& e : externals) if (sameImport(e, imp)) return; // dedup
    externals.push_back(imp);
}

static lg::ast::Program parseFileRecursive(const fs::path& path, std::set<fs::path>& visited,
                                            std::vector<lg::ast::ImportDecl>& externalImports) {
    fs::path canon = fs::weakly_canonical(path);
    lg::ast::Program result;
    if (visited.count(canon)) return result; // juz scalone (obsluga cykli/duplikatow)
    visited.insert(canon);

    if (!fs::exists(path)) {
        throw std::runtime_error("nie znaleziono pliku modulu: " + path.string());
    }
    std::string source = readFile(path.string());
    lg::Lexer lexer(source, path.string());
    auto tokens = lexer.tokenize();
    lg::Parser parser(tokens, path.string());
    auto localProgram = parser.parseProgram();
    stampSourceFile(localProgram, path.string());

    for (auto& imp : localProgram.imports) {
        if (isLocalImport(imp)) {
            fs::path childPath = resolveLocalImportPath(path.parent_path(), imp.path);
            auto childProgram = parseFileRecursive(childPath, visited, externalImports);
            mergeDecls(result, std::move(childProgram));
        } else {
            collectExternalImport(externalImports, imp);
        }
    }

    mergeDecls(result, std::move(localProgram));
    return result;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uzycie: lgc <plik.lg> [--emit-cpp] [-o <plik.cpp>] [--tokens]\n";
        return 1;
    }

    std::string path;
    std::string outPath;
    bool showTokens = false;
    bool emitCpp = false;
    bool skipTypecheck = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tokens") showTokens = true;
        else if (arg == "--emit-cpp") emitCpp = true;
        else if (arg == "--no-typecheck") skipTypecheck = true;
        else if (arg == "-o" && i + 1 < argc) outPath = argv[++i];
        else path = arg;
    }

    if (path.empty()) {
        std::cerr << "blad: nie podano pliku .lg\n";
        return 1;
    }

    try {
        std::string source = readFile(path);
        lg::Lexer lexer(source, path);
        auto tokens = lexer.tokenize();

        if (showTokens) {
            for (const auto& tok : tokens) {
                std::cout << tok.line << ":" << tok.col << "\t"
                          << tokenKindName(tok.kind);
                if (!tok.text.empty() && tok.kind != lg::TokenKind::NewLine) {
                    std::cout << "\t\"" << tok.text << "\"";
                }
                std::cout << "\n";
            }
        }

        lg::Parser parser(tokens, path);
        auto mainProgram = parser.parseProgram();
        stampSourceFile(mainProgram, path);

        // Kompilacja wieloplikowa: rozwiazujemy lokalne importy glownego pliku
        // (import std::X / extern zbieramy oddzielnie i dedupujemy).
        std::set<fs::path> visited;
        visited.insert(fs::weakly_canonical(fs::path(path)));
        std::vector<lg::ast::ImportDecl> externalImports;
        lg::ast::Program program;
        for (auto& imp : mainProgram.imports) {
            if (isLocalImport(imp)) {
                fs::path childPath = resolveLocalImportPath(fs::path(path).parent_path(), imp.path);
                auto childProgram = parseFileRecursive(childPath, visited, externalImports);
                mergeDecls(program, std::move(childProgram));
            } else {
                collectExternalImport(externalImports, imp);
            }
        }
        mergeDecls(program, std::move(mainProgram));
        program.imports = std::move(externalImports);

        if (!skipTypecheck) {
            lg::TypeChecker checker;
            auto issues = checker.check(program);
            if (!issues.empty()) {
                std::cerr << "[lgc] typecheck: znaleziono " << issues.size() << " problem(ow):\n";
                for (auto& msg : issues) std::cerr << "  " << path << ":" << msg << "\n";
                std::cerr << "[lgc] (mozesz pominac ta faze flaga --no-typecheck, jesli to falszywy alarm)\n";
                return 1;
            }
        }

        lg::Codegen codegen;
        std::string cpp = codegen.generate(program);

        if (emitCpp || !outPath.empty()) {
            if (outPath.empty()) outPath = path + ".cpp";
            writeFile(outPath, cpp);
            std::cerr << "[lgc] wygenerowano: " << outPath << "\n";
        } else {
            std::cout << cpp;
        }

    } catch (const std::exception& e) {
        std::cerr << "blad: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
