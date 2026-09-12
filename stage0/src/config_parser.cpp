#include "config_parser.hpp"
#include "lexer.hpp"
#include <algorithm>
#include <stdexcept>

namespace lg::cfg {

const Token& ConfigParser::cur() const { return toks_[pos_]; }
const Token& ConfigParser::peek(size_t off) const {
    size_t i = pos_ + off;
    return i < toks_.size() ? toks_[i] : toks_.back();
}
const Token& ConfigParser::advance() {
    const Token& t = toks_[pos_];
    if (pos_ + 1 < toks_.size()) pos_++;
    return t;
}
bool ConfigParser::check(TokenKind k) const { return cur().kind == k; }
bool ConfigParser::match(TokenKind k) { if (check(k)) { advance(); return true; } return false; }
const Token& ConfigParser::expect(TokenKind k, const std::string& what) {
    if (!check(k)) error("oczekiwano '" + what + "', otrzymano \"" + cur().text + "\"");
    return advance();
}
void ConfigParser::error(const std::string& msg) const {
    throw std::runtime_error(filename_ + ":" + std::to_string(cur().line) + ":" +
                              std::to_string(cur().col) + ": " + msg);
}

ConfigParser::ConfigParser(std::vector<Token> tokens, std::string filename)
    : filename_(std::move(filename)) {
    toks_.reserve(tokens.size());
    for (auto& t : tokens) {
        switch (t.kind) {
            case TokenKind::NewLine:
            case TokenKind::LineComment:
            case TokenKind::DocComment:
            case TokenKind::BlockComment:
                continue;
            default:
                toks_.push_back(t);
        }
    }
    if (toks_.empty() || toks_.back().kind != TokenKind::Eof) {
        toks_.push_back(Token{TokenKind::Eof, "", 0, 0});
    }
}

Value ConfigParser::parseArrayBody() {
    Value v; v.kind = Value::Kind::Arr;
    while (!check(TokenKind::RBracket)) {
        v.arr.push_back(parseValue());
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBracket, "]");
    return v;
}

Value ConfigParser::parseObjectBody() {
    Value v; v.kind = Value::Kind::Obj;
    while (!check(TokenKind::RBrace)) {
        std::string key;
        if (check(TokenKind::StringLiteral) || check(TokenKind::Identifier)) key = advance().text;
        else error("oczekiwano klucza (string lub identyfikator)");
        expect(TokenKind::Colon, ":");
        Value val = parseValue();
        v.obj.emplace_back(key, std::move(val));
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "}");
    return v;
}

Value ConfigParser::parseValue() {
    if (match(TokenKind::LBracket)) return parseArrayBody();
    if (match(TokenKind::LBrace)) return parseObjectBody();
    if (check(TokenKind::StringLiteral) || check(TokenKind::Identifier)) {
        Value v; v.kind = Value::Kind::Str; v.str = advance().text;
        return v;
    }
    error("oczekiwano wartosci (string, [tablica] lub {obiekt})");
}

static void requireKind(const std::string& where, const Value& v, Value::Kind k) {
    if (v.kind != k) throw std::runtime_error(where + ": nieprawidlowy typ wartosci");
}

void ConfigParser::validateProject(const Value& v, ConfigModel& out) {
    for (auto& [k, val] : v.obj) {
        if (k == "name") { requireKind("project.name", val, Value::Kind::Str); out.projectName = val.str; }
        else if (k == "version") { requireKind("project.version", val, Value::Kind::Str); out.projectVersion = val.str; }
        else if (k == "authors") {
            requireKind("project.authors", val, Value::Kind::Arr);
            for (auto& item : val.arr) { requireKind("project.authors[]", item, Value::Kind::Str); out.authors.push_back(item.str); }
        } else if (k == "license") { requireKind("project.license", val, Value::Kind::Str); out.license = val.str; }
        else error("nieznane pole w sekcji `project`: \"" + k + "\"");
    }
    if (out.projectName.empty()) error("`project.name` jest wymagane");
    if (out.projectVersion.empty()) error("`project.version` jest wymagane");
}

void ConfigParser::validateDependencies(const Value& v, ConfigModel& out) {
    for (auto& [k, val] : v.obj) {
        if (k == "lang") {
            requireKind("dependencies.lang", val, Value::Kind::Obj);
            for (auto& [dep, depVal] : val.obj) {
                requireKind("dependencies.lang." + dep, depVal, Value::Kind::Str);
                out.langDeps[dep] = depVal.str;
            }
        } else if (k == "system") {
            requireKind("dependencies.system", val, Value::Kind::Obj);
            for (auto& [dep, depVal] : val.obj) {
                requireKind("dependencies.system." + dep, depVal, Value::Kind::Obj);
                SystemDep sd; sd.name = dep; sd.link = "dynamic";
                for (auto& [subk, subv] : depVal.obj) {
                    if (subk == "version") { requireKind("dependencies.system." + dep + ".version", subv, Value::Kind::Str); sd.version = subv.str; }
                    else if (subk == "link") { requireKind("dependencies.system." + dep + ".link", subv, Value::Kind::Str); sd.link = subv.str; }
                    else error("nieznane pole w dependencies.system." + dep + ": \"" + subk + "\"");
                }
                out.systemDeps.push_back(std::move(sd));
            }
        } else {
            error("nieznane pole w sekcji `dependencies`: \"" + k + "\" (dozwolone: lang, system)");
        }
    }
}

void ConfigParser::validateBuild(const Value& v, ConfigModel& out) {
    static const std::vector<std::string> validTargets = {"bin", "lib", "static-lib"};
    for (auto& [k, val] : v.obj) {
        if (k == "target") {
            requireKind("build.target", val, Value::Kind::Str);
            out.buildTarget = val.str;
            if (std::find(validTargets.begin(), validTargets.end(), val.str) == validTargets.end())
                error("build.target musi byc jednym z: \"bin\", \"lib\", \"static-lib\" (otrzymano \"" + val.str + "\")");
        } else if (k == "version") { requireKind("build.version", val, Value::Kind::Str); out.langVersion = val.str; }
        else if (k == "optimization") { requireKind("build.optimization", val, Value::Kind::Str); out.optimization = val.str; }
        else if (k == "compiler_flags") {
            requireKind("build.compiler_flags", val, Value::Kind::Arr);
            for (auto& item : val.arr) { requireKind("build.compiler_flags[]", item, Value::Kind::Str); out.compilerFlags.push_back(item.str); }
        } else {
            error("nieznane pole w sekcji `build`: \"" + k + "\"");
        }
    }
}

void ConfigParser::validateTasks(const Value& v, ConfigModel& out) {
    for (auto& [k, val] : v.obj) {
        requireKind("tasks." + k, val, Value::Kind::Str);
        out.tasks[k] = val.str;
    }
}

ConfigModel ConfigParser::parse() {
    ConfigModel model;
    bool sawProject = false, sawBuild = false;

    while (!check(TokenKind::Eof)) {
        std::string blockName = expect(TokenKind::Identifier, "nazwa sekcji (project/dependencies/build/tasks)").text;
        expect(TokenKind::LBrace, "{");
        Value body = parseObjectBody();

        if (blockName == "project") {
            if (sawProject) error("zduplikowana sekcja `project`");
            sawProject = true;
            validateProject(body, model);
        } else if (blockName == "dependencies") {
            validateDependencies(body, model);
        } else if (blockName == "build") {
            if (sawBuild) error("zduplikowana sekcja `build`");
            sawBuild = true;
            validateBuild(body, model);
        } else if (blockName == "tasks") {
            validateTasks(body, model);
        } else {
            error("nieznana sekcja najwyzszego poziomu w config.lg: \"" + blockName +
                  "\" (dozwolone: project, dependencies, build, tasks)");
        }
    }

    if (!sawProject) error("config.lg musi zawierac sekcje `project { ... }`");
    if (!sawBuild) error("config.lg musi zawierac sekcje `build { ... }`");
    return model;
}

} // namespace lg::cfg
