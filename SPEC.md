# Legendary Lang — Specyfikacja Języka (v0.1-draft)

> Język programowania natywnie zintegrowany z **LegendaryOS** (dystrybucja bazująca na Fedorze).
> Pliki źródłowe: `.lg`. Transpiler: `lgc`. Menadżer projektu/pakietów: `lpm`.
> Backend: transpilacja do **C++20**, kompilacja przez GCC/Clang.

---

## 0. Filozofia

- **Statyczne typowanie**, wnioskowanie typów (`let x = 5` → `i32`), zero `null` (opcjonalność przez `Option<T>`).
- **Zero-cost abstractions** — jeśli czegoś nie używasz, nie płacisz za to w runtime (dziedziczone podejście z C++).
- **System first** — język "czuje" system: reaktywne zmienne systemowe, sandboxing, live-reload demonów.
- **Jedna poprawna droga** — sztywna struktura projektu i konfiguracji (patrz sekcja 6). Inna struktura/nazwa pliku = błąd kompilacji/lpm.
- Wersjonowanie składni w `config.lg` (`build.version`) — różne wersje Legendary Lang mogą mieć różną gramatykę.

---

## 1. Podstawy składni

### 1.1 Komentarze

| Typ | Składnia | Zastosowanie |
|---|---|---|
| Zwykły | `-- treść` | komentarz jednoliniowy |
| Dokumentacyjny | `--- treść` | generowanie dokumentacji (`lpm doc`) |
| Blokowy | `--( treść na wielu liniach )--` | dłuższe opisy / wyłączanie fragmentu kodu |

### 1.2 Zmienne i typy

```lg
let x = 5              -- i32 (inferencja)
let y: f64 = 3.14
let mut counter = 0    -- mutowalność jawna przez `mut`
const MAX: i32 = 100

-- Typy podstawowe:
-- i8 i16 i32 i64 isize
-- u8 u16 u32 u64 usize
-- f32 f64
-- bool  string  char
-- List<T>  Map<K,V>  Option<T>  Result<T,E>
```

### 1.3 Funkcje

```lg
fn add(a: i32, b: i32) -> i32 {
    return a + b
}

-- Ostatnie wyrażenie bez `;`/`return` jest wartością zwracaną (jak w Rust)
fn square(x: i32) -> i32 {
    x * x
}
```

### 1.4 Struktury i implementacje

```lg
struct Point {
    x: f64,
    y: f64
}

impl Point {
    fn distance(self, other: Point) -> f64 {
        math.sqrt((self.x - other.x) ** 2 + (self.y - other.y) ** 2)
    }
}
```

### 1.5 Sterowanie przepływem

```lg
if condition {
    ...
} else if other {
    ...
} else {
    ...
}

for item in list {
    ...
}

while condition {
    ...
}

match value {
    0 => io.msg("zero"),
    1..=9 => io.msg("cyfra"),
    _ => io.msg("inne")
}
```

---

## 2. Wyjście standardowe

Legendary Lang **nie ma** `print`/`println`/`puts`. Jedyną formą wyjścia w `std::io` jest:

```lg
io.msg("Tekst")
io.msg("Wartość: {value}")   -- interpolacja stringów w {}
io.err("Błąd krytyczny")     -- odpowiednik stderr
```

---

## 3. System importów

```lg
-- Moduł standardowy
import std::io
import std::cli

-- Import zewnętrznego nagłówka C/C++ jako namespace
import extern "libdnf5/libdnf5.h" as dnf

-- Import selektywny (tylko wybrane symbole)
from std::wayland import { Display, WlSurface, Resource }
```

Reguły:
- `import std::X` — cały moduł, dostęp przez `X.symbol`.
- `import extern "<header>" as Alias` — bindowanie C/C++, wymaga wpisu w `config.lg -> dependencies.system`.
- `from module import { A, B }` — import selektywny, bez prefiksu przy użyciu.

---

## 4. Atrybuty (`@attribute`)

Atrybuty modyfikują deklaracje w sposób deklaratywny — kompilator generuje boilerplate C++ pod spodem.

```lg
@wayland::server(
    bundles: [wl.Bundles.DesktopStandard, wl.Bundles.InputMethods],
    exclude: [wl.Protocol.XdgShell]
)
struct CompositorState { ... }

@hot_reload
fn handle_system_event(event: Event) { ... }
```

