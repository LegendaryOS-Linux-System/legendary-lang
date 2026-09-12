#pragma once
#include "token.hpp"
#include <string>
#include <vector>

namespace lg {

// Lexer stage0: obsługuje podstawowy zestaw tokenów potrzebny do skompilowania
// pierwszych programów Legendary Lang (patrz przykłady w /examples).
// Rozpoznaje 3 rodzaje komentarzy: `--`, `---`, `--( ... )--`.
class Lexer {
public:
    explicit Lexer(std::string source, std::string filename = "<stdin>");

    std::vector<Token> tokenize();

private:
    std::string src_;
    std::string filename_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    [[nodiscard]] bool atEnd() const;
    [[nodiscard]] char peek(size_t offset = 0) const;
    char advance();
    bool match(char expected);

    void skipWhitespaceExceptNewline();
    Token lexComment();
    Token lexIdentifierOrKeyword();
    Token lexNumber();
    Token lexString();
    Token lexChar();
    Token lexShellLiteral();
    Token lexSymbol();

    [[noreturn]] void error(const std::string& msg) const;
};

} // namespace lg
