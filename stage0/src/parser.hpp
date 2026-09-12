#pragma once
#include "ast.hpp"
#include "token.hpp"
#include <vector>

namespace lg {

// Parser rekursywnego zstępowania. Zakłada, że komentarze i NewLine
// zostały odfiltrowane przed przekazaniem tokenów (patrz filterTrivia).
class Parser {
public:
    Parser(std::vector<Token> tokens, std::string filename);

    ast::Program parseProgram();

    // Usuwa z listy tokenów: komentarze i NewLine (nieistotne dla gramatyki
    // stage0 — bloki zawsze wyznaczają { }, więc nie potrzebujemy
    // znaczenia białych znaków).
    static std::vector<Token> filterTrivia(const std::vector<Token>& tokens);

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;
    std::string filename_;
    // Rozwiazuje klasyczna niejednoznacznosc "IDENT {" (literal struct vs
    // poczatek bloku) w warunkach if/while/for/match - jak w Rust.
    bool noStructLiteral_ = false;

    const Token& cur() const;
    const Token& peek(size_t off = 1) const;
    const Token& advance();
    bool check(TokenKind k) const;
    bool match(TokenKind k);
    const Token& expect(TokenKind k, const std::string& what);
    [[noreturn]] void error(const std::string& msg) const;

    // Top-level
    ast::ImportDecl parseImport();
    ast::ImportDecl parseFrom();
    ast::StructDecl parseStruct(std::vector<ast::Attribute> attrs);
    ast::EnumDecl parseEnum();
    ast::FnDecl parseFn(std::vector<ast::Attribute> attrs);
    ast::ImplDecl parseImpl();
    std::vector<ast::Attribute> parseAttributes();
    ast::Attribute parseOneAttribute();

    ast::Type parseType();
    std::string parsePath(); // a::b::c lub a.b.c -> "a::b::c" (zunifikowane)

    // Statements
    ast::StmtPtr parseBlock();
    ast::StmtPtr parseStatement();
    ast::StmtPtr parseLet(bool isMut);
    ast::StmtPtr parseReturn();
    ast::StmtPtr parseIf();
    ast::StmtPtr parseWhile();
    ast::StmtPtr parseFor();
    ast::StmtPtr parseMatch();
    ast::StmtPtr parseOn();
    ast::StmtPtr parseProfile();
    ast::StmtPtr parseIsolate();
    ast::StmtPtr parseShellStmt();
    ast::StmtPtr parseTry();
    ast::StmtPtr parseThrow();

    // Expressions (precedencja rosnąco)
    ast::ExprPtr parseExpression();
    ast::ExprPtr parseAssignment();
    ast::ExprPtr parseLogicalOr();
    ast::ExprPtr parseLogicalAnd();
    ast::ExprPtr parseEquality();
    ast::ExprPtr parseRelational();
    ast::ExprPtr parseRange();      // a..=b (tylko w kontekście `match`, ale parsowane ogólnie)
    ast::ExprPtr parseAdditive();
    ast::ExprPtr parseMultiplicative();
    ast::ExprPtr parsePower();
    ast::ExprPtr parseUnary();
    ast::ExprPtr parsePostfix();
    ast::ExprPtr parsePrimary();
    ast::ExprPtr parseStructLiteral(const std::string& name, int line);
    std::vector<ast::Arg> parseArgList();

    ast::ExprPtr makeStringExpr(const std::string& raw, int line);
    std::vector<ast::InterpPart> extractInterpParts(const std::string& raw);
    std::vector<ast::InterpPart> extractInterpParts(const std::string& raw, int baseLine);
};

} // namespace lg
