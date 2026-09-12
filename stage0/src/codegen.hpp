#pragma once
#include "ast.hpp"
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace lg {

// Generator kodu: AST Legendary Lang -> C++20.
// Ograniczenia stage0 opisane w komentarzach przy poszczególnych metodach
// (m.in. `on`, `profile`, `isolate` mają uproszczoną semantykę - patrz README).
class Codegen {
public:
    std::string generate(const ast::Program& prog);

private:
    std::ostringstream out_;
    int indent_ = 0;
    std::set<std::string> namespaceAliases_; // aliasy importów traktowane jako C++ namespace
    std::set<std::string> enumNames_;        // nazwy enum -> `.` staje się `::`
    std::string currentSourceFile_;          // do `#line` - plik aktualnie emitowanej deklaracji
    std::map<std::string, std::vector<std::string>> structFieldOrder_; // struct -> pola w kolejnosci deklaracji
    // enum -> wariant -> nazwy pol C++ w wygenerowanym tagged-struct (puste = brak payloadu)
    std::map<std::string, std::map<std::string, std::vector<std::string>>> enumVariantFields_;

    void line(const std::string& s);
    std::string ind() const;
    void emitLineDirective(int srcLine); // `#line N "plik.lg"` - mapowanie bledow do zrodla

    std::string cppType(const ast::Type& t);
    std::string emitExpr(const ast::Expr& e, bool isCallCallee = false);
    std::string emitArgs(const std::vector<ast::Arg>& args);
    std::string emitInterp(const std::vector<ast::InterpPart>& parts);
    std::string emitShellInterp(const std::vector<ast::InterpPart>& parts);
    std::string emitTypeParamsPrefix(const std::vector<std::string>& typeParams);

    void emitImport(const ast::ImportDecl& imp);
    void emitStruct(const ast::StructDecl& s, const std::vector<const ast::FnDecl*>& methods);
    void emitEnum(const ast::EnumDecl& e);
    void emitFn(const ast::FnDecl& fn);
    void emitMethod(const ast::FnDecl& m); // metoda wewnatrz struct (self -> this)
    void emitParams(const std::vector<ast::Param>& params);
    void emitBlock(const ast::Stmt& block);
    void emitStmt(const ast::Stmt& s);

    // Obsluga niejawnego zwracania ostatniego wyrazenia bloku (jak w Rust):
    // `fn sum(self) -> f64 { self.x + self.y }` == `{ return self.x + self.y; }`
    // Dziala rekurencyjnie przez `match`/`if` w pozycji koncowej funkcji.
    void emitBodyWithImplicitReturn(const ast::Stmt& block, bool hasReturnValue);
    void emitTailStmt(const ast::Stmt& s, bool wantsValue);
    // Wspolna logika lancucha if/else-if/else dla `match` (uzywana i jako
    // zwykla instrukcja, i w pozycji koncowej z niejawnym `return`).
    void emitMatchArmsChain(const std::vector<ast::MatchArm>& arms, bool wantsValue);

    // Rozpoznaje wzorzec konstruktora enuma w `match` (np. `Expr.Binary(l, op, r)`
    // albo bezargumentowe `EventKind.DiskFull`) i zwraca info potrzebne do
    // wygenerowania testu tagu + bindowania pol.
    struct EnumPatternInfo {
        bool matched = false;
        std::string enumName;
        std::string variant;
        std::vector<std::string> bindingNames; // puste dla wariantu bez payloadu
    };
    EnumPatternInfo tryParseEnumPattern(const ast::Expr& pattern);
    void emitMatchArmBody(const ast::MatchArm& arm, const EnumPatternInfo& info, bool wantsValue);
};

} // namespace lg
