/*
 * Library lifetime — the process-global layer of the two-layer lifetime
 * (docs/adapter-findings.md, Finding 3). This file owns pathime_init() and
 * pathime_shutdown(): validating the init params, resolving the two
 * directories, and running each compiled-in backend's one-time global
 * initialization — anthy's dictionaries, pyzy's shared Database and
 * SpecialPhraseTable — through the lifetime hooks backend.h declares. It is
 * documented as the one slow call, so eager global work belongs here and
 * nowhere else. libhangul needs nothing at this layer in our build; the note
 * in pathime_init() says why, and corrects docs/adapter-findings.md, Finding 3 on the point.
 *
 * The pre-init introspection — version and status strings — is implemented
 * already rather than stubbed: the answers are ABI-fixed statics, and
 * tests/api/abi_test.c exercises them to prove the library links.
 *
 * Allocation discipline for this file and engine.cc: exceptions must not cross
 * the C boundary, and neither may they be avoided by new(std::nothrow) alone —
 * std::string and the engine's members can throw from a constructor that
 * nothrow placement does not cover. So every allocating step is wrapped and
 * std::bad_alloc becomes PATHIME_ERROR_OUT_OF_MEMORY.
 */

#include <pathime/pathime.h>

#include <cstdlib>
#include <new>
#include <string>

#include "backend.h"
#include "init.h"
#include "module_path.h"

#if defined(_WIN32)
#  include "win32_utf.h"
#endif

namespace {

/*
 * The whole of the library's process-global state: the initialized flag and
 * the two directories, because everything else a backend would need at this
 * layer lives behind backend.h.
 *
 * No locking: the header requires that calls into libpathime never overlap,
 * which is a requirement about concurrency rather than thread identity, and
 * these are written only by pathime_init()/pathime_shutdown().
 */
bool g_initialized = false;
std::string g_data_dir;
std::string g_resource_dir;

/*
 * Which backends brought their process-global prerequisites up.
 *
 * A backend that could not is reported unavailable rather than failing
 * pathime_init(), which is what the public header describes and is the only
 * behaviour that makes sense for a build carrying three engines: anthy without
 * its dictionary must not cost a client the two engines that would have
 * worked. It is also the difference between "this library cannot serve you"
 * and "this installation is missing a data file" — one is fatal, the other is
 * a per-engine fact pathime_has_engine() exists to report.
 */
bool g_hangul_ready = false;
bool g_anthy_ready = false;
bool g_pyzy_ready = false;

/*
 * An environment variable's value as UTF-8, or "" when unset *or* empty.
 *
 * Windows reads it wide. std::getenv there decodes in the active code page, and
 * a profile directory holding any character outside it would come back mangled
 * — which is the one place a default could produce a path the platform will not
 * accept, since every path libpathime is *given* is already UTF-8.
 */
std::string env_or_empty(const char *name)
{
#if defined(_WIN32)
    const std::wstring wide_name = pathime::utf8_to_utf16(name);
    const wchar_t *value = _wgetenv(wide_name.c_str());
    if (value == nullptr || value[0] == L'\0') {
        return std::string();
    }
    return pathime::utf16_to_utf8(value);
#else
    const char *value = std::getenv(name);
    return (value != nullptr) ? std::string(value) : std::string();
#endif
}

/*
 * The directory pathime_init_params_t::data_dir selects by being NULL.
 *
 * The header commits to "a platform-appropriate default beneath the user's
 * configuration directory", so this is a real decision rather than a
 * placeholder, and it is made the same way libhangul makes it for its own
 * keyboard data (engines/libhangul/hangul/hangulkeyboard.c:913-949): the platform's
 * per-user roaming application-data directory on Windows, the XDG
 * configuration directory on everything else.
 *
 * It is *config* rather than cache even though backends put caches beneath it
 * too. The directory is an identity — it is what replaces anthy's write-once
 * "personality", so a client wanting a second profile supplies a second
 * directory — and an identity that a cache-cleaner may delete would lose the
 * user's learned words.
 *
 * The last resort is the relative path "libpathime": determinate, per the
 * house rule that a fixed answer beats an undefined one, and reached only on a
 * system with no HOME and no XDG_CONFIG_HOME (or no APPDATA), where any
 * absolute guess would be a worse fiction. A client that cares — and an
 * embedded or phone-keyboard client always should — passes data_dir itself.
 */
std::string default_data_dir()
{
#if defined(_WIN32)
    std::string base = env_or_empty("APPDATA");
    if (base.empty()) {
        base = env_or_empty("LOCALAPPDATA");
    }
    if (base.empty()) {
        return "libpathime";
    }
    return base + "\\libpathime";
#else
    const std::string config_home = env_or_empty("XDG_CONFIG_HOME");
    if (!config_home.empty()) {
        return config_home + "/libpathime";
    }
    const std::string home = env_or_empty("HOME");
    if (!home.empty()) {
        return home + "/.config/libpathime";
    }
    return "libpathime";
#endif
}

/*
 * The directory pathime_init_params_t::resource_dir selects by being NULL:
 * `pathime-data` beside the libpathime binary.
 *
 * The rule is deliberately one rule rather than a search. A client that can
 * load the library can, by construction, reach anything sitting next to it —
 * which is true of an installed package, a portable directory the user is free
 * to move, and an application bundle alike, and true regardless of the
 * process's working directory. Adding fallbacks would buy nothing that the
 * explicit resource_dir does not already buy, and would cost the property that
 * makes this predictable: there is exactly one place the data can be, so a
 * client that gets it wrong finds out immediately rather than on the one
 * machine where a stale copy shadowed the right one.
 *
 * Empty when module_dir() cannot answer, which leaves the path below relative
 * and, in all but the accidental case, unopenable — so the engines that need a
 * data file report themselves unavailable. That is the same outcome as a
 * missing file and reaches the client through the same channel.
 */
std::string default_resource_dir()
{
    const std::string base = pathime::module_dir();
    if (base.empty()) {
        return "pathime-data";
    }
#if defined(_WIN32)
    return base + "\\pathime-data";
#else
    return base + "/pathime-data";
#endif
}

}  // namespace

