#include "config_parser.hpp"
#include "lexer.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string readFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("nie mozna otworzyc pliku: " + path.string());
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Sciezka do wlasnej binarki (do zlokalizowania sasiadujacego `lgc` i runtime'u).
static fs::path selfExecutablePath() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) throw std::runtime_error("nie mozna ustalic sciezki do lpm (/proc/self/exe)");
    buf[len] = '\0';
    return fs::path(buf);
}

static fs::path findLgc() {
    fs::path candidate = selfExecutablePath().parent_path() / "lgc";
    if (fs::exists(candidate)) return candidate;
    throw std::runtime_error("nie znaleziono binarki `lgc` obok `lpm` (" + candidate.string() + ")");
}

static fs::path findRuntimeHeader() {
    fs::path base = selfExecutablePath().parent_path();
    std::vector<fs::path> candidates = {
        base / "lg_runtime.hpp",
        base / ".." / "runtime" / "lg_runtime.hpp",
        base / "share" / "legendary-lang" / "runtime" / "lg_runtime.hpp",
    };
    for (auto& c : candidates) if (fs::exists(c)) return fs::canonical(c);
    throw std::runtime_error("nie znaleziono lg_runtime.hpp (szukano obok binarki lpm)");
}

static lg::cfg::ConfigModel loadConfig(const fs::path& root) {
    fs::path configPath = root / "config.lg";
    if (!fs::exists(configPath)) {
        throw std::runtime_error("brak config.lg w " + root.string() +
                                  " (oczekiwana struktura: config.lg, src/main.lg lub src/lib.lg)");
    }
    std::string source = readFile(configPath);
    lg::Lexer lexer(source, configPath.string());
    auto tokens = lexer.tokenize();
    lg::cfg::ConfigParser parser(tokens, configPath.string());
    return parser.parse();
}

static int runCommand(const std::string& cmd) {
    std::cerr << "[lpm] $ " << cmd << "\n";
    return std::system(cmd.c_str());
}

static int cmdBuild(const fs::path& root, bool release) {
    auto model = loadConfig(root);

    fs::path srcDir = root / "src";
    fs::path entry;
    if (model.buildTarget == "bin") entry = srcDir / "main.lg";
    else entry = srcDir / "lib.lg"; // "lib" | "static-lib"

    if (!fs::exists(entry)) {
        throw std::runtime_error("brak wymaganego pliku wejsciowego: " + entry.string() +
                                  " (target \"" + model.buildTarget + "\" wymaga " +
                                  (model.buildTarget == "bin" ? "src/main.lg" : "src/lib.lg") + ")");
    }

    fs::path buildDir = root / "build";
    fs::path targetDir = buildDir / "target";
    fs::path pkgsDir = buildDir / "pkgs";
    fs::create_directories(targetDir);
    fs::create_directories(pkgsDir);

    // 1) lgc: .lg -> .cpp
    fs::path generatedCpp = targetDir / (entry.stem().string() + ".cpp");
    fs::path lgc = findLgc();
    std::ostringstream lgcCmd;
    lgcCmd << lgc.string() << " " << entry.string() << " --emit-cpp -o " << generatedCpp.string();
    if (runCommand(lgcCmd.str()) != 0) {
        throw std::runtime_error("lgc: kompilacja " + entry.string() + " nie powiodla sie");
    }

    // 2) skopiuj runtime obok wygenerowanego .cpp (include wzgledny "lg_runtime.hpp")
    fs::path runtimeHeader = findRuntimeHeader();
    fs::copy_file(runtimeHeader, targetDir / "lg_runtime.hpp", fs::copy_options::overwrite_existing);

    // TODO(stage1): rozwiazywanie dependencies.system przez libdnf5 (pobieranie/linkowanie
    // bibliotek C do build/pkgs/). stage0 zaklada, ze naglowki/biblioteki systemowe z
    // `import extern "..."` sa juz dostepne w sciezkach kompilatora.
    if (!model.systemDeps.empty()) {
        std::cerr << "[lpm] UWAGA: dependencies.system nie sa jeszcze automatycznie "
                     "rozwiazywane przez libdnf5 (stage1) - upewnij sie, ze naglowki/liby "
                     "sa juz dostepne w systemie.\n";
    }

    // 3) g++: .cpp -> binarka/lib
    std::ostringstream gppCmd;
    gppCmd << "g++ -std=c++20 -pthread ";
    gppCmd << (release || model.optimization == "release" ? "-O2 " : "-O0 -g ");
    for (auto& flag : model.compilerFlags) gppCmd << flag << " ";
    gppCmd << "-I" << targetDir.string() << " ";
    gppCmd << generatedCpp.string() << " ";

    fs::path outBinary = targetDir / model.projectName;
    if (model.buildTarget == "bin") {
        gppCmd << "-o " << outBinary.string();
    } else {
        gppCmd << "-c -o " << (targetDir / (model.projectName + ".o")).string();
    }

    if (runCommand(gppCmd.str()) != 0) {
        throw std::runtime_error("g++: kompilacja wygenerowanego C++ nie powiodla sie");
    }

    std::cerr << "[lpm] zbudowano: " << outBinary.string() << "\n";
    return 0;
}

static int cmdRun(const fs::path& root, const std::vector<std::string>& extraArgs) {
    auto model = loadConfig(root);
    int rc = cmdBuild(root, /*release=*/false);
    if (rc != 0) return rc;

    fs::path binary = root / "build" / "target" / model.projectName;
    std::ostringstream cmd;
    cmd << binary.string();
    for (auto& a : extraArgs) cmd << " " << a;
    return runCommand(cmd.str());
}

static int cmdClean(const fs::path& root) {
    fs::path buildDir = root / "build";
    std::error_code ec;
    fs::remove_all(buildDir, ec);
    if (ec) {
        std::cerr << "blad: nie mozna usunac " << buildDir.string() << ": " << ec.message() << "\n";
        return 1;
    }
    std::cerr << "[lpm] wyczyszczono: " << buildDir.string() << "\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uzycie: lpm <build|run|clean> [--release|--debug] [-- <argumenty>]\n";
        return 1;
    }

    std::string command = argv[1];
    bool release = true;
    std::vector<std::string> extra;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--release") release = true;
        else if (a == "--debug") release = false;
        else extra.push_back(a);
    }

    fs::path root = fs::current_path();

    try {
        if (command == "build") return cmdBuild(root, release);
        if (command == "run") return cmdRun(root, extra);
        if (command == "clean") return cmdClean(root);

        std::cerr << "nieznana komenda: \"" << command << "\" (dostepne: build, run, clean)\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "blad: " << e.what() << "\n";
        return 1;
    }
}
