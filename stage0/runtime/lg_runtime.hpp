#pragma once
// lg_runtime.hpp — minimalny runtime dla kodu wygenerowanego przez `lgc` (stage0).
// Odpowiada modulom `std::*` z Legendary Lang. Dolaczany automatycznie przez
// wygenerowany plik .cpp (patrz Codegen::generate()).

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <variant>

#if defined(__linux__)
#include <cerrno>
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lg {

// ---------------- std::io ----------------
namespace io {
    inline void msg(const std::string& text) { std::cout << text << "\n"; }
    inline void err(const std::string& text) { std::cerr << text << "\n"; }
}

// ---------------- std::cli ----------------
namespace cli {
    struct App {
        std::string name;
        std::string version;
        App(std::string n = "", std::string v = "") : name(std::move(n)), version(std::move(v)) {}
    };
}

// ---------------- std::collections::Channel<T> ----------------
// Prosty, watkowo-bezpieczny kanal MPSC uzywany przez `spawn` / `Channel<T>`.
// Kopie `Channel<T>` wspoldziela ten sam bufor (semantyka "uchwytu", jak
// nadawca/odbiorca tego samego kanalu) - stad wewnetrzny shared_ptr.
template <typename T>
class Channel {
public:
    Channel() : state_(std::make_shared<State>()) {}

    void send(T value) {
        {
            std::lock_guard<std::mutex> lock(state_->mtx);
            state_->queue.push(std::move(value));
        }
        state_->cv.notify_one();
    }

    T receive() {
        std::unique_lock<std::mutex> lock(state_->mtx);
        state_->cv.wait(lock, [this] { return !state_->queue.empty(); });
        T value = std::move(state_->queue.front());
        state_->queue.pop();
        return value;
    }

    std::optional<T> try_receive() {
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (state_->queue.empty()) return std::nullopt;
        T value = std::move(state_->queue.front());
        state_->queue.pop();
        return value;
    }

private:
    struct State {
        std::mutex mtx;
        std::condition_variable cv;
        std::queue<T> queue;
    };
    std::shared_ptr<State> state_;
};

// ---------------- std::Result<T, E> ----------------
template <typename T, typename E>
class Result {
public:
    static Result Ok(T value) { Result r; r.data_ = std::move(value); return r; }
    static Result Err(E error) { Result r; r.data_ = std::move(error); return r; }

    bool is_ok() const { return std::holds_alternative<T>(data_); }
    bool is_err() const { return std::holds_alternative<E>(data_); }

    const T& unwrap() const { return std::get<T>(data_); }
    const E& unwrap_err() const { return std::get<E>(data_); }

private:
    std::variant<T, E> data_;
};

// ---------------- std::sys (placeholder, stage1) ----------------
namespace sys {
    inline void notify(const std::string& title, const std::string& body) {
        // TODO(stage1): prawdziwa integracja z dbus/notify-send na LegendaryOS.
        std::cout << "[notify] " << title << ": " << body << "\n";
    }
}

// ---------------- std::fs ----------------
// Niezbedne do napisania `lpm` w samym Legendary Lang (stage1).
namespace fs {
    inline bool exists(const std::string& path) {
        return std::filesystem::exists(path);
    }

    inline void create_dirs(const std::string& path) {
        std::filesystem::create_directories(path);
    }

    inline std::string read_file(const std::string& path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("nie mozna otworzyc pliku: " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    inline void write_file(const std::string& path, const std::string& content) {
        std::ofstream f(path);
        if (!f) throw std::runtime_error("nie mozna zapisac pliku: " + path);
        f << content;
    }

    inline void copy_file(const std::string& src, const std::string& dst) {
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    }

    inline void remove_all(const std::string& path) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        if (ec) throw std::runtime_error("nie mozna usunac: " + path + " (" + ec.message() + ")");
    }

    // Laczy dwa segmenty sciezki (odpowiednik `Path::join`).
    inline std::string join(const std::string& a, const std::string& b) {
        return (std::filesystem::path(a) / b).string();
    }

    inline std::string parent_dir(const std::string& path) {
        return std::filesystem::path(path).parent_path().string();
    }

    inline std::string file_stem(const std::string& path) {
        return std::filesystem::path(path).stem().string();
    }
}

// ---------------- std::process ----------------
namespace process {
    // Uruchamia polecenie powloki synchronicznie, zwraca kod wyjscia.
    // TODO(stage1): `isolate(...)` powinno kierowac tutaj przez namespaces/cgroups
    // zamiast bezposredniego std::system.
    inline int32_t run(const std::string& cmd) {
        return std::system(cmd.c_str());
    }
}

// ---------------- std::env ----------------
namespace env {
    inline std::string current_dir() {
        return std::filesystem::current_path().string();
    }

    // Sciezka do wlasnej binarki (dziala na Linuksie/LegendaryOS - stad
    // uzalegnienie od /proc/self/exe zamiast przenosnego rozwiazania).
    inline std::string self_exe() {
#if defined(__linux__)
        char buf[4096];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len <= 0) throw std::runtime_error("nie mozna ustalic sciezki do wlasnej binarki");
        buf[len] = '\0';
        return std::string(buf);
#else
        throw std::runtime_error("env::self_exe() zaimplementowane tylko na Linuksie (LegendaryOS)");
#endif
    }
}

// ---------------- isolate { } -> lg::sandbox::run ----------------
// Prawdziwa (choc podstawowa) izolacja polecen `$ ... $` z bloku `isolate(...)`:
// - `network: false`  -> nowy namespace sieciowy (CLONE_NEWNET) - brak interfejsow poza `lo`
// - `read_only_fs: true` -> biezacy katalog roboczy przemontowany read-only (bind + remount-ro)
//
// UWAGA: to nie jest kompletny sandbox klasy bubblewrap/firejail (brak np. cgroups v2,
// seccomp, pelnego przemontowania calego systemu plikow, mapowania uid/gid dla
// unprivileged user namespaces). Wymaga uprawnien do tworzenia namespaces
// (CAP_SYS_ADMIN lub wlaczonych unprivileged user namespaces w jadrze). Jesli
// nie jest to mozliwe (np. w kontenerze budujacym bez tych uprawnien), funkcja
// wypisuje OSTRZEZENIE na stderr i uruchamia polecenie BEZ izolacji zamiast
// cichego zafailowania calego builda - swiadomy kompromis stage0.
namespace sandbox {
    inline int32_t run(const std::string& cmd, bool allow_network, bool read_only_fs) {
#if defined(__linux__)
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "[sandbox] fork() nie powiodl sie (" << std::strerror(errno)
                      << ") - uruchamiam bez izolacji\n";
            return std::system(cmd.c_str());
        }
        if (pid == 0) {
            int unshare_flags = 0;
            if (!allow_network) unshare_flags |= CLONE_NEWNET;
            if (read_only_fs) unshare_flags |= CLONE_NEWNS;

            bool ns_ok = true;
            if (unshare_flags != 0 && unshare(unshare_flags) != 0) {
                std::cerr << "[sandbox] unshare() nie powiodlo sie (" << std::strerror(errno)
                          << ") - brak uprawnien do namespaces? kontynuuje BEZ pelnej izolacji.\n";
                ns_ok = false;
            }
            if (ns_ok && read_only_fs) {
                std::string cwd = std::filesystem::current_path().string();
                if (mount(cwd.c_str(), cwd.c_str(), nullptr, MS_BIND, nullptr) != 0 ||
                    mount(cwd.c_str(), cwd.c_str(), nullptr, MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr) != 0) {
                    std::cerr << "[sandbox] przemontowanie read-only nie powiodlo sie ("
                              << std::strerror(errno) << ") - katalog roboczy pozostaje zapisywalny.\n";
                }
            }
            execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
            std::cerr << "[sandbox] execl nie powiodlo sie (" << std::strerror(errno) << ")\n";
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
        std::cerr << "[sandbox] izolacja dostepna tylko na Linuksie (LegendaryOS) - uruchamiam bez izolacji\n";
        (void)allow_network; (void)read_only_fs;
        return std::system(cmd.c_str());
#endif
    }
}

// ---------------- on { } -> lg::reactive::on_condition ----------------
// Watek w tle odpytuje `cond` co `poll_ms` milisekund i wywoluje `callback`
// przy KAZDYM przejsciu false -> true (edge-triggered - jak "gdy bateria
// SPADNIE ponizej 15%", nie "podczas gdy bateria jest ponizej 15%").
//
// UWAGA (stage0): to prawdziwy POLLING w tle (realna poprawa wzgledem
// jednorazowego sprawdzenia), ale NIE jest to push-based subskrypcja
// sysfs/udev/dbus - integracja ze sprzetem/systemem to material na stage1.
// Jesli `cond` czyta ZAJMOWANA PRZEZ WARTOSC (kopie) zmienna lokalna z `let`,
// polling nigdy nie zobaczy zmiany - `cond` powinien wywolywac funkcje/zapytania
// pobierajace swieze dane (np. wlasna funkcje odczytujaca plik/sysfs).
namespace reactive {
    inline void on_condition(std::function<bool()> cond, std::function<void()> callback, int poll_ms = 200) {
        std::thread([cond, callback, poll_ms] {
            bool was_true = false;
            while (true) {
                bool now_true = false;
                try { now_true = cond(); } catch (...) { now_true = false; }
                if (now_true && !was_true) callback();
                was_true = now_true;
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
            }
        }).detach();
    }
}

} // namespace lg