namespace pathime {

bool initialized()
{
    return g_initialized;
}

bool backend_ready(pathime_engine_id_t id)
{
    if (!g_initialized) {
        return false;
    }
    switch (id) {
    case PATHIME_ENGINE_HANGUL:
        return g_hangul_ready;
    case PATHIME_ENGINE_ANTHY:
        return g_anthy_ready;
    case PATHIME_ENGINE_PINYIN:
    case PATHIME_ENGINE_BOPOMOFO:
        /* One backend, two ids — they rise and fall together. */
        return g_pyzy_ready;
    default:
        return false;
    }
}

const char *data_dir()
{
    /* Never NULL, as init.h promises — c_str() cannot be. Before
     * pathime_init() it is "", which no caller sees: every path that reads it
     * has already rejected the uninitialized case. */
    return g_data_dir.c_str();
}

const char *resource_dir()
{
    return g_resource_dir.c_str();
}

}  // namespace pathime

uint32_t pathime_version(void)
{
    return PATHIME_VERSION;
}

const char *pathime_version_string(void)
{
    return PATHIME_VERSION_STRING;
}

const char *pathime_status_string(pathime_status_t status)
{
    switch (status) {
    case PATHIME_OK:                        return "ok";
    case PATHIME_ERROR_INVALID_ARGUMENT:    return "invalid argument";
    case PATHIME_ERROR_UNKNOWN_ENGINE:      return "engine not available in this library";
    case PATHIME_ERROR_MISSING_CALLBACK:    return "client lacks a callback the engine requires";
    case PATHIME_ERROR_UNSUPPORTED:         return "engine does not implement this operation";
    case PATHIME_ERROR_NOT_INITIALIZED:     return "pathime_init() has not been called";
    case PATHIME_ERROR_ALREADY_INITIALIZED: return "pathime_init() has already succeeded";
    case PATHIME_ERROR_NOT_FOCUSED:         return "context is not focused";
    case PATHIME_ERROR_OUT_OF_MEMORY:       return "out of memory";
    case PATHIME_ERROR_BACKEND:             return "backend library or data file failure";
    }
    return "unknown status code";
}

