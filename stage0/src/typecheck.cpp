#include "typecheck.hpp"

namespace lg {
using namespace ast;

void TypeChecker::pushScope() { scopes_.emplace_back(); }
void TypeChecker::popScope() { scopes_.pop_back(); }

void TypeChecker::declareVar(const std::string& name, bool isMut) {
    if (scopes_.empty()) pushScope();
    scopes_.back()[name] = isMut;
}

bool TypeChecker::isDeclared(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        if (it->count(name)) return true;
    }
    return globalNames_.count(name) != 0;
}

bool TypeChecker::lookupMut(const std::string& name, bool& found) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) { found = true; return f->second; }
    }
    found = false;
    return false;
}

void TypeChecker::error(int line, const std::string& msg) {
    errors_.push_back((line > 0 ? (std::to_string(line) + ": ") : std::string()) + msg);
}

// Rozpoznaje wzorzec konstruktora enuma w `match` (jak w Codegen) - potrzebne
// tylko po to, zeby wiedziec, ze identyfikatory w `Wariant(a, b)` sa NOWYMI
// bindingami wzorca, a nie odwolaniami do juz istniejacych zmiennych.
TypeChecker::EnumPatternInfo TypeChecker::tryParseEnumPattern(const Expr& pattern) {
    EnumPatternInfo info;
    const Expr* memberExpr = nullptr;
    std::vector<std::string> bindings;

    if (pattern.kind == ExprKind::Member) {
        memberExpr = &pattern;
    } else if (pattern.kind == ExprKind::Call && pattern.callee &&
               pattern.callee->kind == ExprKind::Member) {
        memberExpr = pattern.callee.get();
        for (auto& a : pattern.args) {
            if (a.value && a.value->kind == ExprKind::Ident) bindings.push_back(a.value->name);
            else bindings.push_back("_");
        }
    }
    if (!memberExpr || !memberExpr->object || memberExpr->object->kind != ExprKind::Ident) return info;
    if (!enumNames_.count(memberExpr->object->name)) return info;

    info.matched = true;
    info.bindingNames = std::move(bindings);
    return info;
}

void TypeChecker::collectGlobals(const Program& prog) {
    for (auto& s : prog.structs) globalNames_.insert(s.name);
    for (auto& e : prog.enums) { globalNames_.insert(e.name); enumNames_.insert(e.name); }

    for (auto& imp : prog.imports) {
        if (imp.isFrom) {
            for (auto& sym : imp.selective) globalNames_.insert(sym);
        } else {
            globalNames_.insert(imp.alias);
        }
    }

    std::set<std::string> seen;
    for (auto& fn : prog.fns) {
        globalNames_.insert(fn.name);
        if (seen.count(fn.name)) {
            ambiguousFnNames_.insert(fn.name); // >1 definicja - nie sprawdzamy arity (bezpieczenstwo > precyzja)
        }
        seen.insert(fn.name);
        fnArity_[fn.name] = fn.params.size();
    }
}

std::vector<std::string> TypeChecker::check(const Program& prog) {
    errors_.clear();
    globalNames_.clear();
    fnArity_.clear();
    ambiguousFnNames_.clear();
    enumNames_.clear();
    scopes_.clear();

    collectGlobals(prog);

    for (auto& fn : prog.fns) checkFn(fn);
    for (auto& impl : prog.impls) for (auto& m : impl.methods) checkFn(m);

    return errors_;
}

void TypeChecker::checkFn(const FnDecl& fn) {
    scopes_.clear();
    pushScope();
    for (auto& p : fn.params) declareVar(p.name, p.isMut);
    if (fn.body) checkBlock(*fn.body);
    popScope();
}

void TypeChecker::checkBlock(const Stmt& block) {
    pushScope();
    for (auto& st : block.stmts) checkStmt(*st);
    popScope();
}

// Bardzo zachowawcza (celowo!) kompatybilnosc literalu z jawnie podanym typem
// w `let x: T = <literal>`. Rozszerzanie int -> float NIE jest bledem.
static bool literalCompatibleWithType(ExprKind litKind, const std::string& typeName) {
    static const std::set<std::string> intTypes = {
        "i8", "i16", "i32", "i64", "isize", "u8", "u16", "u32", "u64", "usize"};
    static const std::set<std::string> floatTypes = {"f32", "f64"};

    switch (litKind) {
        case ExprKind::IntLit: return intTypes.count(typeName) || floatTypes.count(typeName);
        case ExprKind::FloatLit: return floatTypes.count(typeName);
        case ExprKind::StringLit:
        case ExprKind::StringInterp: return typeName == "string";
        case ExprKind::BoolLit: return typeName == "bool";
        case ExprKind::CharLit: return typeName == "char";
        default: return true; // nie literal - nie oceniamy tutaj
    }
}