---

## 5. Funkcje "Game Changer"

### 5.1 Live Reloading (`@hot_reload`)

Funkcje oznaczone `@hot_reload` są kompilowane jako oddzielne jednostki `.so`, ładowane przez wskaźnik funkcji w wygenerowanym C++. Zmiana pliku `.lg` → `lgc watch` przebudowuje tylko dany symbol i podmienia wskaźnik w działającym procesie (bez restartu demona).

```lg
@hot_reload
fn handle_system_event(event: Event) {
    if event.kind == EventKind.DiskFull {
        sys.clean_tmp()
    }
}
```

### 5.2 Profiler zero-overhead (`profile { region ... }`)

```lg
profile "Przetwarzanie pakietów RPM" {
    region TempArena {
        lpm.parse_all_headers()
    }
}
```
W trybie `debug` generuje liczniki alokacji/czasu (RAII w C++), w `release` blok kompiluje się do zwykłego wywołania (zero overhead).

### 5.3 Lekka współbieżność — `spawn` + `Channel<T>`

```lg
fn fetch_mirror(url: string, out_chan: Channel<Package>) {
    let pkg = http.download(url)
    out_chan.send(pkg)
}

fn parallel_download() {
    let chan = Channel<Package>()
    spawn fetch_mirror("https://mirror1.legendary.org/lpm", chan)
    spawn fetch_mirror("https://mirror2.legendary.org/lpm", chan)
    let first_pkg = chan.receive()
}
```
`spawn` → transpilowane do `std::jthread` (lub coroutine C++20, jeśli funkcja jest oznaczona `@lightweight`). `Channel<T>` → cienka warstwa nad `std::queue` + `std::condition_variable`.

### 5.4 Zero-Cost Sandbox — `isolate { ... }`

```lg
fn run_untrusted_script(script_path: string) {
    isolate(network: false, read_only_fs: true) {
        $ bash {script_path} $
    }
}
```
`isolate(...)` generuje kod oparty o Linux `namespaces` + `cgroups v2` (clone/unshare, mount --bind read-only, brak `CLONE_NEWNET` interfejsu). Blok `$ ... $` to inline-shell z interpolacją zmiennych `{}`.

### 5.5 Reaktywne zmienne systemowe (`on`)

```lg
let battery = sys::power::battery_level   -- reaktywny strumień (Signal<T>)

on battery < 15 {
    sys::notify("Niski poziom baterii!", "Włączono tryb oszczędzania.")
}
```
`sys::*` zmienne to `Signal<T>` — pod spodem: wątek nasłuchujący `udev`/`sysfs`/`dbus`, `on { }` rejestruje callback reagujący na zmianę wartości (debounced).

---

## 6. Struktura projektu

Struktura jest **sztywna** — inna nazwa/lokalizacja pliku = błąd `lpm`.

```
mój-projekt/
├── config.lg          -- odpowiednik Cargo.toml (WYMAGANY)
├── build.lg           -- odpowiednik build.rs (OPCJONALNY)
├── src/
│   ├── main.lg        -- punkt wejścia dla target: "bin"
│   └── lib.lg         -- punkt wejścia dla target: "lib" / "static-lib"
└── build/             -- generowane, nie commitować
    ├── target/
    │   ├── <cache pośrednie: .cpp wygenerowane, .o>
    │   └── <nazwa-binarki>
    └── pkgs/           -- pobrane/zbudowane zależności systemowe (mini-sandboxy per zależność)
```

---

## 7. `config.lg` — pełna referencja

```lg
-- Konfiguracja projektu Legendary Lang
project {
    name: "legendary-lang",
    version: "0.1.0",
    authors: ["LegendaryOS Team <legendaryos.linux.system@gmail.com>"],
    license: "Apache-2.0"
}

dependencies {
    lang: {
        "std::io": "^1.2.0",
        "std::libdnf5": "latest"
    },

    -- Bezpośrednie powiązania z bibliotekami systemowymi C/C++
    system: {
        "libdnf5":         { version: ">= 5.1.0", link: "dynamic" },
        "wayland-server":  { version: ">= 1.22",  link: "dynamic" },
        "pixman-1":        { link: "static" }   -- lpm sam pobiera/buduje, nie zakłada obecności w systemie
    }
}

build {
    target: "bin",              -- "bin" | "lib" | "static-lib"
    version: "0.1",             -- wersja SKŁADNI Legendary Lang używana w projekcie
    optimization: "release",    -- "debug" | "release" | "size"
    compiler_flags: ["-O3", "-march=native", "-Wall"]
    -- UWAGA: nie deklarujemy standardu C++ — zawsze C++20, ustalone przez lgc.
}

tasks {
    "run":     "build/target/legendary-lang",
    "clean":   "rm -rf build",
    "package": "lpm package --release"
}
```