pathime_status_t pathime_init(const pathime_init_params_t *params)
{
    /*
     * Arguments first, then state — the validation order every entry point in
     * the library follows. It decides one case the header does not spell out:
     * a *second* call carrying a malformed struct_size is
     * PATHIME_ERROR_INVALID_ARGUMENT rather than
     * PATHIME_ERROR_ALREADY_INITIALIZED. Both leave the library exactly as it
     * was, which is the guarantee that matters, and reporting the argument the
     * caller can actually fix is the more useful of the two.
     *
     * NULL params is legal and means every default applies.
     */
    if (params != nullptr) {
        /*
         * Exactly one layout of this struct has shipped, so exactly one size
         * is recognized. When a second field is added this becomes a range —
         * at least the size through the last field this library knows, at most
         * sizeof(pathime_init_params_t) — so that an older caller stays usable
         * against a newer library. A *larger* value stays an error in either
         * case: it means the caller set fields we would silently ignore.
         */
        if (params->struct_size != sizeof(pathime_init_params_t)) {
            return PATHIME_ERROR_INVALID_ARGUMENT;
        }

        /* An empty path names nothing. NULL is how "use the default" is
         * spelled, so "" cannot also mean it without making the two
         * indistinguishable to a client that built the string itself. */
        if (params->data_dir != nullptr && params->data_dir[0] == '\0') {
            return PATHIME_ERROR_INVALID_ARGUMENT;
        }
        if (params->resource_dir != nullptr && params->resource_dir[0] == '\0') {
            return PATHIME_ERROR_INVALID_ARGUMENT;
        }
    }

    if (g_initialized) {
        return PATHIME_ERROR_ALREADY_INITIALIZED;
    }

    /* Resolve into locals first: nothing global is touched until every step
     * that can fail has succeeded, which is what makes a failed init leave the
     * library uninitialized and retryable. */
    std::string resolved;
    std::string resolved_resources;
    try {
        if (params != nullptr && params->data_dir != nullptr) {
            resolved = params->data_dir;
        } else {
            resolved = default_data_dir();
        }
        if (params != nullptr && params->resource_dir != nullptr) {
            resolved_resources = params->resource_dir;
        } else {
            resolved_resources = default_resource_dir();
        }
    } catch (const std::bad_alloc &) {
        return PATHIME_ERROR_OUT_OF_MEMORY;
    }

    /*
     * Each compiled-in backend's one-time global init, given both directories
     * rather than the locations it would pick from the environment and its own
     * compiled-in prefix — which is the whole purpose of
     * pathime_init_params_t::data_dir and ::resource_dir. Every path either
     * one becomes is handed to the backend verbatim, so a client is free to
     * install wherever it likes.
     *
     * A hook failure is *not* fatal. It marks that backend unavailable and
     * pathime_init() still succeeds, because the alternative — one missing
     * dictionary costing a client the two engines that would have worked — is
     * not a trade any caller would choose, and because the header already has
     * the right channel for it: pathime_has_engine() is documented false for
     * an engine "whose runtime prerequisites, such as its dictionaries, are
     * unavailable". PATHIME_ERROR_BACKEND from this function would mean the
     * library itself is unusable, which is a different and much rarer claim.
     *
     * libhangul is deliberately absent, against what docs/adapter-findings.md, Finding 3 used
     * to say. hangul_init() and hangul_fini() exist only under
     * ENABLE_EXTERNAL_KEYBOARDS (engines/libhangul/hangul/hangul.h:99-103,
     * hangulkeyboard.c:994-1033), which our top-level CMakeLists.txt:34 turns
     * off to avoid an EXPAT dependency and a sed-based codegen step. Without it
     * there is no keyboard registry to populate: the nine built-in layouts are
     * static tables, so hangul has no process-global setup at all in this
     * build. Should external keyboards ever be turned on, the pair belongs here
     * and hangul_init() takes the user keyboard path — one more thing that
     * would be rooted at `resolved`.
     */
#if PATHIME_WITH_HANGUL
    g_hangul_ready = pathime::hangul_global_init(resolved.c_str(),
                                                 resolved_resources.c_str());
#endif
#if PATHIME_WITH_ANTHY
    g_anthy_ready = pathime::anthy_global_init(resolved.c_str(),
                                               resolved_resources.c_str());
#endif
#if PATHIME_WITH_PYZY
    g_pyzy_ready = pathime::pyzy_global_init(resolved.c_str(),
                                             resolved_resources.c_str());
#endif

    /* swap, not assign: it cannot throw, so the globals move to their new
     * values together and there is no state in which one has been updated and
     * the others have not. */
    g_data_dir.swap(resolved);
    g_resource_dir.swap(resolved_resources);
    g_initialized = true;
    return PATHIME_OK;
}

void pathime_shutdown(void)
{
    /* Documented as a no-op when the library is not initialized, so that a
     * caller which does not track whether pathime_init() succeeded can put
     * this on its failure path unconditionally. */
    if (!g_initialized) {
        return;
    }

    /*
     * The shutdown hooks, in the reverse of the order pathime_init() ran them.
     *
     * pyzy's is the one that does real work: PyZy::InputContext::finalize() is
     * also where its user database is written, because the save it schedules
     * through g_timeout_add and a GTimer never fires without a GMainLoop we do
     * not run (docs/design-history.md §5). Skipping this loses the user's learned phrases.
     *
     * By contract every engine and context is already destroyed, so nothing
     * here has to tear down per-context state.
     */
#if PATHIME_WITH_PYZY
    if (g_pyzy_ready) {
        pathime::pyzy_global_shutdown();
    }
#endif
#if PATHIME_WITH_ANTHY
    if (g_anthy_ready) {
        pathime::anthy_global_shutdown();
    }
#endif
#if PATHIME_WITH_HANGUL
    if (g_hangul_ready) {
        pathime::hangul_global_shutdown();
    }
#endif

    g_hangul_ready = false;
    g_anthy_ready = false;
    g_pyzy_ready = false;

    g_data_dir.clear();
    g_resource_dir.clear();
    g_initialized = false;
}
