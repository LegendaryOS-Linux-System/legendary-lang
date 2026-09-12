#include "parser.hpp"
#include "lexer.hpp"
#include <set>
#include <stdexcept>

namespace lg {

using namespace ast;

std::vector<Token> Parser::filterTrivia(const std::vector<Token>& tokens) {
    std::vector<Token> out;
    out.reserve(tokens.size());
    for (const auto& t : tokens) {
        switch (t.kind) {
            case TokenKind::NewLine:
            case TokenKind::LineComment:
            case TokenKind::DocComment:
            case TokenKind::BlockComment:
                continue; // TODO(stage1): DocComment -> `lpm doc`
            default:
                out.push_back(t);
        }
    }
    return out;
}

Parser::Parser(std::vector<Token> tokens, std::string filename)
    : toks_(filterTrivia(tokens)), filename_(std::move(filename)) {}

const Token& Parser::cur() const { return toks_[pos_]; }

const Token& Parser::peek(size_t off) const {
    size_t i = pos_ + off;
    return i < toks_.size() ? toks_[i] : toks_.back(); // ostatni = Eof
}

const Token& Parser::advance() {
    const Token& t = toks_[pos_];
    if (pos_ + 1 < toks_.size()) pos_++;
    return t;
}

bool Parser::check(TokenKind k) const { return cur().kind == k; }

bool Parser::match(TokenKind k) {
    if (check(k)) { advance(); return true; }
    return false;
}

const Token& Parser::expect(TokenKind k, const std::string& what) {
    if (!check(k)) {
        error("oczekiwano '" + what + "', otrzymano \"" + cur().text + "\"");
    }
    return advance();
}

void Parser::error(const std::string& msg) const {
    throw std::runtime_error(filename_ + ":" + std::to_string(cur().line) + ":" +
                              std::to_string(cur().col) + ": " + msg);
}

// ---------------- Top level ----------------

Program Parser::parseProgram() {
    Program prog;
    while (!check(TokenKind::Eof)) {
        auto attrs = parseAttributes();

        if (check(TokenKind::KwImport)) {
            prog.imports.push_back(parseImport());
        } else if (check(TokenKind::KwFrom)) {
            prog.imports.push_back(parseFrom());
        } else if (check(TokenKind::KwStruct)) {
            prog.structs.push_back(parseStruct(std::move(attrs)));
        } else if (check(TokenKind::KwEnum)) {
            prog.enums.push_back(parseEnum());
        } else if (check(TokenKind::KwFn)) {
            prog.fns.push_back(parseFn(std::move(attrs)));
        } else if (check(TokenKind::KwImpl)) {
            prog.impls.push_back(parseImpl());
        } else {
            error("nieoczekiwana deklaracja na najwyzszym poziomie: \"" + cur().text + "\"");
        }
    }
    return prog;
}

std::string Parser::parsePath() {
    std::string result = expect(TokenKind::Identifier, "identyfikator").text;
    while (check(TokenKind::DoubleColon)) {
        advance();
        result += "::";
        result += expect(TokenKind::Identifier, "identyfikator").text;
    }
    return result;
}

std::vector<Attribute> Parser::parseAttributes() {
    std::vector<Attribute> attrs;
    while (check(TokenKind::At)) attrs.push_back(parseOneAttribute());
    return attrs;
}

Attribute Parser::parseOneAttribute() {
    expect(TokenKind::At, "@");
    Attribute a;
    a.path = parsePath();
    if (match(TokenKind::LParen)) {
        a.hasArgs = true;
        int depth = 1;
        std::string raw;
        while (depth > 0) {
            if (check(TokenKind::Eof)) error("niezamkniety atrybut @" + a.path + "(...)");
            if (check(TokenKind::LParen)) depth++;
            if (check(TokenKind::RParen)) {
                depth--;
                if (depth == 0) { advance(); break; }
            }
            raw += advance().text;
            raw += ' ';
        }
        a.rawArgs = raw;
    }
    return a;
}

ImportDecl Parser::parseImport() {
    expect(TokenKind::KwImport, "import");
    ImportDecl d;
    if (match(TokenKind::KwExtern)) {
        d.isExtern = true;
        d.externHeader = expect(TokenKind::StringLiteral, "\"header.h\"").text;
        expect(TokenKind::KwAs, "as");
        d.alias = expect(TokenKind::Identifier, "identyfikator").text;
        return d;
    }
    d.path = parsePath();
    if (match(TokenKind::KwAs)) {
        d.alias = expect(TokenKind::Identifier, "identyfikator").text;
    } else {
        auto pos = d.path.rfind("::");
        d.alias = (pos == std::string::npos) ? d.path : d.path.substr(pos + 2);
    }
    return d;
}

ImportDecl Parser::parseFrom() {
    expect(TokenKind::KwFrom, "from");
    ImportDecl d;
    d.isFrom = true;
    d.path = parsePath();
    expect(TokenKind::KwImport, "import");
    expect(TokenKind::LBrace, "{");
    while (!check(TokenKind::RBrace)) {
        d.selective.push_back(expect(TokenKind::Identifier, "identyfikator").text);
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "}");
    return d;
}

Type Parser::parseType() {
    Type t;
    t.name = expect(TokenKind::Identifier, "typ").text;
    while (check(TokenKind::Dot)) {
        advance();
        t.name += "::";
        t.name += expect(TokenKind::Identifier, "identyfikator").text;
    }
    if (match(TokenKind::Lt)) {
        do {
            t.generics.push_back(parseType());
        } while (match(TokenKind::Comma));
        expect(TokenKind::Gt, ">");
    }
    return t;
}

StructDecl Parser::parseStruct(std::vector<Attribute> attrs) {
    int startLine = cur().line;
    expect(TokenKind::KwStruct, "struct");
    StructDecl s;
    s.line = startLine;
    s.attrs = std::move(attrs);
    s.name = expect(TokenKind::Identifier, "identyfikator").text;
    if (match(TokenKind::Lt)) {
        do { s.typeParams.push_back(expect(TokenKind::Identifier, "parametr generyczny").text); }
        while (match(TokenKind::Comma));
        expect(TokenKind::Gt, ">");
    }
    expect(TokenKind::LBrace, "{");
    while (!check(TokenKind::RBrace)) {
        Field f;
        f.name = expect(TokenKind::Identifier, "pole").text;
        expect(TokenKind::Colon, ":");
        f.type = parseType();
        s.fields.push_back(std::move(f));
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "}");
    return s;
}

EnumDecl Parser::parseEnum() {
    int startLine = cur().line;
    expect(TokenKind::KwEnum, "enum");
    EnumDecl e;
    e.line = startLine;
    e.name = expect(TokenKind::Identifier, "identyfikator").text;
    expect(TokenKind::LBrace, "{");
    while (!check(TokenKind::RBrace)) {
        EnumVariant v;
        v.name = expect(TokenKind::Identifier, "wariant enuma").text;
        if (match(TokenKind::LParen)) {
            if (!check(TokenKind::RParen)) {
                do { v.payloadTypes.push_back(parseType()); } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, ")");
        }
        e.variants.push_back(std::move(v));
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "}");
    return e;
}

FnDecl Parser::parseFn(std::vector<Attribute> attrs) {
    int startLine = cur().line;
    expect(TokenKind::KwFn, "fn");
    FnDecl fn;
    fn.line = startLine;
    fn.attrs = std::move(attrs);
    fn.name = expect(TokenKind::Identifier, "identyfikator").text;
    if (match(TokenKind::Lt)) {
        do { fn.typeParams.push_back(expect(TokenKind::Identifier, "parametr generyczny").text); }
        while (match(TokenKind::Comma));
        expect(TokenKind::Gt, ">");
    }
    expect(TokenKind::LParen, "(");
    while (!check(TokenKind::RParen)) {
        Param p;
        if (check(TokenKind::Identifier) && cur().text == "self" && peek().kind != TokenKind::Colon) {
            p.name = "self";
            p.type = Type{"Self", {}};
            advance();
        } else {
            p.isMut = match(TokenKind::KwMut);
            p.name = expect(TokenKind::Identifier, "parametr").text;
            expect(TokenKind::Colon, ":");
            p.type = parseType();
        }
        fn.params.push_back(std::move(p));
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RParen, ")");
    if (match(TokenKind::Arrow)) fn.returnType = parseType();
    fn.body = parseBlock();
    return fn;
}

ImplDecl Parser::parseImpl() {
    int startLine = cur().line;
    expect(TokenKind::KwImpl, "impl");
    ImplDecl d;
    d.line = startLine;
    Type first = parseType();
    if (match(TokenKind::KwFor)) {
        d.traitType = first;
        d.targetType = parseType();
    } else {
        d.targetType = first;
    }
    expect(TokenKind::LBrace, "{");
    while (!check(TokenKind::RBrace)) {
        auto attrs = parseAttributes();
        d.methods.push_back(parseFn(std::move(attrs)));
    }
    expect(TokenKind::RBrace, "}");
    return d;
}

// ---------------- Statements ----------------

StmtPtr Parser::parseBlock() {
    expect(TokenKind::LBrace, "{");
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    while (!check(TokenKind::RBrace)) {
        block->stmts.push_back(parseStatement());
    }
    expect(TokenKind::RBrace, "}");
    return block;
}

StmtPtr Parser::parseStatement() {
    int startLine = cur().line;
    StmtPtr s;
    if (check(TokenKind::KwLet)) { advance(); s = parseLet(match(TokenKind::KwMut)); }
    else if (check(TokenKind::KwConst)) { advance(); s = parseLet(false); }
    else if (check(TokenKind::KwReturn)) s = parseReturn();
    else if (check(TokenKind::KwIf)) s = parseIf();
    else if (check(TokenKind::KwWhile)) s = parseWhile();
    else if (check(TokenKind::KwFor)) s = parseFor();
    else if (check(TokenKind::KwMatch)) s = parseMatch();
    else if (check(TokenKind::KwOn)) s = parseOn();
    else if (check(TokenKind::KwProfile)) s = parseProfile();
    else if (check(TokenKind::KwIsolate)) s = parseIsolate();
    else if (check(TokenKind::ShellLiteral)) s = parseShellStmt();
    else if (check(TokenKind::KwTry)) s = parseTry();
    else if (check(TokenKind::KwThrow)) s = parseThrow();
    else if (check(TokenKind::LBrace)) s = parseBlock();
    else {
        s = std::make_unique<Stmt>();
        s->kind = StmtKind::ExprStmt;
        s->expr = parseExpression();
    }
    s->line = startLine; // jeden punkt prawdy dla numeru linii kazdej instrukcji (patrz #line w Codegen)
    return s;
}

StmtPtr Parser::parseLet(bool isMut) {
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Let;
    s->isMut = isMut;
    s->varName = expect(TokenKind::Identifier, "nazwa zmiennej").text;
    if (match(TokenKind::Colon)) s->declType = parseType();
    expect(TokenKind::Eq, "=");
    s->initExpr = parseExpression();
    return s;
}

StmtPtr Parser::parseReturn() {
    expect(TokenKind::KwReturn, "return");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Return;
    if (!check(TokenKind::RBrace)) s->expr = parseExpression();
    return s;
}

StmtPtr Parser::parseIf() {
    expect(TokenKind::KwIf, "if");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::If;
    IfBranch b;
    bool old = noStructLiteral_; noStructLiteral_ = true;
    b.cond = parseExpression();
    noStructLiteral_ = old;
    b.body = parseBlock();
    s->branches.push_back(std::move(b));
    while (match(TokenKind::KwElse)) {
        if (match(TokenKind::KwIf)) {
            IfBranch eb;
            bool old2 = noStructLiteral_; noStructLiteral_ = true;
            eb.cond = parseExpression();
            noStructLiteral_ = old2;
            eb.body = parseBlock();
            s->branches.push_back(std::move(eb));
        } else {
            IfBranch eb;
            eb.cond = nullptr;
            eb.body = parseBlock();
            s->branches.push_back(std::move(eb));
            break;
        }
    }
    return s;
}

StmtPtr Parser::parseWhile() {
    expect(TokenKind::KwWhile, "while");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::While;
    bool old = noStructLiteral_; noStructLiteral_ = true;
    s->whileCond = parseExpression();
    noStructLiteral_ = old;
    s->whileBody = parseBlock();
    return s;
}

StmtPtr Parser::parseFor() {
    expect(TokenKind::KwFor, "for");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::For;
    s->forVar = expect(TokenKind::Identifier, "zmienna petli").text;
    expect(TokenKind::KwIn, "in");
    bool old = noStructLiteral_; noStructLiteral_ = true;
    s->forIter = parseExpression();
    noStructLiteral_ = old;
    s->forBody = parseBlock();
    return s;
}

StmtPtr Parser::parseMatch() {
    expect(TokenKind::KwMatch, "match");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Match;
    bool old = noStructLiteral_; noStructLiteral_ = true;
    s->matchSubject = parseExpression();
    noStructLiteral_ = old;
    expect(TokenKind::LBrace, "{");
    while (!check(TokenKind::RBrace)) {
        MatchArm arm;
        if (check(TokenKind::Identifier) && cur().text == "_") {
            arm.isWildcard = true;
            advance();
        } else {
            arm.pattern = parseAdditive();
            if (match(TokenKind::DotDotEq)) {
                arm.isRange = true;
                arm.rangeEnd = parseAdditive();
            }
        }
        expect(TokenKind::FatArrow, "=>");
        if (check(TokenKind::LBrace)) {
            arm.body = parseBlock();
        } else {
            auto block = std::make_unique<Stmt>();
            block->kind = StmtKind::Block;
            auto es = std::make_unique<Stmt>();
            es->kind = StmtKind::ExprStmt;
            es->expr = parseExpression();
            block->stmts.push_back(std::move(es));
            arm.body = std::move(block);
        }
        s->matchArms.push_back(std::move(arm));
        if (!match(TokenKind::Comma)) {
            if (!check(TokenKind::RBrace)) continue; // brak przecinka przed kolejnym ramieniem tez ok
            break;
        }
    }
    expect(TokenKind::RBrace, "}");
    return s;
}

StmtPtr Parser::parseOn() {
    expect(TokenKind::KwOn, "on");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::On;
    bool old = noStructLiteral_; noStructLiteral_ = true;
    s->onCond = parseExpression();
    noStructLiteral_ = old;
    s->onBody = parseBlock();
    return s;
}

StmtPtr Parser::parseProfile() {
    expect(TokenKind::KwProfile, "profile");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Profile;
    s->profileLabel = expect(TokenKind::StringLiteral, "etykieta profilu").text;
    expect(TokenKind::LBrace, "{");
    while (!check(TokenKind::RBrace)) {
        expect(TokenKind::KwRegion, "region");
        RegionStmt r;
        r.name = expect(TokenKind::Identifier, "nazwa regionu").text;
        r.body = parseBlock();
        s->regions.push_back(std::move(r));
    }
    expect(TokenKind::RBrace, "}");
    return s;
}

StmtPtr Parser::parseIsolate() {
    expect(TokenKind::KwIsolate, "isolate");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Isolate;
    expect(TokenKind::LParen, "(");
    s->isolateArgs = parseArgList();
    s->isolateBody = parseBlock();
    return s;
}

StmtPtr Parser::parseShellStmt() {
    Token tok = expect(TokenKind::ShellLiteral, "$ ... $");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::ShellExec;
    s->shellParts = extractInterpParts(tok.text, tok.line);
    return s;
}

StmtPtr Parser::parseTry() {
    expect(TokenKind::KwTry, "try");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Try;
    s->tryBody = parseBlock();
    expect(TokenKind::KwCatch, "catch");
    expect(TokenKind::LParen, "(");
    s->catchVarName = expect(TokenKind::Identifier, "nazwa zmiennej bledu").text;
    if (match(TokenKind::Colon)) s->catchType = parseType();
    expect(TokenKind::RParen, ")");
    s->catchBody = parseBlock();
    return s;
}

StmtPtr Parser::parseThrow() {
    expect(TokenKind::KwThrow, "throw");
    auto s = std::make_unique<Stmt>();
    s->kind = StmtKind::Throw;
    s->expr = parseExpression();
    return s;
}

// ---------------- Expressions ----------------

ExprPtr Parser::parseExpression() { return parseAssignment(); }

ExprPtr Parser::parseAssignment() {
    ExprPtr lhs = parseLogicalOr();
    if (check(TokenKind::Eq)) {
        advance();
        ExprPtr rhs = parseAssignment();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Assign;
        e->op = "=";
        e->lhs = std::move(lhs);
        e->rhs = std::move(rhs);
        return e;
    }
    return lhs;
}

ExprPtr Parser::parseLogicalOr() {
    ExprPtr lhs = parseLogicalAnd();
    while (match(TokenKind::OrOr)) {
        ExprPtr rhs = parseLogicalAnd();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Binary; e->op = "||";
        e->lhs = std::move(lhs); e->rhs = std::move(rhs);
        lhs = std::move(e);
    }
    return lhs;
}

ExprPtr Parser::parseLogicalAnd() {
    ExprPtr lhs = parseEquality();
    while (match(TokenKind::AndAnd)) {
        ExprPtr rhs = parseEquality();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Binary; e->op = "&&";
        e->lhs = std::move(lhs); e->rhs = std::move(rhs);
        lhs = std::move(e);
    }
    return lhs;
}

ExprPtr Parser::parseEquality() {
    ExprPtr lhs = parseRelational();
    while (check(TokenKind::EqEq) || check(TokenKind::NotEq)) {
        std::string op = check(TokenKind::EqEq) ? "==" : "!=";
        advance();
        ExprPtr rhs = parseRelational();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Binary; e->op = op;
        e->lhs = std::move(lhs); e->rhs = std::move(rhs);
        lhs = std::move(e);
    }
    return lhs;
}

ExprPtr Parser::parseRelational() {
    ExprPtr lhs = parseAdditive();
    while (check(TokenKind::Lt) || check(TokenKind::LtEq) ||
           check(TokenKind::Gt) || check(TokenKind::GtEq)) {
        std::string op = cur().text;
        advance();
        ExprPtr rhs = parseAdditive();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Binary; e->op = op;
        e->lhs = std::move(lhs); e->rhs = std::move(rhs);
        lhs = std::move(e);
    }
    return lhs;
}

ExprPtr Parser::parseRange() { return parseAdditive(); } // rezerwa (patrz parseMatch)

ExprPtr Parser::parseAdditive() {
    ExprPtr lhs = parseMultiplicative();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        std::string op = cur().text;
        advance();
        ExprPtr rhs = parseMultiplicative();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Binary; e->op = op;
        e->lhs = std::move(lhs); e->rhs = std::move(rhs);
        lhs = std::move(e);
    }
    return lhs;
}

