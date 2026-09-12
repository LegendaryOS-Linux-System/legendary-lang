#include "codegen.hpp"
#include <stdexcept>

namespace lg {
using namespace ast;

std::string Codegen::ind() const { return std::string(indent_ * 4, ' '); }

void Codegen::line(const std::string& s) { out_ << ind() << s << "\n"; }

void Codegen::emitLineDirective(int srcLine) {
    // Mapuje wygenerowany C++ z powrotem na oryginalny plik .lg, zeby bledy
    // kompilacji g++ wskazywaly linie w zrodle Legendary, nie w wygenerowanym .cpp.
    if (srcLine <= 0 || currentSourceFile_.empty()) return;
    out_ << "#line " << srcLine << " \"" << currentSourceFile_ << "\"\n";
}

std::string Codegen::emitTypeParamsPrefix(const std::vector<std::string>& typeParams) {
    if (typeParams.empty()) return "";
    std::string out = "template <";
    for (size_t i = 0; i < typeParams.size(); ++i) {
        if (i) out += ", ";
        out += "typename " + typeParams[i];
    }
    out += ">";
    return out;
}

// ---------------- Typy ----------------

std::string Codegen::cppType(const Type& t) {
    static const std::pair<const char*, const char*> primitives[] = {
        {"i8", "int8_t"}, {"i16", "int16_t"}, {"i32", "int32_t"}, {"i64", "int64_t"},
        {"isize", "ptrdiff_t"},
        {"u8", "uint8_t"}, {"u16", "uint16_t"}, {"u32", "uint32_t"}, {"u64", "uint64_t"},
        {"usize", "size_t"},
        {"f32", "float"}, {"f64", "double"},
        {"bool", "bool"}, {"string", "std::string"}, {"char", "char"}, {"void", "void"},
    };
    for (auto& p : primitives) if (t.name == p.first) return p.second;

    if (t.name == "List" && t.generics.size() == 1)
        return "std::vector<" + cppType(t.generics[0]) + ">";
    if (t.name == "Map" && t.generics.size() == 2)
        return "std::unordered_map<" + cppType(t.generics[0]) + ", " + cppType(t.generics[1]) + ">";
    if (t.name == "Option" && t.generics.size() == 1)
        return "std::optional<" + cppType(t.generics[0]) + ">";
    if (t.name == "Channel" && t.generics.size() == 1)
        return "lg::Channel<" + cppType(t.generics[0]) + ">";
    if (t.name == "Result" && t.generics.size() == 2)
        return "lg::Result<" + cppType(t.generics[0]) + ", " + cppType(t.generics[1]) + ">";
    if (t.name == "Box" && t.generics.size() == 1)
        return "std::shared_ptr<" + cppType(t.generics[0]) + ">";

    // typ nierozpoznany (struct uzytkownika lub typ z extern bindingu) -> przechodzi 1:1
    std::string name = t.name;
    if (!t.generics.empty()) {
        name += "<";
        for (size_t i = 0; i < t.generics.size(); ++i) {
            if (i) name += ", ";
            name += cppType(t.generics[i]);
        }
        name += ">";
    }
    return name;
}

// ---------------- Wyrazenia ----------------

static std::string escapeCppString(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string Codegen::emitInterp(const std::vector<InterpPart>& parts) {
    std::ostringstream s;
    s << "([&]{ std::ostringstream lg_ss; ";
    for (auto& p : parts) {
        if (p.isExpr) s << "lg_ss << (" << emitExpr(*p.expr) << "); ";
        else s << "lg_ss << \"" << escapeCppString(p.text) << "\"; ";
    }
    s << "return lg_ss.str(); }())";
    return s.str();
}

std::string Codegen::emitShellInterp(const std::vector<InterpPart>& parts) {
    // Buduje std::string polecenia z interpolacja {zmienna} - analogicznie do stringow.
    return emitInterp(parts);
}

std::string Codegen::emitArgs(const std::vector<Arg>& args) {
    // UWAGA (stage0): C++ nie ma argumentow nazwanych - nazwy z Legendary
    // (np. `name: "lpm"`) sa odrzucane, przekazujemy je pozycyjnie w
    // kolejnosci z kodu zrodlowego. Docelowo (stage1) codegen powinien
    // mapowac nazwane argumenty na odpowiadajace parametry funkcji/konstruktora.
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) out += ", ";
        out += emitExpr(*args[i].value);
    }
    return out;
}

