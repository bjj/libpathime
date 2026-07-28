/*
 * The engine layer: pathime_engine_* — what one engine shares across all of
 * its contexts (the middle layer of docs/adapter-findings.md, Finding 3). Owns:
 *
 *  - the engine registry, keyed by pathime_engine_id_t and gated on the
 *    PATHIME_WITH_* macros from <pathime/config.h> — pathime_has_engine()
 *    reads it, pathime_engine_create() consults it (one backend, pyzy,
 *    supplies two engine ids: PINYIN and BOPOMOFO);
 *  - engine handle lifecycle and pathime_engine_id();
 *  - pathime_engine_requirements() — today only hangul's PREEDIT_NONE mode
 *    sets a PATHIME_REQUIRES_* bit;
 *  - the engine level of the two-level option store (the machinery itself is
 *    options.cc's).
 *
 * The registry is the one place that knows which engine ids this build can
 * actually supply, and it is deliberately not a table of function pointers:
 * the arms are #if-gated so an id whose backend was compiled out falls through
 * to the same default as a value that is not an engine id at all, which is the
 * answer a caller wants in both cases.
 *
 * Allocation discipline is init.cc's: std::bad_alloc is caught at the boundary
 * and becomes PATHIME_ERROR_OUT_OF_MEMORY, rather than new(std::nothrow),
 * which would not cover a throwing member constructor.
 */

#include <pathime/pathime.h>

#include <memory>
#include <new>

#include "backend.h"
#include "engine.h"
#include "init.h"
#include "options.h"

namespace pathime {

bool engine_available(pathime_engine_id_t id)
{
    /*
     * False for everything before pathime_init(), as the header promises and
     * tests/api/abi_test.c asserts. This is not a formality: an engine's
     * runtime prerequisites — libhangul's keyboard registry, anthy's
     * dictionaries, pyzy's shared database — are precisely what pathime_init()
     * opens, so before it there is nothing to report on.
     */
    if (!initialized()) {
        return false;
    }

    /*
     * The registry. Each arm exists exactly when this build contains the
     * backend behind it, which is why the #ifs wrap the cases rather than the
     * bodies: an id whose backend was compiled out falls through to the
     * default, together with a value that is not an engine id at all. Note
     * that the mapping is not one-to-one — pyzy supplies both PINYIN and
     * BOPOMOFO, since the phonetic scheme is fixed when its context is created
     * — so a per-id switch is the registry's real shape, not a per-macro one.
     *
     * Being compiled in is necessary but not sufficient. backend_ready() is
     * the second half: it reports whether that backend's process-global
     * prerequisites actually came up during pathime_init() — anthy's
     * dictionary, pyzy's database — which is precisely the case the header
     * has this query answer false for. Hangul's hook cannot fail, so for it
     * this is always true once initialized.
     */
    switch (id) {
#if PATHIME_WITH_HANGUL
    case PATHIME_ENGINE_HANGUL:
        return backend_ready(id);
#endif

#if PATHIME_WITH_ANTHY
    case PATHIME_ENGINE_ANTHY:
        return backend_ready(id);
#endif

#if PATHIME_WITH_PYZY
    case PATHIME_ENGINE_PINYIN:
    case PATHIME_ENGINE_BOPOMOFO:
        return backend_ready(id);
#endif

#if PATHIME_WITH_TABLE
    case PATHIME_ENGINE_TABLE:
        /* Its global hook does have something to report after all: it resolves
         * the directory shipped tables are named against, and answers false
         * without one. A context with no PATHIME_OPT_TABLE_FILE still produces
         * nothing, which is a per-context state and not this question. */
        return backend_ready(id);
#endif

    default:
        /* The backend is not in this build, or @a id is not an engine id.
         * Both mean the same thing to a caller: do not try to create it. */
        return false;
    }
}

/**
 * Build the adapter's engine-level state for @a id, or nullptr if it cannot be
 * built. Only ever called for an id engine_available() has already approved,
 * so an unreachable default is the honest body for the rest.
 */
std::unique_ptr<EngineBackend> create_engine_backend(pathime_engine_id_t id)
{
    switch (id) {
#if PATHIME_WITH_HANGUL
    case PATHIME_ENGINE_HANGUL:
        return hangul_create_engine();
#endif
#if PATHIME_WITH_ANTHY
    case PATHIME_ENGINE_ANTHY:
        return anthy_create_engine();
#endif
#if PATHIME_WITH_PYZY
    case PATHIME_ENGINE_PINYIN:
    case PATHIME_ENGINE_BOPOMOFO:
        /* One backend, two ids: pyzy fixes its InputType when the context is
         * created, so the phonetic scheme has to travel with the id. */
        return pyzy_create_engine(id);
#endif
#if PATHIME_WITH_TABLE
    case PATHIME_ENGINE_TABLE:
        return table_create_engine();
#endif
    default:
        return nullptr;
    }
}

}  // namespace pathime

