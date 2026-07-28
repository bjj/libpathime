# libpathime

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

The first four wrap vendored submodules. The table engine is a reimplementation of
`ibus-table` that uses the same databases.

Each backend adds significant IME functionality on top of the base library: For example,
if you only care about Japanese, you could use Anthy directly, but you wouldn't get kana
input support.

## Platforms

Linux and Windows (MSVC and clang-cl). The vendored submodule trees are never
edited: everything Windows needs comes from an in-tree compat layer or a
configure-time generated copy. See **[BUILD.md](BUILD.md)**.

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
**[`demo/README.md`](demo/README.md)**

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
    pathime_context_set_focused(ctx, true);

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

`tests/` holds two kinds of suite, both run by `ctest`. `tests/hangul`,
`tests/anthy` and `tests/pyzy` test the ports of the vendored libraries
themselves — that they still behave the same after being built this way,
particularly on Windows. `tests/api` and `tests/core` test `libpathime`:
the public header driven exactly as a client would drive it, end to end
against each engine, plus the core's own units. `docs/testing.md` is how to run
them.

## Documentation

- `BUILD.md` — how to build
- `docs/testing.md` — how to run the tests
- `docs/CONCEPTS.md` — definition of terms: engine, client, composition, candidates
- `include/pathime/pathime.h` — the API, documented in full
- `docs/source-layout.md` — the map of `src/`
- `docs/windows-port.md` — how the Windows port works, and its limitations
- `demo/README.md` — the interactive terminal demo, and what to try in it