std::string Codegen::emitExpr(const Expr& e, bool isCallCallee) {
    switch (e.kind) {
        case ExprKind::IntLit: return std::to_string(e.intVal);
        case ExprKind::FloatLit: return std::to_string(e.floatVal);
        case ExprKind::StringLit: return "std::string(\"" + escapeCppString(e.strVal) + "\")";
        case ExprKind::StringInterp: return emitInterp(e.interpParts);
        case ExprKind::BoolLit: return e.boolVal ? "true" : "false";
        case ExprKind::CharLit: return std::string("'") + (e.strVal.empty() ? ' ' : e.strVal[0]) + "'";
        case ExprKind::Ident: {
            if (e.genericHint) {
                // `Box<T>(args)` -> `std::make_shared<T>(args)` (nie shared_ptr<T>(args)!)
                if (e.genericHint->name == "Box" && e.genericHint->generics.size() == 1) {
                    return "std::make_shared<" + cppType(e.genericHint->generics[0]) + ">";
                }
                return cppType(*e.genericHint);
            }
            return e.name;
        }

        case ExprKind::Binary:
            return "(" + emitExpr(*e.lhs) + " " + e.op + " " + emitExpr(*e.rhs) + ")";

        case ExprKind::Unary:
            return "(" + e.op + emitExpr(*e.operand) + ")";

        case ExprKind::Assign:
            return "(" + emitExpr(*e.lhs) + " = " + emitExpr(*e.rhs) + ")";

        case ExprKind::Call:
            // `isCallCallee=true` przy emisji callee: pozwala Member ponizej
            // odroznic "to jest wywolywane, Call dopisze swoje (args)" od
            // "to jest uzyte jako gola wartosc" (np. bezargumentowy wariant enuma).
            return emitExpr(*e.callee, true) + "(" + emitArgs(e.args) + ")";

        case ExprKind::Member: {
            // `self.pole` -> `this->pole` (metody z impl staja sie zwyklymi
            // niestatycznymi skladowymi struct, wiec `self` nie istnieje w C++).
            if (e.object->kind == ExprKind::Ident && e.object->name == "self") {
                return "this->" + e.member;
            }
            // Enum: `EnumName.Wariant` -> `EnumName::Wariant` (fabryka statyczna,
            // patrz emitEnum). Bezargumentowy wariant uzyty jako WARTOSC (nie
            // jako callee Call) dostaje automatycznie dopisane `()`, zeby
            // `EventKind.DiskFull` dzialalo bez jawnych nawiasow.
            if (e.object->kind == ExprKind::Ident && enumNames_.count(e.object->name)) {
                const std::string& enumName = e.object->name;
                std::string base = enumName + "::" + e.member;
                auto enumIt = enumVariantFields_.find(enumName);
                if (enumIt != enumVariantFields_.end()) {
                    auto varIt = enumIt->second.find(e.member);
                    if (varIt != enumIt->second.end() && !isCallCallee && varIt->second.empty()) {
                        return base + "()";
                    }
                }
                return base;
            }
            // Jesli obiektem jest bare-identyfikator odpowiadajacy zaimportowanemu
            // modulowi (np. `io`, `cli`, `dnf`), traktujemy `.` jako C++ `::`.
            if (e.object->kind == ExprKind::Ident && namespaceAliases_.count(e.object->name)) {
                return e.object->name + "::" + e.member;
            }
            return emitExpr(*e.object) + "." + e.member;
        }

        case ExprKind::Index:
            return emitExpr(*e.object) + "[" + emitExpr(*e.indexExpr) + "]";

        case ExprKind::Spawn:
            // `spawn f(args)` -> lekki watek (stage0: std::thread, docelowo jthread/coroutine).
            // Rozszerzenie GNU (statement-expression) - kompilowane pod GCC/Clang.
            return "({ std::thread([=]{ " + emitExpr(*e.spawnCall) + "; }).detach(); 0; })";

        case ExprKind::StructLit: {
            // Wartosci sa ukladane wg KOLEJNOSCI POL W DEKLARACJI struct (nie
            // wg kolejnosci podanej w literale) - poprawne niezaleznie od tego,
            // w jakiej kolejnosci uzytkownik wypisal pola w `{ }`. Brakujace
            // pole dostaje wartosc domyslna `{}` (value-initialization).
            auto it = structFieldOrder_.find(e.name);
            std::string out = e.name + "{";
            if (it != structFieldOrder_.end()) {
                for (size_t i = 0; i < it->second.size(); ++i) {
                    if (i) out += ", ";
                    const std::string& fieldName = it->second[i];
                    const Expr* found = nullptr;
                    for (auto& a : e.args) if (a.name == fieldName) { found = a.value.get(); break; }
                    out += found ? emitExpr(*found) : "{}";
                }
            } else {
                // struct nieznany na tym etapie kompilacji (np. zdefiniowany w
                // naglowku C z extern bindingu) -> awaryjnie kolejnosc z literalu.
                for (size_t i = 0; i < e.args.size(); ++i) {
                    if (i) out += ", ";
                    out += emitExpr(*e.args[i].value);
                }
            }
            out += "}";
            return out;
        }

        case ExprKind::ArrayLit: {
            // `[1, 2, 3]` -> `std::vector{1, 2, 3}` (CTAD, C++17+).
            // UWAGA: literal pusty `[]` nie zadziala bez adnotacji typu (CTAD
            // nie ma z czego wywnioskowac typu elementu) - stage0 nie wspiera tego przypadku.
            std::string out = "std::vector{";
            for (size_t i = 0; i < e.elements.size(); ++i) {
                if (i) out += ", ";
                out += emitExpr(*e.elements[i]);
            }
            out += "}";
            return out;
        }
    }
    throw std::runtime_error("codegen: nieobslugiwany rodzaj wyrazenia");
}