bool pathime_has_engine(pathime_engine_id_t id)
{
    return pathime::engine_available(id);
}

pathime_status_t pathime_engine_create(pathime_engine_id_t id,
                                       pathime_engine_t **out_engine)
{
    /* Arguments, then initialization, then state — the library-wide order.
     * out_engine is left untouched on every one of these paths. */
    if (out_engine == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!pathime::initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }

    /* engine_available() answers false when uninitialized too, so the check
     * above is what separates "you have not called pathime_init()" from "this
     * library cannot supply that engine" — two very different fixes. */
    if (!pathime::engine_available(id)) {
        return PATHIME_ERROR_UNKNOWN_ENGINE;
    }

    pathime_engine_t *engine = nullptr;
    try {
        engine = new pathime_engine();
    } catch (const std::bad_alloc &) {
        return PATHIME_ERROR_OUT_OF_MEMORY;
    }
    engine->id = id;

    /*
     * The adapter's engine-level state. Nothing is published to the caller
     * until it has succeeded, which is why the handle above is a bare pointer
     * this function still owns rather than something already in *out_engine.
     */
    engine->backend = pathime::create_engine_backend(id);
    if (engine->backend == nullptr) {
        delete engine;
        return PATHIME_ERROR_BACKEND;
    }

    *out_engine = engine;
    return PATHIME_OK;
}

void pathime_engine_destroy(pathime_engine_t *engine)
{
    /*
     * NULL is a no-op, which `delete` already gives us.
     *
     * The contract that every context created from this engine has already
     * been destroyed is the caller's, and it is left as one: engine.contexts
     * would make a check trivial, but a library that aborts on a client bug is
     * worse than one that documents the obligation, and there is no error
     * channel here to report it through. What the list does guarantee is that
     * a well-behaved caller leaves it empty, so nothing here dangles.
     */
    delete engine;
}

pathime_engine_id_t pathime_engine_id(const pathime_engine_t *engine)
{
    /*
     * Deliberately not NULL-guarded. pathime_engine_id_t has no "none" value,
     * so any answer this could invent for a NULL handle would name a real
     * engine and mislead the caller into thinking it holds one — worse than
     * the caller's own bug surfacing where it happened. A valid handle is part
     * of the contract for this call.
     */
    return engine->id;
}

uint32_t pathime_engine_requirements(const pathime_engine_t *engine)
{
    /* Unlike pathime_engine_id() this one has an honest answer for a handle
     * that does not exist: no callback obligation can arise from it. */
    if (engine == nullptr) {
        return 0;
    }

    /*
     * PATHIME_HANGUL_PREEDIT_NONE is the only thing in the library that sets
     * either bit (docs/design-history.md §1, "Cut in the API review round"). It holds no
     * preedit at all, building each syllable inside the client's document by
     * deleting the partial form and recommitting a fuller one, so it can only
     * work against a client that both supplies surrounding text and can delete
     * what it sees. Every other engine in every other configuration requires
     * nothing, which is why this function is usually 0.
     *
     * Resolved at the engine level — nullptr for the context — and that is the
     * point rather than an omission: engine-level resolution deliberately does
     * no capability capping, so this reports the true configured value. A
     * context whose client cannot serve it is rejected by
     * pathime_context_create() against exactly this answer, which is only
     * possible because the value has not already been quietly capped.
     *
     * The header's contract holds here: these are the *engine's* requirements,
     * so a context that overrides PATHIME_OPT_HANGUL_PREEDIT itself needs
     * whatever its own resolved value needs, and the option's documentation is
     * what a client reads for that.
     */
    uint32_t requirements = 0;

    if (pathime::resolve_option_number(engine, nullptr, PATHIME_OPT_HANGUL_PREEDIT) ==
        PATHIME_HANGUL_PREEDIT_NONE) {
        requirements |= PATHIME_REQUIRES_SURROUNDING_TEXT |
                        PATHIME_REQUIRES_DELETE_SURROUNDING;
    }

    return requirements;
}
