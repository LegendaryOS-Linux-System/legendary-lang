#pragma once
#include "ast.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace lg {

// Podstawowy, SWIADOMIE KONSERWATYWNY typechecker. Wylapuje najczestsze
// pomylki zanim dojdzie do (czesto nieczytelnych) bledow g++:
//   - odwolania do nieznanych identyfikatorow (literowki w nazwach zmiennych)
//   - zla liczba argumentow w wywolaniach WOLNYCH funkcji (nie metod - te
//     pomijamy, bo `.` jest tez uzywane dla namespace'ow i nie odrozniamy
//     tego w prosty sposob bez pelnego typowania)
//   - przypisanie do zmiennej zadeklarowanej bez `mut`
//   - oczywista niezgodnosc literalu z jawnie zadeklarowanym typem w `let`
//
// To NIE jest pelny system typow z inferencja typow ogolnych wyrazen -
// jesli czegos nie jestesmy pewni, wolimy PRZEPUSCIC (permissive) niz
// zglosic falszywy alarm blokujacy poprawny program.
class TypeChecker {
public:
    // Zwraca liste komunikatow bledow (pusta = OK).
    std::vector<std::string> check(const ast::Program& prog);

private:
    std::set<std::string> globalNames_;         // fn/struct/enum/aliasy importow/selektywne importy
    std::map<std::string, size_t> fnArity_;      // nazwa wolnej fn -> liczba parametrow
    std::set<std::string> ambiguousFnNames_;     // >1 definicja tej samej nazwy - pomijamy arity-check
    std::set<std::string> enumNames_;
    std::vector<std::map<std::string, bool>> scopes_; // stos scope'ow: nazwa -> czy `mut`
    std::vector<std::string> errors_;

    void collectGlobals(const ast::Program& prog);
    void checkFn(const ast::FnDecl& fn);
    void checkBlock(const ast::Stmt& block);
    void checkStmt(const ast::Stmt& s);
    void checkExpr(const ast::Expr& e);

    void pushScope();
    void popScope();
    void declareVar(const std::string& name, bool isMut);
    bool isDeclared(const std::string& name) const;
    bool lookupMut(const std::string& name, bool& found) const;
    void error(int line, const std::string& msg);

    struct EnumPatternInfo {
        bool matched = false;
        std::vector<std::string> bindingNames;
    };
    EnumPatternInfo tryParseEnumPattern(const ast::Expr& pattern);
};

} // namespace lg