// ---------------- Instrukcje ----------------

void Codegen::emitBlock(const Stmt& block) {
    line("{");
    indent_++;
    for (auto& st : block.stmts) emitStmt(*st);
    indent_--;
    line("}");
}

void Codegen::emitBodyWithImplicitReturn(const Stmt& block, bool hasReturnValue) {
    line("{");
    indent_++;
    if (!block.stmts.empty()) {
        for (size_t i = 0; i + 1 < block.stmts.size(); ++i) emitStmt(*block.stmts[i]);
        emitTailStmt(*block.stmts.back(), hasReturnValue);
    }
    indent_--;
    line("}");
}

void Codegen::emitTailStmt(const Stmt& s, bool wantsValue) {
    if (!wantsValue) { emitStmt(s); return; }
    emitLineDirective(s.line);

    if (s.kind == StmtKind::ExprStmt) {
        line("return " + emitExpr(*s.expr) + ";");
        return;
    }

    if (s.kind == StmtKind::Match) {
        line("{");
        indent_++;
        line("[[maybe_unused]] auto&& lg_match_subject = (" + emitExpr(*s.matchSubject) + ");");
        emitMatchArmsChain(s.matchArms, true);
        indent_--;
        line("}");
        return;
    }

    if (s.kind == StmtKind::If) {
        for (size_t i = 0; i < s.branches.size(); ++i) {
            const auto& br = s.branches[i];
            std::string kw = (i == 0) ? "if" : (br.cond ? "else if" : "else");
            if (i > 0) { out_.seekp(-1, std::ios_base::cur); out_ << " " << kw; }
            else out_ << ind() << kw;
            if (br.cond) out_ << " (" << emitExpr(*br.cond) << ") ";
            else out_ << " ";
            emitBodyWithImplicitReturn(*br.body, true);
        }
        // UWAGA (stage0): jesli lancuch if/else nie ma galezi `else`, brak
        // pokrycia wszystkich przypadkow moze prowadzic do "brak return" w C++.
        return;
    }

    // Domyslnie: instrukcja bez sensownej wartosci (let/while/for/...) - brak niejawnego return.
    emitStmt(s);
}

