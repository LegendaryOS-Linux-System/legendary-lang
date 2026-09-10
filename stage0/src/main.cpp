#include "lexer.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uzycie: lgc <plik.lg> [--emit-cpp] [--tokens]\n";
        return 1;
    }

    std::string path;
    bool showTokens = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tokens") showTokens = true;
        else if (arg == "--emit-cpp") { /* TODO: parser + codegen */ }
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

        // TODO(stage0): parser -> AST -> codegen C++20
        // Na tym etapie stage0 dostarcza wyłącznie warstwę leksykalną.
        std::cerr << "[lgc] stage0: leksykalna analiza OK ("
                  << tokens.size() << " tokenow). Parser/codegen: w budowie.\n";

    } catch (const std::exception& e) {
        std::cerr << "blad: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