ExprPtr Parser::parseMultiplicative() {
    ExprPtr lhs = parsePower();
    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent)) {
        std::string op = cur().text;
        advance();
        ExprPtr rhs = parsePower();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Binary; e->op = op;
        e->lhs = std::move(lhs); e->rhs = std::move(rhs);
        lhs = std::move(e);
    }
    return lhs;
}

ExprPtr Parser::parsePower() {
    ExprPtr lhs = parseUnary();
    while (match(TokenKind::StarStar)) {
        ExprPtr rhs = parseUnary();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Binary; e->op = "**";
        e->lhs = std::move(lhs); e->rhs = std::move(rhs);
        lhs = std::move(e);
    }
    return lhs;
}

ExprPtr Parser::parseUnary() {
    if (check(TokenKind::Minus) || check(TokenKind::Not) || check(TokenKind::Star)) {
        std::string op = cur().text; // `*` jako prefiks = dereferencja Box<T>/shared_ptr
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Unary; e->op = op;
        e->operand = parseUnary();
        return e;
    }
    return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
    ExprPtr expr = parsePrimary();
    int baseLine = expr->line;
    while (true) {
        if (match(TokenKind::Dot)) {
            std::string member = expect(TokenKind::Identifier, "pole/metoda").text;
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Member;
            e->line = baseLine;
            e->object = std::move(expr);
            e->member = member;
            expr = std::move(e);
        } else if (check(TokenKind::LParen)) {
            advance();
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Call;
            e->line = baseLine;
            e->callee = std::move(expr);
            e->args = parseArgList();
            expr = std::move(e);
        } else if (match(TokenKind::LBracket)) {
            bool old = noStructLiteral_; noStructLiteral_ = false;
            auto idx = parseExpression();
            noStructLiteral_ = old;
            expect(TokenKind::RBracket, "]");
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Index;
            e->line = baseLine;
            e->object = std::move(expr);
            e->indexExpr = std::move(idx);
            expr = std::move(e);
        } else {
            break;
        }
    }
    return expr;
}