void TypeChecker::checkExpr(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Ident:
            if (!e.genericHint && !isDeclared(e.name)) {
                error(e.line, "nieznany identyfikator: '" + e.name + "'");
            }
            break;

        case ExprKind::Binary:
            checkExpr(*e.lhs);
            checkExpr(*e.rhs);
            break;

        case ExprKind::Unary:
            checkExpr(*e.operand);
            break;

        case ExprKind::Assign: {
            checkExpr(*e.rhs);
            if (e.lhs->kind == ExprKind::Ident) {
                bool found = false;
                bool isMut = lookupMut(e.lhs->name, found);
                if (!found) {
                    if (!globalNames_.count(e.lhs->name)) {
                        error(e.lhs->line, "nieznany identyfikator: '" + e.lhs->name + "'");
                    }
                } else if (!isMut) {
                    error(e.lhs->line, "przypisanie do niemutowalnej zmiennej '" + e.lhs->name +
                                            "' (brakuje 'mut' w 'let')");
                }
            } else {
                checkExpr(*e.lhs);
            }
            break;
        }

        case ExprKind::Call: {
            checkExpr(*e.callee);
            for (auto& a : e.args) if (a.value) checkExpr(*a.value);
            if (e.callee->kind == ExprKind::Ident) {
                auto it = fnArity_.find(e.callee->name);
                if (it != fnArity_.end() && !ambiguousFnNames_.count(e.callee->name)) {
                    if (e.args.size() != it->second) {
                        error(e.line, "zla liczba argumentow wywolania '" + e.callee->name +
                                          "': oczekiwano " + std::to_string(it->second) +
                                          ", otrzymano " + std::to_string(e.args.size()));
                    }
                }
            }
            break;
        }

        case ExprKind::Member:
            // Sprawdzamy tylko obiekt - `member` to nazwa pola/metody/wariantu, nie zmienna.
            checkExpr(*e.object);
            break;

        case ExprKind::Index:
            checkExpr(*e.object);
            checkExpr(*e.indexExpr);
            break;

        case ExprKind::Spawn:
            checkExpr(*e.spawnCall);
            break;

        case ExprKind::StructLit:
            // e.name to nazwa struct (nie zmienna) - nie sprawdzamy jej.
            for (auto& a : e.args) if (a.value) checkExpr(*a.value);
            break;

        case ExprKind::ArrayLit:
            for (auto& el : e.elements) if (el) checkExpr(*el);
            break;

        case ExprKind::StringInterp:
            for (auto& p : e.interpParts) if (p.isExpr && p.expr) checkExpr(*p.expr);
            break;

        default:
            break; // literaly - nic do zrobienia
    }
}

void TypeChecker::checkStmt(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Let: {
            if (s.initExpr) checkExpr(*s.initExpr);
            if (s.declType && s.declType->generics.empty() && s.initExpr &&
                !literalCompatibleWithType(s.initExpr->kind, s.declType->name)) {
                error(s.line, "let '" + s.varName + "': zadeklarowany typ '" + s.declType->name +
                                  "' najprawdopodobniej nie pasuje do przypisanej wartosci");
            }
            declareVar(s.varName, s.isMut);
            break;
        }

        case StmtKind::Return:
        case StmtKind::ExprStmt:
        case StmtKind::Throw:
            if (s.expr) checkExpr(*s.expr);
            break;

        case StmtKind::If:
            for (auto& br : s.branches) {
                if (br.cond) checkExpr(*br.cond);
                if (br.body) checkBlock(*br.body);
            }
            break;

        case StmtKind::While:
            if (s.whileCond) checkExpr(*s.whileCond);
            if (s.whileBody) checkBlock(*s.whileBody);
            break;

        case StmtKind::For:
            if (s.forIter) checkExpr(*s.forIter);
            pushScope();
            declareVar(s.forVar, false);
            if (s.forBody) for (auto& st : s.forBody->stmts) checkStmt(*st);
            popScope();
            break;

        case StmtKind::Block:
            checkBlock(s);
            break;

        case StmtKind::Match: {
            if (s.matchSubject) checkExpr(*s.matchSubject);
            for (auto& arm : s.matchArms) {
                std::vector<std::string> bindings;
                if (!arm.isWildcard) {
                    EnumPatternInfo info = tryParseEnumPattern(*arm.pattern);
                    if (info.matched) {
                        bindings = info.bindingNames;
                    } else if (arm.isRange) {
                        checkExpr(*arm.pattern);
                        if (arm.rangeEnd) checkExpr(*arm.rangeEnd);
                    } else {
                        checkExpr(*arm.pattern);
                    }
                }
                pushScope();
                for (auto& b : bindings) if (b != "_") declareVar(b, false);
                if (arm.body) for (auto& st : arm.body->stmts) checkStmt(*st);
                popScope();
            }
            break;
        }

        case StmtKind::On:
            if (s.onCond) checkExpr(*s.onCond);
            if (s.onBody) checkBlock(*s.onBody);
            break;

        case StmtKind::Profile:
            for (auto& r : s.regions) if (r.body) checkBlock(*r.body);
            break;

        case StmtKind::Isolate:
            for (auto& a : s.isolateArgs) if (a.value) checkExpr(*a.value);
            if (s.isolateBody) checkBlock(*s.isolateBody);
            break;

        case StmtKind::ShellExec:
            for (auto& p : s.shellParts) if (p.isExpr && p.expr) checkExpr(*p.expr);
            break;

        case StmtKind::Try:
            if (s.tryBody) checkBlock(*s.tryBody);
            pushScope();
            declareVar(s.catchVarName, false);
            if (s.catchBody) for (auto& st : s.catchBody->stmts) checkStmt(*st);
            popScope();
            break;
    }
}

} // namespace lg
