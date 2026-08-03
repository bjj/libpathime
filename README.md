# libpathime

[![CI](https://github.com/bjj/libpathime/actions/workflows/ci.yml/badge.svg)](https://github.com/bjj/libpathime/actions/workflows/ci.yml)
[![CodeQL](https://github.com/bjj/libpathime/actions/workflows/codeql.yml/badge.svg)](https://github.com/bjj/libpathime/actions/workflows/codeql.yml)
[![codecov](https://codecov.io/gh/bjj/libpathime/branch/master/graph/badge.svg)](https://codecov.io/gh/bjj/libpathime)

A CJK input method engine as a plain C library.

Does it use the latest and greatest backend for each language? No. But does it require
any graphical desktop, daemons, message busses, or system integration? Also no!
It is a library you link, call synchronously from your own input thread, and render
yourself. This makes it usable in a game, on an embedded device, or wherever you need no-frills CJK input. It owns no window and no user interface; it
produces preedit text, candidates, and requests to edit your text, and you
decide what that looks like.

Composition resolves greedily, left to right: each candidate you select settles
the part of the composition it covers and produces a fresh list for what
remains. There is no segment navigation and no segment resizing.
A phone keyboard model rather than the desktop one. See `docs/CONCEPTS.md` for more details.

The name is its backends: **p**yzy, **a**nthy, **t**able, **h**angul.

## Input methods

| Engine | Method | Backend |
|---|---|---|
| `PATHIME_ENGINE_HANGUL` | Korean Hangul composition | libhangul |
| `PATHIME_ENGINE_ANTHY` | Japanese kana–kanji conversion | anthy-unicode |
| `PATHIME_ENGINE_PINYIN` | Chinese, Pinyin | pyzy |
| `PATHIME_ENGINE_BOPOMOFO` | Chinese, Bopomofo/Zhuyin | pyzy |
| `PATHIME_ENGINE_TABLE` | Table-driven: Wubi, Cangjie, Stroke5, Zhuyin, … | internal |

The first four engines are served by three vendored libraries — pyzy supplies both
Pinyin and Bopomofo. The table engine is part of this library but shares table formats with `ibus-table`.

Each adapter adds real input-method behaviour on top of the library it wraps. If you
only care about Japanese you could link anthy directly, but anthy accepts finished
kana and nothing else — the romaji and kana typing, the key handling and the
composition model would all still be yours to write.

## Platforms

Linux, macOS, and Windows (MSVC and clang-cl). What Windows needs comes from an
in-tree POSIX compat layer plus a short series of portability fixes carried as
commits on each vendored library's `libpathime` branch; macOS takes the
ordinary POSIX path, with sqlite3 and the UUID functions from the system.
See **[BUILD.md](BUILD.md)**.

## Demo

**[`/demo` is an interactive IME in a terminal](demo/README.md)**: type into a text field and watch
the preedit, the settled boundary, the candidate list, every callback the
library makes, and the whole option inventory with everything the active engine
implements editable live.

```bash
cmake -S . -B build -DLIBPATHIME_BUILD_DEMO=ON
cmake --build build
./build/bin/pathime-demo
```

It is a client of the public header and nothing else, so it also serves as a
worked example of the parts the snippet above skips — surrounding text,
candidate paging, and what to do with a key the engine declines. See
**[`demo/README.md`](demo/README.md)** for what to try in it.

## Bindings

There is a **[python binding](https://github.com/bjj/libpathime-python)** and
a **[C# binding](https://github.com/bjj/libpathime-sharp)** based on this library.
Each one also has its own demo. The [demo in the C# binding](https://github.com/bjj/libpathime-sharp/tree/master/demo/PathimeSharp.Demo)
is a graphical demo that actually looks like a phone.

## Example

Typing `nihao` and taking the first candidate, in full:

```c
#include <pathime/pathime.h>

static void on_commit(void *user_data, pathime_str_t text)
{
    /* Insert text.bytes[0 .. text.len) at your insertion point. */
}

static void on_changed(void *user_data, const pathime_composition_t *comp)
{
    /* Draw comp->preedit, then comp->candidate_count entries fetched with
     * pathime_context_candidate(). */
}

int main(void)
{
    pathime_engine_t *engine;
    pathime_context_t *ctx;
    pathime_client_t client = { sizeof(client), on_commit, NULL, on_changed };
    const char *keys = "nihao";

    pathime_init(NULL);
    pathime_engine_create(PATHIME_ENGINE_PINYIN, &engine);
    pathime_context_create(engine, &client, NULL, &ctx);

    for (; *keys; keys++) {
        pathime_key_event_t key = { sizeof(key), (uint32_t)*keys, 0, 0 };
        bool handled = false;
        pathime_context_process_key(ctx, &key, &handled);
    }
    /* Preedit now reads 你好, with 你好 利好 你 里 李 … as candidates. */

    pathime_context_select_candidate(ctx, 0);  /* commits 你好 */

    pathime_context_destroy(ctx);
    pathime_engine_destroy(engine);
    pathime_shutdown();
    return 0;
}
```

Everything the engine produces is dispatched through `client` before the call
that caused it returns. The API is synchronous throughout and starts no threads.

## Tests

`tests/` holds two kinds of suite, both run by `ctest`: one that checks the
vendored libraries still behave the same after being built this way, and one
that drives the public header exactly as a client would, end to end against
every engine. `docs/testing.md` has the detail and the commands.

## Documentation

Using the library:

- `include/pathime/pathime.h` — the API, documented in full
- `docs/CONCEPTS.md` — definition of terms: engine, client, composition, candidates
- `BUILD.md` — how to build, and every build option
- `demo/README.md` — the interactive terminal demo, and what to try in it
- `THIRD-PARTY.md` — what libpathime links and ships, and under what terms

Working on the library:

- `docs/source-layout.md` — the map of `src/`: which file owns what
- `docs/testing.md` — the test suites and how to run them
- `docs/windows-port.md` — how the Windows port works, and its limitations
- `src/engines/table/README.md` — the table engine's own map
- `docs/libhangul-mapping.md`, `docs/anthy-mapping.md`, `docs/pyzy-mapping.md` — how
  each vendored library connects to this one. Read before upgrading a submodule.
- `docs/ibus-table-mapping.md` — the ibus-table data format, and what this library
  does with each part of it
- `docs/japanese-input-model.md` — measured anthy and ibus-anthy behaviour, behind
  the Japanese design decisions

## License

libpathime is **MIT** (`LICENSE`).

The libraries it wraps are not. libhangul, anthy-unicode and pyzy are LGPL-2.1,
and each is built and loaded as a shared library. Two of the data files in
`pathime-data/` are compiled from GPL sources: anthy's dictionary and the
ibus-table-chinese tables. A build can be configured without either.

`THIRD-PARTY.md` is the full inventory.