std::vector<Arg> Parser::parseArgList() {
    std::vector<Arg> args;
    bool old = noStructLiteral_; noStructLiteral_ = false;
    while (!check(TokenKind::RParen)) {
        Arg a;
        if (check(TokenKind::Identifier) && peek().kind == TokenKind::Colon) {
            a.name = advance().text;
            advance(); // ':'
        }
        a.value = parseExpression();
        args.push_back(std::move(a));
        if (!match(TokenKind::Comma)) break;
    }
    noStructLiteral_ = old;
    expect(TokenKind::RParen, ")");
    return args;
}

ExprPtr Parser::parsePrimary() {
    const Token& tok = cur();

    if (tok.kind == TokenKind::IntLiteral) {
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::IntLit; e->intVal = std::stoll(tok.text); e->line = tok.line;
        return e;
    }
    if (tok.kind == TokenKind::FloatLiteral) {
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::FloatLit; e->floatVal = std::stod(tok.text); e->line = tok.line;
        return e;
    }
    if (tok.kind == TokenKind::StringLiteral) {
        advance();
        return makeStringExpr(tok.text, tok.line);
    }
    if (tok.kind == TokenKind::CharLiteral) {
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::CharLit; e->strVal = tok.text; e->line = tok.line;
        return e;
    }
    if (tok.kind == TokenKind::KwTrue || tok.kind == TokenKind::KwFalse) {
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::BoolLit; e->boolVal = (tok.kind == TokenKind::KwTrue);
        return e;
    }
    if (tok.kind == TokenKind::Identifier) {
        advance();
        // Konstruktor generyczny znanych typow runtime'u: `Channel<i32>()`, `List<string>()`, ...
        // (rozroznienie od `a < b` - patrz uzasadnienie w komentarzu ponizej).
        static const std::set<std::string> genericCtorNames = {"Channel", "List", "Map", "Option", "Result", "Box"};
        if (check(TokenKind::Lt) && genericCtorNames.count(tok.text)) {
            Type t;
            t.name = tok.text;
            advance(); // '<'
            do { t.generics.push_back(parseType()); } while (match(TokenKind::Comma));
            expect(TokenKind::Gt, ">");
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Ident;
            e->genericHint = t;
            e->line = tok.line;
            return e; // parsePostfix obsluzy nastepujace `(...)` jako Call
        }
        if (!noStructLiteral_ && check(TokenKind::LBrace)) {
            return parseStructLiteral(tok.text, tok.line);
        }
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Ident; e->name = tok.text; e->line = tok.line;
        return e;
    }
    if (tok.kind == TokenKind::LParen) {
        advance();
        bool old = noStructLiteral_; noStructLiteral_ = false;
        auto inner = parseExpression();
        noStructLiteral_ = old;
        expect(TokenKind::RParen, ")");
        return inner;
    }
    if (tok.kind == TokenKind::KwSpawn) {
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Spawn;
        e->spawnCall = parsePostfix();
        return e;
    }
    if (tok.kind == TokenKind::LBracket) {
        advance();
        bool old = noStructLiteral_; noStructLiteral_ = false;
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::ArrayLit;
        e->line = tok.line;
        while (!check(TokenKind::RBracket)) {
            e->elements.push_back(parseExpression());
            if (!match(TokenKind::Comma)) break;
        }
        noStructLiteral_ = old;
        expect(TokenKind::RBracket, "]");
        return e;
    }

    error("nieoczekiwany token w wyrazeniu: \"" + tok.text + "\"");
}