Codegen::EnumPatternInfo Codegen::tryParseEnumPattern(const Expr& pattern) {
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
            else bindings.push_back("_"); // nietypowy wzorzec - binding ignorowany
        }
    }
    if (!memberExpr || !memberExpr->object || memberExpr->object->kind != ExprKind::Ident) return info;

    const std::string& enumName = memberExpr->object->name;
    if (!enumNames_.count(enumName)) return info;
    auto enumIt = enumVariantFields_.find(enumName);
    if (enumIt == enumVariantFields_.end() || !enumIt->second.count(memberExpr->member)) return info;

    info.matched = true;
    info.enumName = enumName;
    info.variant = memberExpr->member;
    info.bindingNames = std::move(bindings);
    return info;
}

// Emituje cialo jednego ramienia `match`. Dla wzorca konstruktora enuma z
// bindingami (`Binary(l, op, r) => ...`) dowiazuje pola przed instrukcjami
// ciala; w przeciwnym razie zachowanie jak dotychczas (emitBlock/emitBodyWithImplicitReturn).
void Codegen::emitMatchArmBody(const MatchArm& arm, const EnumPatternInfo& info, bool wantsValue) {
    if (info.matched && !info.bindingNames.empty()) {
        auto& fields = enumVariantFields_[info.enumName][info.variant];
        out_ << "{\n";
        indent_++;
        for (size_t i = 0; i < info.bindingNames.size() && i < fields.size(); ++i) {
            line("auto&& " + info.bindingNames[i] + " = lg_match_subject." + fields[i] + ";");
        }
        if (!arm.body->stmts.empty()) {
            for (size_t i = 0; i + 1 < arm.body->stmts.size(); ++i) emitStmt(*arm.body->stmts[i]);
            emitTailStmt(*arm.body->stmts.back(), wantsValue);
        }
        indent_--;
        out_ << ind() << "}\n";
        return;
    }
    if (wantsValue) emitBodyWithImplicitReturn(*arm.body, true);
    else emitBlock(*arm.body);
}

void Codegen::emitMatchArmsChain(const std::vector<MatchArm>& arms, bool wantsValue) {
    bool wroteAny = false;
    for (size_t i = 0; i < arms.size(); ++i) {
        const auto& arm = arms[i];
        std::string cond;
        EnumPatternInfo info;
        if (!arm.isWildcard) {
            info = tryParseEnumPattern(*arm.pattern);
            if (info.matched) {
                cond = "(lg_match_subject.kind == " + info.enumName + "::Kind::" + info.variant + ")";
            } else if (arm.isRange) {
                cond = "(lg_match_subject >= (" + emitExpr(*arm.pattern) + ") && lg_match_subject <= (" +
                       emitExpr(*arm.rangeEnd) + "))";
            } else {
                cond = "(lg_match_subject == (" + emitExpr(*arm.pattern) + "))";
            }
        }

        if (!wroteAny) {
            out_ << ind();
            if (!arm.isWildcard) out_ << "if (" << cond << ") ";
            // wildcard `_` jako pierwsze/jedyne ramie -> bezwarunkowy blok, bez `if`/`else`
        } else {
            out_.seekp(-1, std::ios_base::cur);
            if (arm.isWildcard) out_ << " else ";
            else out_ << " else if (" << cond << ") ";
        }
        wroteAny = true;

        emitMatchArmBody(arm, info, wantsValue);

        if (arm.isWildcard) break; // `_` konczy lancuch (jak w Rust)
    }
}

