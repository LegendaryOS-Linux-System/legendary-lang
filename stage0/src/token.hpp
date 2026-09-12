#pragma once
#include <string>
#include <string_view>
#include <unordered_map>

namespace lg {

enum class TokenKind {
    // Literały
    Identifier, IntLiteral, FloatLiteral, StringLiteral, CharLiteral, ShellLiteral,

    // Słowa kluczowe
    KwLet, KwMut, KwConst, KwFn, KwStruct, KwImpl, KwReturn,
    KwIf, KwElse, KwFor, KwIn, KwWhile, KwMatch,
    KwImport, KwFrom, KwAs, KwExtern,
    KwSpawn, KwIsolate, KwOn, KwProfile, KwRegion,
    KwEnum, KwTry, KwCatch, KwThrow,
    KwTrue, KwFalse,

    // Komentarze (zachowywane w strumieniu tokenów dla `---` -> generator dokumentacji)
    DocComment, LineComment, BlockComment,

    // Symbole / operatory
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Colon, DoubleColon, Comma, Dot, Arrow, FatArrow,
    Plus, Minus, Star, StarStar, Slash, Percent,
    Eq, EqEq, NotEq, Lt, LtEq, Gt, GtEq,
    AndAnd, OrOr, Not,
    At,          // @attribute
    Dollar,      // $ inline-shell $
    DotDotEq,    // ..= (range inclusive w match)

    NewLine, Eof, Unknown
};

struct Token {
    TokenKind kind;
    std::string text;
    int line = 0;
    int col = 0;
};

inline const std::unordered_map<std::string_view, TokenKind>& keywords() {
    static const std::unordered_map<std::string_view, TokenKind> kw = {
        {"let", TokenKind::KwLet}, {"mut", TokenKind::KwMut}, {"const", TokenKind::KwConst},
        {"fn", TokenKind::KwFn}, {"struct", TokenKind::KwStruct}, {"impl", TokenKind::KwImpl},
        {"return", TokenKind::KwReturn}, {"if", TokenKind::KwIf}, {"else", TokenKind::KwElse},
        {"for", TokenKind::KwFor}, {"in", TokenKind::KwIn}, {"while", TokenKind::KwWhile},
        {"match", TokenKind::KwMatch}, {"import", TokenKind::KwImport}, {"from", TokenKind::KwFrom},
        {"as", TokenKind::KwAs}, {"extern", TokenKind::KwExtern}, {"spawn", TokenKind::KwSpawn},
        {"isolate", TokenKind::KwIsolate}, {"on", TokenKind::KwOn}, {"profile", TokenKind::KwProfile},
        {"region", TokenKind::KwRegion}, {"true", TokenKind::KwTrue}, {"false", TokenKind::KwFalse},
        {"enum", TokenKind::KwEnum}, {"try", TokenKind::KwTry}, {"catch", TokenKind::KwCatch},
        {"throw", TokenKind::KwThrow},
    };
    return kw;
}

} // namespace lg
