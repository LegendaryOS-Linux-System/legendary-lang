#include "lexer.hpp"
#include <cctype>
#include <iostream>
#include <stdexcept>

namespace lg {

Lexer::Lexer(std::string source, std::string filename)
    : src_(std::move(source)), filename_(std::move(filename)) {}

bool Lexer::atEnd() const { return pos_ >= src_.size(); }

char Lexer::peek(size_t offset) const {
    size_t i = pos_ + offset;
    return i < src_.size() ? src_[i] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { line_++; col_ = 1; } else { col_++; }
    return c;
}

bool Lexer::match(char expected) {
    if (atEnd() || src_[pos_] != expected) return false;
    advance();
    return true;
}

void Lexer::skipWhitespaceExceptNewline() {
    while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) {
        advance();
    }
}

void Lexer::error(const std::string& msg) const {
    throw std::runtime_error(filename_ + ":" + std::to_string(line_) + ":" +
                              std::to_string(col_) + ": " + msg);
}

// Obsługuje: `-- linia`, `--- dokumentacja`, `--( blok wieloliniowy )--`
Token Lexer::lexComment() {
    int startLine = line_, startCol = col_;
    // zjadamy pierwsze "--"
    advance(); advance();

    if (peek() == '(') {
        advance(); // '('
        std::string content;
        while (!atEnd() && !(peek() == ')' && peek(1) == '-' && peek(2) == '-')) {
            content += advance();
        }
        if (atEnd()) error("niezamknięty komentarz blokowy --( ... )--");
        advance(); advance(); advance(); // )--
        return Token{TokenKind::BlockComment, content, startLine, startCol};
    }

    bool isDoc = false;
    if (peek() == '-') { isDoc = true; advance(); }

    std::string content;
    while (!atEnd() && peek() != '\n') content += advance();

    return Token{isDoc ? TokenKind::DocComment : TokenKind::LineComment,
                 content, startLine, startCol};
}

Token Lexer::lexIdentifierOrKeyword() {
    int startLine = line_, startCol = col_;
    std::string text;
    while (!atEnd() && (std::isalnum((unsigned char)peek()) || peek() == '_')) {
        text += advance();
    }
    auto& kw = keywords();
    auto it = kw.find(text);
    TokenKind kind = (it != kw.end()) ? it->second : TokenKind::Identifier;
    return Token{kind, text, startLine, startCol};
}

Token Lexer::lexNumber() {
    int startLine = line_, startCol = col_;
    std::string text;
    bool isFloat = false;
    while (!atEnd() && std::isdigit((unsigned char)peek())) text += advance();
    if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
        isFloat = true;
        text += advance();
        while (!atEnd() && std::isdigit((unsigned char)peek())) text += advance();
    }
    return Token{isFloat ? TokenKind::FloatLiteral : TokenKind::IntLiteral,
                 text, startLine, startCol};
}

Token Lexer::lexString() {
    int startLine = line_, startCol = col_;
    advance(); // otwierający "
    std::string text;
    while (!atEnd() && peek() != '"') {
        char c = advance();
        if (c == '\\' && !atEnd()) {
            char esc = advance();
            switch (esc) {
                case 'n': text += '\n'; break;
                case 't': text += '\t'; break;
                case '"': text += '"'; break;
                case '\\': text += '\\'; break;
                default: text += esc; break;
            }
        } else {
            text += c;
        }
    }
    if (atEnd()) error("niezamknięty string literal");
    advance(); // zamykający "
    return Token{TokenKind::StringLiteral, text, startLine, startCol};
}

Token Lexer::lexChar() {
    int startLine = line_, startCol = col_;
    advance(); // '
    std::string text;
    if (!atEnd() && peek() != '\'') text += advance();
    if (peek() != '\'') error("niezamknięty char literal");
    advance();
    return Token{TokenKind::CharLiteral, text, startLine, startCol};
}

