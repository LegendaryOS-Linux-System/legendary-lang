# Legendary Lang

Język programowania dla **LegendaryOS** (dystrybucja bazująca na Fedorze), transpilowany do C++20.

## Zawartość repo

- `SPEC.md` — pełna specyfikacja języka: składnia, `std::*`, `config.lg`, `lpm`, `lgc`, funkcje "game changer" (`@hot_reload`, `profile`, `spawn`/`Channel`, `isolate`, `on`/`Signal`).
- `stage0/` — pierwsza implementacja transpilera `lgc` **napisana w C++20** (bootstrapping). Obecnie: pełny lexer + entry point CLI.
- `examples/` — przykładowe pliki `.lg`.

## Build stage0

```bash
cd stage0
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/lgc ../examples/lpm-example.lg --tokens
```

## Status

- [x] Lexer (komentarze `--` / `---` / `--( )--`, słowa kluczowe, literały, operatory)
- [ ] Parser -> AST
- [ ] Codegen C++20
- [ ] `config.lg` parser (dla `lpm`)
- [ ] `lpm build/run/clean` w oparciu o `lgc`
- [ ] Atrybuty `@wayland::server`, `@hot_reload` -> codegen
- [ ] stage1: przepisanie `lpm` na sam Legendary Lang, kompilowane przez `stage0/lgc`
- [ ] stage2: self-hosting `lgc`

Zobacz sekcję 11 (`Roadmapa bootstrapowania`) w `SPEC.md`.
