#pragma once
#include <map>
#include <string>
#include <vector>

namespace lg::cfg {

// Generyczna wartosc w config.lg: string, tablica lub zagniezdzony obiekt.
// (config.lg nie ma liczb/boolow jako osobnych typow - wszystko co nie jest
// tablica/obiektem jest stringiem, tak jak w podanych przykladach).
struct Value {
    enum class Kind { Str, Arr, Obj } kind = Kind::Str;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    const Value* get(const std::string& key) const {
        for (auto& [k, v] : obj) if (k == key) return &v;
        return nullptr;
    }
};

struct SystemDep {
    std::string name;
    std::string version; // moze byc puste
    std::string link;    // "dynamic" | "static" (domyslnie dynamic)
};

// Model po walidacji - odpowiednik "Cargo.toml" dla Legendary Lang.
struct ConfigModel {
    // [project]
    std::string projectName;
    std::string projectVersion;
    std::vector<std::string> authors;
    std::string license;

    // [dependencies]
    std::map<std::string, std::string> langDeps; // "std::io" -> "^1.2.0"
    std::vector<SystemDep> systemDeps;

    // [build]
    std::string buildTarget = "bin"; // "bin" | "lib" | "static-lib"
    std::string langVersion;         // wersja SKLADNI Legendary Lang
    std::string optimization = "release";
    std::vector<std::string> compilerFlags;

    // [tasks] - dowolne nazwy zadan
    std::map<std::string, std::string> tasks;
};

} // namespace lg::cfg
