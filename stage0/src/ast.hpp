#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lg::ast {

// ---------- Typy ----------
struct Type {
    std::string name;
    std::vector<Type> generics;
};

// ---------- Atrybuty (@nazwa(...)) ----------
// rawArgs = tekst pomiędzy nawiasami, przechowywany dosłownie (bez pełnego
// parsowania) — na etapie stage0 atrybuty są w większości pass-through/TODO.
struct Attribute {
    std::string path;     // np. "wayland::server" albo "hot_reload"
    std::string rawArgs;  // np. "bundles: [...], exclude: [...]"
    bool hasArgs = false;
};

// ---------- Wyrażenia ----------
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

enum class ExprKind {
    IntLit, FloatLit, StringLit, StringInterp, BoolLit, CharLit,
    Ident, Binary, Unary, Call, Member, Index, Assign, Spawn, StructLit, ArrayLit
};

struct Arg {
    std::string name; // puste jeśli pozycyjny
    ExprPtr value;
};

struct InterpPart {
    bool isExpr = false;
    std::string text;  // literalny fragment (gdy !isExpr)
    ExprPtr expr;       // sparsowane wyrazenie (gdy isExpr) - patrz Parser::extractInterpParts
};

struct Expr {
    ExprKind kind;
    int line = 0;

    // literały
    std::string strVal;
    long long intVal = 0;
    double floatVal = 0.0;
    bool boolVal = false;
    std::vector<InterpPart> interpParts;

    // identyfikator
    std::string name;
    std::optional<Type> genericHint; // dla `Channel<i32>(...)` itp. - patrz Parser::parsePrimary

    // binary / assign
    std::string op;
    ExprPtr lhs, rhs;

    // unary
    ExprPtr operand;

    // call
    ExprPtr callee;
    std::vector<Arg> args;

    // member: object.member
    ExprPtr object;
    std::string member;

    // index: object[indexExpr]
    ExprPtr indexExpr;

    // array literal: [e1, e2, ...]
    std::vector<ExprPtr> elements;

    // spawn <call-expr>
    ExprPtr spawnCall;
};

// ---------- Instrukcje ----------
struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

enum class StmtKind {
    Let, Return, ExprStmt, If, While, For, Block,
    Match, On, Profile, Isolate, ShellExec, Try, Throw
};

struct IfBranch {
    ExprPtr cond; // nullptr = branch "else"
    StmtPtr body; // Block
};

struct MatchArm {
    bool isWildcard = false;
    bool isRange = false;
    ExprPtr pattern;   // wartość lub start zakresu
    ExprPtr rangeEnd;  // koniec zakresu (dla `a..=b`)
    StmtPtr body;      // Block
};

struct RegionStmt {
    std::string name;
    StmtPtr body; // Block
};

struct Stmt {
    StmtKind kind;
    int line = 0;

    // let
    bool isMut = false;
    std::string varName;
    std::optional<Type> declType;
    ExprPtr initExpr;

    // return / exprStmt
    ExprPtr expr;

    // if
    std::vector<IfBranch> branches;

    // while
    ExprPtr whileCond;
    StmtPtr whileBody;

    // for
    std::string forVar;
    ExprPtr forIter;
    StmtPtr forBody;

    // block
    std::vector<StmtPtr> stmts;

    // match
    ExprPtr matchSubject;
    std::vector<MatchArm> matchArms;

    // on (reaktywne)
    ExprPtr onCond;
    StmtPtr onBody;

    // profile
    std::string profileLabel;
    std::vector<RegionStmt> regions;

    // isolate(...)
    std::vector<Arg> isolateArgs;
    StmtPtr isolateBody;

    // $ shell $  (jako samodzielna instrukcja)
    std::vector<InterpPart> shellParts;

    // try { ... } catch (err[: Type]) { ... }
    StmtPtr tryBody;
    std::string catchVarName;
    std::optional<Type> catchType; // parsowany, ale stage0 zawsze wiaze .what() jako string
    StmtPtr catchBody;
    // throw <expr>  (reuzywa pola `expr`)
};

// ---------- Deklaracje najwyższego poziomu ----------
struct Field {
    std::string name;
    Type type;
};

struct Param {
    std::string name;
    Type type;
    bool isMut = false;
};

struct FnDecl {
    std::vector<Attribute> attrs;
    std::string name;
    std::vector<std::string> typeParams; // `fn identity<T>(...)` - generyki
    std::vector<Param> params;
    std::optional<Type> returnType;
    StmtPtr body; // Block
    int line = 0;
    std::string sourceFile; // do `#line` w wygenerowanym C++ - patrz main.cpp (stampSourceFile)
};

struct StructDecl {
    std::vector<Attribute> attrs;
    std::string name;
    std::vector<std::string> typeParams; // `struct Pair<A, B>` - generyki
    std::vector<Field> fields;
    int line = 0;
    std::string sourceFile;
};

// Wariant enuma - moze miec powiazane dane (sum type), np.
// `Binary(Box<Expr>, string, Box<Expr>)`. Pusty payloadTypes = zwykla wartosc C-enum-like.
struct EnumVariant {
    std::string name;
    std::vector<Type> payloadTypes;
};

struct EnumDecl {
    std::string name;
    std::vector<EnumVariant> variants;
    int line = 0;
    std::string sourceFile;
};

struct ImplDecl {
    Type targetType;
    std::optional<Type> traitType; // `impl Trait for Target`
    std::vector<FnDecl> methods;
    int line = 0;
    std::string sourceFile;
};

struct ImportDecl {
    bool isExtern = false;
    std::string externHeader;       // dla `import extern "..."`
    std::string path;               // np. "std::io"
    std::string alias;              // `as X` (dla extern: nazwa namespace'u w kodzie)
    bool isFrom = false;            // `from X import { A, B }`
    std::vector<std::string> selective;
};

struct Program {
    std::vector<ImportDecl> imports;
    std::vector<StructDecl> structs;
    std::vector<EnumDecl> enums;
    std::vector<ImplDecl> impls;
    std::vector<FnDecl> fns;
};

} // namespace lg::ast