Sekcje inne niż `project`, `dependencies`, `build`, `tasks` → błąd parsowania.

---

## 8. `lpm` — Legendary Package Manager

Podwójna rola:
1. **Menadżer pakietów/projektu Legendary Lang** (jak Cargo) — `dependencies.lang`.
2. **Menadżer pakietów systemowych dla LegendaryOS/Fedory** poprzez `libdnf5` — `dependencies.system`.

### Komendy

```
lpm build [--release|--debug]   -- szuka src/, config.lg, opcjonalnie build.lg
lpm run                          -- build + uruchomienie binarki z build/target/
lpm clean                        -- czyści build/
lpm add <pakiet> [--system]      -- dodaje zależność do config.lg
lpm package                      -- pakietowanie (np. do .rpm dla LegendaryOS)
lpm doc                          -- generuje dokumentację z komentarzy `---`
```

`lpm build`:
1. Waliduje strukturę katalogu (sekcja 6) — brak `src/main.lg`/`src/lib.lg` lub `config.lg` = błąd.
2. Rozwiązuje `dependencies.system` przez `libdnf5` (instalacja/link systemowych .so) oraz pobiera/buduje zależności bez systemowego odpowiednika do `build/pkgs/`.
3. Wywołuje `lgc` na plikach `.lg`, generując pośredni C++20 w `build/target/`.
4. Kompiluje wygenerowany kod przez GCC/Clang z `compiler_flags` z `config.lg`.
5. Wynikowa binarka/lib ląduje w `build/target/<nazwa>`.

---

## 9. `lgc` — transpiler

```
lgc <plik.lg> [--emit-cpp] [--release|--debug] [-o <output>]
```
- Wejście: pojedynczy plik `.lg` lub katalog `src/` (przez `lpm build`, który orkiestruje wywołania `lgc`).
- Wyjście: plik(i) `.cpp`/`.hpp` zgodne z C++20, następnie kompilacja natywnym kompilatorem.
- `lgc` w wersji **stage0** jest napisany w C++ (patrz `/stage0`). Docelowo (**stage1**) `lpm` i `lgc` będą w 100% napisane w samym Legendary Lang (self-hosting).

---

## 10. Wbudowane biblioteki standardowe (`std::*`)

| Moduł | Opis |
|---|---|
| `std::io` | `msg`, `err`, operacje na plikach |
| `std::cli` | budowanie CLI (`App`, argumenty, subkomendy) |
| `std::libdnf5` | binding do libdnf5 — zarządzanie pakietami RPM/DNF |
| `std::wayland` / `std::wayland::desktop` | serwer Wayland, bundling protokołów |
| `std::sys` | `sys::power`, `sys::notify`, reaktywne `Signal<T>` |
| `std::net` / `std::http` | pobieranie plików, mirror-fetching |
| `std::fs` | operacje na systemie plików |
| `std::collections` | `List<T>`, `Map<K,V>`, `Channel<T>` |
| `std::math` | funkcje matematyczne |

---

## 11. Roadmapa bootstrapowania

- **stage0** (obecny etap): `lgc`/`lpm` napisane w C++ (`stage0/`, `CMakeLists.txt`). Cel: transpiler wystarczająco kompletny, by skompilować podstawowy `lpm` napisany w Legendary Lang.
- **stage1**: `lpm` w 100% napisany w `.lg`, kompilowany przez `stage0/lgc`. Testy regresyjne + fuzzing składni.
- **stage2 (self-hosting)**: `lgc` przepisany na Legendary Lang, kompilowany przez `stage1`. Porzucenie zależności od ręcznie pisanego C++ frontendu — od tego momentu C++ pozostaje wyłącznie jako *backend/target* transpilacji, nie jako implementacja kompilatora.