void Codegen::emitStmt(const Stmt& s) {
    emitLineDirective(s.line);
    switch (s.kind) {
        case StmtKind::Let: {
            std::string typeStr = s.declType ? cppType(*s.declType) : "auto";
            line(typeStr + " " + s.varName + " = " + emitExpr(*s.initExpr) + ";");
            break;
        }
        case StmtKind::Return: {
            if (s.expr) line("return " + emitExpr(*s.expr) + ";");
            else line("return;");
            break;
        }
        case StmtKind::ExprStmt: {
            line(emitExpr(*s.expr) + ";");
            break;
        }
        case StmtKind::If: {
            for (size_t i = 0; i < s.branches.size(); ++i) {
                const auto& br = s.branches[i];
                std::string kw = (i == 0) ? "if" : (br.cond ? "else if" : "else");
                if (i > 0) {
                    // dopisz "else"/"else if" do tej samej linii co poprzedni "}"
                    out_.seekp(-1, std::ios_base::cur); // usun ostatni '\n'
                    out_ << " " << kw;
                } else {
                    out_ << ind() << kw;
                }
                if (br.cond) out_ << " (" << emitExpr(*br.cond) << ") ";
                else out_ << " ";
                indent_++; indent_--; // brak zmiany, emitBlock sam zarzadza wcieciem
                // emitBlock doda wlasny ind() na "{" - unikamy podwojnego wciecia:
                out_ << "{\n";
                indent_++;
                for (auto& st : br.body->stmts) emitStmt(*st);
                indent_--;
                out_ << ind() << "}\n";
            }
            break;
        }
        case StmtKind::While: {
            line("while (" + emitExpr(*s.whileCond) + ") ");
            out_.seekp(-1, std::ios_base::cur);
            emitBlock(*s.whileBody);
            break;
        }
        case StmtKind::For: {
            line("for (auto&& " + s.forVar + " : (" + emitExpr(*s.forIter) + ")) ");
            out_.seekp(-1, std::ios_base::cur);
            emitBlock(*s.forBody);
            break;
        }
        case StmtKind::Block: {
            emitBlock(s);
            break;
        }
        case StmtKind::Match: {
            line("{");
            indent_++;
            line("[[maybe_unused]] auto&& lg_match_subject = (" + emitExpr(*s.matchSubject) + ");");
            emitMatchArmsChain(s.matchArms, false);
            indent_--;
            line("}");
            break;
        }
        case StmtKind::On: {
            // Prawdziwy polling w tle (nie "sprawdz raz") - patrz lg::reactive::on_condition.
            // UWAGA: warunek jest wykonywany przez WATEK W TLE co ~200ms i
            // callback odpala sie na kazdym przejsciu false->true. Zmienne
            // lokalne w `cond`/ciele sa przechwycone PRZEZ WARTOSC (kopia z
            // momentu rejestracji) - dla prawdziwej "swiezosci" `cond` powinien
            // wywolywac funkcje pobierajace dane na zywo, a nie odczytywac
            // migawke zlapana wczesniej przez `let`.
            line("lg::reactive::on_condition([=]{ return (" + emitExpr(*s.onCond) + "); }, [=] ");
            out_.seekp(-1, std::ios_base::cur);
            emitBlock(*s.onBody);
            out_.seekp(-1, std::ios_base::cur);
            out_ << ");\n";
            break;
        }
        case StmtKind::Profile: {
            line("// profile \"" + s.profileLabel + "\" (stage0: zero-overhead, brak licznikow debug)");
            for (auto& r : s.regions) {
                line("// region " + r.name);
                emitBlock(*r.body);
            }
            break;
        }
        case StmtKind::Isolate: {
            // Tylko polecenia `$ ... $` w tym bloku sa faktycznie piaskowane
            // (przez lg::sandbox::run - patrz runtime) - to zgodne z tym, jak
            // dziala prawdziwy sandboxing (izoluje sie PODPROCESY, nie kod
            // dzialajacy w tym samym procesie). Pozostaly kod w bloku (jesli
            // jakikolwiek) wykonuje sie normalnie, bez izolacji.
            bool allowNetwork = true;
            bool readOnlyFs = false;
            for (auto& a : s.isolateArgs) {
                if (!a.value || a.value->kind != ExprKind::BoolLit) continue;
                if (a.name == "network") allowNetwork = a.value->boolVal;
                else if (a.name == "read_only_fs") readOnlyFs = a.value->boolVal;
            }
            std::string netStr = allowNetwork ? "true" : "false";
            std::string roStr = readOnlyFs ? "true" : "false";
            line("// isolate(network: " + netStr + ", read_only_fs: " + roStr +
                 ") -- sandboxowane sa tylko polecenia `$ ... $` (lg::sandbox::run)");
            out_ << ind() << "{\n";
            indent_++;
            for (auto& st : s.isolateBody->stmts) {
                if (st->kind == StmtKind::ShellExec) {
                    emitLineDirective(st->line);
                    line("lg::sandbox::run((" + emitShellInterp(st->shellParts) + "), " +
                         netStr + ", " + roStr + ");");
                } else {
                    emitStmt(*st);
                }
            }
            indent_--;
            line("}");
            break;
        }
        case StmtKind::ShellExec: {
            line("std::system((" + emitShellInterp(s.shellParts) + ").c_str()); "
                 "// TODO(stage1): sandbox zamiast bezposredniego std::system");
            break;
        }
        case StmtKind::Throw: {
            // `throw <expr>` zaklada, ze <expr> jest stringiem (komunikatem bledu).
            line("throw std::runtime_error(" + emitExpr(*s.expr) + ");");
            break;
        }
        case StmtKind::Try: {
            line("try ");
            out_.seekp(-1, std::ios_base::cur);
            emitBlock(*s.tryBody);
            out_.seekp(-1, std::ios_base::cur);
            out_ << " catch (const std::exception& lg_exc) {\n";
            indent_++;
            // UWAGA (stage0): zmienna z `catch (err) { ... }` jest zawsze
            // wiazana jako `std::string` z tresci wyjatku (.what()), niezaleznie
            // od ewentualnej adnotacji typu w kodzie Legendary.
            line("std::string " + s.catchVarName + " = lg_exc.what();");
            for (auto& st : s.catchBody->stmts) emitStmt(*st);
            indent_--;
            out_ << ind() << "}\n";
            break;
        }
    }
}