// `$ ... $` — inline shell literal. Zawartość jest surowym tekstem;
// segmenty `{ident}` są interpolowane w Parserze/Codegenie (tak jak w stringach).
Token Lexer::lexShellLiteral() {
    int startLine = line_, startCol = col_;
    advance(); // pierwszy '$'
    std::string content;
    while (!atEnd() && peek() != '$') {
        content += advance();
    }
    if (atEnd()) error("niezamkniety literal shellowy $ ... $");
    advance(); // zamykajacy '$'
    return Token{TokenKind::ShellLiteral, content, startLine, startCol};
}

Token Lexer::lexSymbol() {
    int startLine = line_, startCol = col_;
    char c = advance();
    TokenKind kind = TokenKind::Unknown;
    std::string text(1, c);

    switch (c) {
        case '(': kind = TokenKind::LParen; break;
        case ')': kind = TokenKind::RParen; break;
        case '{': kind = TokenKind::LBrace; break;
        case '}': kind = TokenKind::RBrace; break;
        case '[': kind = TokenKind::LBracket; break;
        case ']': kind = TokenKind::RBracket; break;
        case ',': kind = TokenKind::Comma; break;
        case '@': kind = TokenKind::At; break;
        case '$': kind = TokenKind::Dollar; break;
        case '.':
            if (match('.')) {
                if (match('=')) { kind = TokenKind::DotDotEq; text = "..="; }
                else { kind = TokenKind::Unknown; text = ".."; }
            } else { kind = TokenKind::Dot; }
            break;
        case ':':
            if (match(':')) { kind = TokenKind::DoubleColon; text = "::"; }
            else { kind = TokenKind::Colon; }
            break;
        case '-':
            if (match('>')) { kind = TokenKind::Arrow; text = "->"; }
            else { kind = TokenKind::Minus; }
            break;
        case '=':
            if (match('=')) { kind = TokenKind::EqEq; text = "=="; }
            else if (match('>')) { kind = TokenKind::FatArrow; text = "=>"; }
            else { kind = TokenKind::Eq; }
            break;
        case '!':
            if (match('=')) { kind = TokenKind::NotEq; text = "!="; }
            else { kind = TokenKind::Not; }
            break;
        case '<':
            if (match('=')) { kind = TokenKind::LtEq; text = "<="; }
            else { kind = TokenKind::Lt; }
            break;
        case '>':
            if (match('=')) { kind = TokenKind::GtEq; text = ">="; }
            else { kind = TokenKind::Gt; }
            break;
        case '&':
            if (match('&')) { kind = TokenKind::AndAnd; text = "&&"; }
            break;
        case '|':
            if (match('|')) { kind = TokenKind::OrOr; text = "||"; }
            break;
        case '+': kind = TokenKind::Plus; break;
        case '*':
            if (match('*')) { kind = TokenKind::StarStar; text = "**"; }
            else { kind = TokenKind::Star; }
            break;
        case '/': kind = TokenKind::Slash; break;
        case '%': kind = TokenKind::Percent; break;
        default: break;
    }
    return Token{kind, text, startLine, startCol};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (!atEnd()) {
        skipWhitespaceExceptNewline();
        if (atEnd()) break;

        char c = peek();

        if (c == '\n') {
            advance();
            tokens.push_back(Token{TokenKind::NewLine, "\\n", line_, col_});
            continue;
        }

        if (c == '-' && peek(1) == '-') {
            tokens.push_back(lexComment());
            continue;
        }

        if (std::isalpha((unsigned char)c) || c == '_') {
            tokens.push_back(lexIdentifierOrKeyword());
            continue;
        }

        if (std::isdigit((unsigned char)c)) {
            tokens.push_back(lexNumber());
            continue;
        }

        if (c == '"') {
            tokens.push_back(lexString());
            continue;
        }

        if (c == '\'') {
            tokens.push_back(lexChar());
            continue;
        }

        if (c == '$') {
            tokens.push_back(lexShellLiteral());
            continue;
        }

        tokens.push_back(lexSymbol());
    }

    tokens.push_back(Token{TokenKind::Eof, "", line_, col_});
    return tokens;
}

} // namespace lg