ast::ExprPtr Parser::parseStructLiteral(const std::string& name, int line) {
    expect(TokenKind::LBrace, "{");
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::StructLit;
    e->name = name;
    e->line = line;
    while (!check(TokenKind::RBrace)) {
        Arg a;
        a.name = expect(TokenKind::Identifier, "pole").text;
        expect(TokenKind::Colon, ":");
        a.value = parseExpression();
        e->args.push_back(std::move(a));
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "}");
    return e;
}

// ---------------- String interpolation ----------------

std::vector<InterpPart> Parser::extractInterpParts(const std::string& raw) {
    return extractInterpParts(raw, 0);
}

std::vector<InterpPart> Parser::extractInterpParts(const std::string& raw, int baseLine) {
    std::vector<InterpPart> parts;
    std::string literal;
    size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] == '{') {
            size_t j = raw.find('}', i + 1);
            if (j == std::string::npos) { literal += raw[i]; i++; continue; }
            if (!literal.empty()) {
                InterpPart p; p.isExpr = false; p.text = literal;
                parts.push_back(std::move(p));
                literal.clear();
            }
            std::string inner = raw.substr(i + 1, j - i - 1);
            // Prawdziwe parsowanie wyrazenia w {..} (a nie wklejanie surowego
            // tekstu) - dzieki temu np. `{env.current_dir()}` poprawnie
            // przechodzi przez rozwiazywanie aliasow namespace (`.` -> `::`).
            InterpPart p;
            p.isExpr = true;
            Lexer subLexer(inner, filename_);
            auto subTokens = subLexer.tokenize();
            Parser subParser(subTokens, filename_);
            p.expr = subParser.parseExpression();
            // Sub-lexer liczy linie od 1 wzgledem SAMEGO fragmentu {..}, nie
            // calego pliku - nadpisujemy numerem linii literalu-rodzica, zeby
            // komunikaty bledow (np. z typecheckera) wskazywaly wlasciwe miejsce.
            if (baseLine > 0 && p.expr) p.expr->line = baseLine;
            parts.push_back(std::move(p));
            i = j + 1;
        } else {
            literal += raw[i];
            i++;
        }
    }
    if (!literal.empty() || parts.empty()) {
        InterpPart p; p.isExpr = false; p.text = literal;
        parts.push_back(std::move(p));
    }
    return parts;
}

ExprPtr Parser::makeStringExpr(const std::string& raw, int line) {
    auto parts = extractInterpParts(raw, line);
    auto e = std::make_unique<Expr>();
    e->line = line;
    if (parts.size() == 1 && !parts[0].isExpr) {
        e->kind = ExprKind::StringLit;
        e->strVal = parts[0].text;
    } else {
        e->kind = ExprKind::StringInterp;
        e->interpParts = std::move(parts);
    }
    return e;
}

} // namespace lg