void Codegen::emitEnum(const EnumDecl& e) {
    currentSourceFile_ = e.sourceFile;
    emitLineDirective(e.line);
    // Enum jest reprezentowany jako "tagged struct": wewnetrzny `enum class Kind`
    // + plaskie pola per-wariant (nazwane `<Wariant>_<indeks>`) + statyczne
    // fabryki o nazwie identycznej z wariantem (np. `Expr::Binary(l, op, r)`,
    // `EventKind::DiskFull()`). Dzieki temu `EnumName.Wariant(args)` w kodzie
    // Legendary mapuje sie 1:1 na wywolanie fabryki (patrz emitExpr::Member/Call).
    line("struct " + e.name + " {");
    indent_++;
    line("enum class Kind {");
    indent_++;
    for (size_t i = 0; i < e.variants.size(); ++i) {
        line(e.variants[i].name + (i + 1 < e.variants.size() ? "," : ""));
    }
    indent_--;
    line("} kind{};");

    auto& variantFields = enumVariantFields_[e.name];
    for (auto& v : e.variants) {
        std::vector<std::string> fieldNames;
        for (size_t i = 0; i < v.payloadTypes.size(); ++i) {
            std::string fname = v.name + "_" + std::to_string(i);
            line(cppType(v.payloadTypes[i]) + " " + fname + "{};");
            fieldNames.push_back(fname);
        }
        variantFields[v.name] = fieldNames;
    }

    if (!e.variants.empty()) line("");
    for (auto& v : e.variants) {
        std::vector<std::string> paramNames;
        for (size_t i = 0; i < v.payloadTypes.size(); ++i) paramNames.push_back("a" + std::to_string(i));

        out_ << ind() << "static " << e.name << " " << v.name << "(";
        for (size_t i = 0; i < v.payloadTypes.size(); ++i) {
            if (i) out_ << ", ";
            out_ << cppType(v.payloadTypes[i]) << " " << paramNames[i];
        }
        out_ << ") {\n";
        indent_++;
        line(e.name + " r{};");
        line("r.kind = Kind::" + v.name + ";");
        for (size_t i = 0; i < v.payloadTypes.size(); ++i) {
            line("r." + v.name + "_" + std::to_string(i) + " = " + paramNames[i] + ";");
        }
        line("return r;");
        indent_--;
        line("}");
    }

    indent_--;
    line("};");
    enumNames_.insert(e.name);
}

