#pragma once
#include "config_ast.hpp"
#include "token.hpp"
#include <vector>

namespace lg::cfg {

// Parsuje config.lg do surowego drzewa blokow { "project": Value::Obj, ... },
// a nastepnie waliduje je do ConfigModel. Kazde odstepstwo od sekcji
// project/dependencies/build/tasks (lub nieznane pole w nich) jest bledem -
// zgodnie z zalozeniem "inna struktura = blad" z SPEC.md.
class ConfigParser {
public:
    ConfigParser(std::vector<Token> tokens, std::string filename);

    ConfigModel parse();

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;
    std::string filename_;

    const Token& cur() const;
    const Token& peek(size_t off = 1) const;
    const Token& advance();
    bool check(TokenKind k) const;
    bool match(TokenKind k);
    const Token& expect(TokenKind k, const std::string& what);
    [[noreturn]] void error(const std::string& msg) const;

    Value parseValue();
    Value parseObjectBody(); // zaklada, ze '{' juz skonsumowane; konsumuje '}'
    Value parseArrayBody();  // zaklada, ze '[' juz skonsumowane; konsumuje ']'

    void validateProject(const Value& v, ConfigModel& out);
    void validateDependencies(const Value& v, ConfigModel& out);
    void validateBuild(const Value& v, ConfigModel& out);
    void validateTasks(const Value& v, ConfigModel& out);
};

} // namespace lg::cfg