// ---------------- Deklaracje ----------------

void Codegen::emitImport(const ImportDecl& imp) {
    if (imp.isExtern) {
        line("#include \"" + imp.externHeader + "\"");
        // Zalozenie stage0: przestrzen nazw w naglowku C/C++ nazywa sie tak samo
        // jak alias (`as dnf` -> `namespace dnf { ... }` w naglowku). Udokumentowane w README.
        namespaceAliases_.insert(imp.alias);
        return;
    }

    // std::X -> lg::X (przestrzen nazw runtime'u Legendary)
    std::string ns = imp.path;
    if (ns.rfind("std::", 0) == 0) ns = "lg::" + ns.substr(5);
    else ns = "lg::" + ns; // najlepsza-mozliwa heurystyka dla modulow spoza std

    if (imp.isFrom) {
        for (auto& sym : imp.selective) {
            line("using " + ns + "::" + sym + ";");
        }
    } else {
        line("namespace " + imp.alias + " = " + ns + ";");
        namespaceAliases_.insert(imp.alias);
    }
}

void Codegen::emitParams(const std::vector<Param>& params) {
    out_ << "(";
    bool first = true;
    for (auto& p : params) {
        if (p.name == "self") continue; // `self` -> niestatyczna metoda skladowa, bez jawnego parametru
        if (!first) out_ << ", ";
        first = false;
        out_ << cppType(p.type) << " " << p.name;
    }
    out_ << ")";
}

// Metoda pochodzaca z `impl` - generowana JAKO CZLONEK struct (self -> this),
// dzieki czemu nie potrzeba deklaracji+definicji poza klasa.
void Codegen::emitMethod(const FnDecl& m) {
    currentSourceFile_ = m.sourceFile;
    emitLineDirective(m.line);
    for (auto& a : m.attrs)
        line("// @" + a.path + (a.hasArgs ? "(" + a.rawArgs + ") -- TODO(stage1): codegen atrybutu" : ""));
    std::string retType = m.returnType ? cppType(*m.returnType) : "void";
    out_ << ind() << retType << " " << m.name;
    emitParams(m.params);
    out_ << " ";
    emitBodyWithImplicitReturn(*m.body, m.returnType.has_value());
}

void Codegen::emitStruct(const StructDecl& s, const std::vector<const FnDecl*>& methods) {
    currentSourceFile_ = s.sourceFile;
    emitLineDirective(s.line);
    for (auto& a : s.attrs) line("// @" + a.path + (a.hasArgs ? "(" + a.rawArgs + ") -- TODO(stage1): codegen atrybutu" : ""));
    if (!s.typeParams.empty()) line(emitTypeParamsPrefix(s.typeParams));
    line("struct " + s.name + " {");
    indent_++;
    for (auto& f : s.fields) line(cppType(f.type) + " " + f.name + ";");
    if (!methods.empty()) line("");
    for (auto* m : methods) emitMethod(*m);
    indent_--;
    line("};");
}

void Codegen::emitFn(const FnDecl& fn) {
    currentSourceFile_ = fn.sourceFile;
    emitLineDirective(fn.line);
    for (auto& a : fn.attrs)
        line("// @" + a.path + (a.hasArgs ? "(" + a.rawArgs + ") -- TODO(stage1): codegen atrybutu" : ""));

    if (fn.name == "main") {
        // Specjalny wrapper: `fn main(args: List<string>)` -> prawdziwy `int main`.
        std::string argsName = fn.params.empty() ? "args" : fn.params[0].name;
        line("int main(int argc, char** argv) {");
        indent_++;
        line("std::vector<std::string> " + argsName + "(argv + 1, argv + argc);");
        for (auto& st : fn.body->stmts) emitStmt(*st);
        line("return 0;");
        indent_--;
        line("}");
        return;
    }

    if (!fn.typeParams.empty()) line(emitTypeParamsPrefix(fn.typeParams));
    std::string retType = fn.returnType ? cppType(*fn.returnType) : "void";
    out_ << ind() << retType << " " << fn.name;
    emitParams(fn.params);
    out_ << " ";
    emitBodyWithImplicitReturn(*fn.body, fn.returnType.has_value());
}

std::string Codegen::generate(const Program& prog) {
    out_.str("");
    out_.clear();
    namespaceAliases_.clear();
    enumNames_.clear();
    enumVariantFields_.clear();
    structFieldOrder_.clear();
    currentSourceFile_.clear();

    // Kolejnosc pol w deklaracji kazdego struct - potrzebne do poprawnego
    // (nie zaleznego od kolejnosci w literale) ukladania `Point{x:.., y:..}`.
    for (auto& s : prog.structs) {
        std::vector<std::string> names;
        for (auto& f : s.fields) names.push_back(f.name);
        structFieldOrder_[s.name] = names;
    }

    line("// Wygenerowano automatycznie przez lgc (Legendary Lang Compiler, stage0).");
    line("// Nie edytuj recznie - zmiany nadpisze kolejny build.");
    line("#include <cstdint>");
    line("#include <string>");
    line("#include <vector>");
    line("#include <unordered_map>");
    line("#include <optional>");
    line("#include <memory>");
    line("#include <sstream>");
    line("#include <iostream>");
    line("#include <thread>");
    line("#include <cstdlib>");
    line("#include <stdexcept>");
    line("#include \"lg_runtime.hpp\"");
    line("");

    for (auto& imp : prog.imports) emitImport(imp);
    line("");

    for (auto& e : prog.enums) { emitEnum(e); line(""); }

    // Grupujemy metody z `impl X { ... }` / `impl Trait for X { ... }` per nazwa struct,
    // zeby wygenerowac je JAKO CZLONKI struct (self -> this) - patrz emitMethod().
    // Uwaga (stage0): semantyka trait/interfejsu nie jest weryfikowana - metody z
    // `impl Trait for X` sa po prostu dopisywane do X tak jak zwykle `impl X`.
    std::map<std::string, std::vector<const FnDecl*>> methodsByStruct;
    for (auto& impl : prog.impls) {
        if (impl.traitType) {
            line("// impl " + impl.traitType->name + " for " + impl.targetType.name +
                 " -- metody dopisane do struct " + impl.targetType.name + " ponizej");
        }
        auto& vec = methodsByStruct[impl.targetType.name];
        for (auto& m : impl.methods) vec.push_back(&m);
    }
    line("");

    for (auto& s : prog.structs) {
        auto it = methodsByStruct.find(s.name);
        static const std::vector<const FnDecl*> empty;
        emitStruct(s, it != methodsByStruct.end() ? it->second : empty);
        line("");
    }

    for (auto& fn : prog.fns) { emitFn(fn); line(""); }

    return out_.str();
}

} // namespace lg
