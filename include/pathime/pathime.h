/*
 * libpathime — public C API
 *
 * This header is the client-facing boundary described in docs/CONCEPTS.md.
 * Terminology (engine, input context, composition data, handled, ...) is used
 * exactly as defined there.
 *
 * ---------------------------------------------------------------------------
 * Conventions
 * ---------------------------------------------------------------------------
 *
 * Text
 *   All text crossing this boundary is plain UTF-8. Text must not contain
 *   embedded NUL bytes: U+0000 is not representable in this API, in either
 *   direction.
 *
 *   Text that participates in composition — preedit, candidates, commits,
 *   surrounding text — is passed as an explicit (pointer, byte length) pair and
 *   is never required to be NUL-terminated, though text produced by the library
 *   always is. The length is authoritative for how much text there is, not for
 *   whether it stops early. This form exists because such text is routinely a
 *   slice of a larger buffer the client already holds.
 *
 *   Short discrete values that name something rather than carrying prose — the
 *   data directory, option strings — are plain NUL-terminated pointers. No
 *   caller has a reason to pass a slice of one, and a length parameter would be
 *   ceremony rather than expression.
 *
 * Positions and counts
 *   Every position and every count in this API is measured in Unicode scalar
 *   values, never in bytes. The single exception is pathime_str_t::len, which
 *   is a buffer size rather than a position. This holds whether the text is
 *   one the library handed you or one only the client can see.
 *
 * Ownership
 *   Nothing in this header transfers ownership. Strings the caller passes in
 *   are borrowed for the duration of the call only; the library copies
 *   anything it needs to keep. Strings and structs the library hands back are
 *   owned by the library and remain valid until the next call that mutates the
 *   same input context. There are therefore no free/release functions for
 *   returned data — only the explicit *_destroy calls for handles.
 *
 *   "Mutates" is wider than it first reads, and the option setters are the ones
 *   that surprise: pathime_engine_set_option_*, pathime_context_set_option_*
 *   and both reset_option forms are mutating calls. An option set can discard
 *   the composition outright, and it re-materializes the candidate list even
 *   when it does not, so every slice reaching into a composition is invalid
 *   afterwards. The pathime_composition_t itself is the context's own and keeps
 *   its address, which makes this easy to get away with by accident — a stale
 *   pointer still reads a live struct — but a pathime_str_t copied out of it
 *   before the set points into storage the library has since reassigned.
 *
 * Threading
 *   Every function is synchronous, and the library neither starts threads nor
 *   dispatches callbacks anywhere but on the calling thread.
 *
 *   The library performs no locking, and its backends have process-global
 *   mutable state on their conversion paths — shared lattice scratch arrays
 *   and allocators in the Japanese backend, a single shared database handle
 *   and query buffer in the Chinese one. Two calls that overlap in time are a
 *   data race even when they name different contexts and different engines.
 *
 *   The requirement is therefore that **calls into libpathime never overlap**.
 *   That is a requirement about concurrency, not about thread identity: no
 *   function has thread affinity, so a client may hand its contexts to a
 *   different thread whenever it likes, provided the handoff establishes the
 *   usual happens-before relationship. One dedicated input thread satisfies
 *   this trivially and is the expected arrangement.
 *
 *   Locking internally was considered and rejected. Because the contended
 *   state is process-global rather than per-context, correctness would require
 *   one global lock, which would serialize every client anyway while adding
 *   the hazard of holding that lock across a pathime_client callback.
 *
 * Reentrancy
 *   While the library is invoking a pathime_client callback, the input context
 *   is mid-mutation. From inside a callback a client may call only the
 *   functions marked "callback-safe" below; every other function is undefined.
 *   The callback-safe set is exactly the non-mutating queries:
 *
 *     pathime_version, pathime_version_string, pathime_status_string,
 *     pathime_has_engine, pathime_engine_name, pathime_engine_id,
 *     pathime_engine_requirements,
 *     pathime_context_engine, pathime_context_user_data,
 *     pathime_context_requirements,
 *     pathime_context_composition, pathime_context_candidate,
 *     pathime_option_count, pathime_option_name, pathime_option_value_name,
 *     pathime_engine_option_info,
 *     pathime_engine_get_option_*, pathime_context_get_option_*,
 *     pathime_engine_option_is_set, pathime_context_option_is_set
 *
 *   This is enough for the common case, which is a client rendering a new
 *   candidate list from inside composition_changed: the library materializes
 *   every candidate the current cap allows before dispatching that callback,
 *   so pathime_context_candidate is a plain array read there and cannot
 *   re-enter the backend. Anything that changes state — including raising the
 *   candidate cap — must wait until the triggering call has returned.
 */

#ifndef PATHIME_H
#define PATHIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Generated at configure time. Defines PATHIME_WITH_HANGUL, PATHIME_WITH_ANTHY,
 * PATHIME_WITH_PYZY and PATHIME_WITH_TABLE as 0 or 1, according to which
 * backends this build actually contains, so that clients can compile out
 * unavailable paths, and PATHIME_BUILT_STATIC to say how it was linked.
 * The matching runtime query is pathime_has_engine().
 *
 * The macros are per *backend*, not per engine id, and one of them is not a
 * one-to-one mapping: PATHIME_WITH_PYZY covers both PATHIME_ENGINE_PINYIN and
 * PATHIME_ENGINE_BOPOMOFO, since one backend supplies both. The other three
 * name a single engine each.
 */
#include <pathime/config.h>

/* =========================================================================
 * Linkage
 * ========================================================================= */

/**
 * Decorates every public function. Expands to the platform's import
 * declaration by default, to its export declaration while the library itself
 * is being compiled (PATHIME_BUILD), and to nothing when the library is a
 * static archive (PATHIME_STATIC). Clients using the installed CMake package
 * get the right one automatically; others should define PATHIME_STATIC to
 * match PATHIME_BUILT_STATIC from <pathime/config.h>.
 */
#if defined(PATHIME_STATIC)
#  define PATHIME_API
#elif defined(_WIN32)
#  if defined(PATHIME_BUILD)
#    define PATHIME_API __declspec(dllexport)
#  else
#    define PATHIME_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define PATHIME_API __attribute__((visibility("default")))
#else
#  define PATHIME_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Version
 * =========================================================================
 *
 * Two kinds of versioning coexist here, answering different questions.
 *
 * The macros describe the header this translation unit was compiled against.
 * They are the only form usable in #if, so they are what guards conditional
 * compilation.
 *
 * The functions describe the library that was actually loaded, which for a
 * shared build is decided long after compilation. Compare them when a client
 * must degrade gracefully against an older runtime.
 *
 * Neither is the mechanism by which this API grows. Struct layouts are
 * versioned individually through their struct_size member, so a client that
 * gains a field does not need a global version bump to stay compatible, and
 * the library can tell precisely which fields a caller's struct really has.
 * The version numbers describe the release; struct_size describes the ABI.
 *
 * These four macros are the release version's single point of definition:
 * the build parses them (top of CMakeLists.txt), everything downstream
 * derives from that, and configure fails if PATHIME_VERSION_STRING and the
 * numeric macros disagree.
 */

#define PATHIME_VERSION_MAJOR 0
#define PATHIME_VERSION_MINOR 1
#define PATHIME_VERSION_PATCH 1

#define PATHIME_VERSION_STRING "0.1.1"

/** Encodes a version as a single comparable integer. */
#define PATHIME_VERSION_ENCODE(major, minor, patch) \
    (((major) * 1000000) + ((minor) * 1000) + (patch))

/** The version this header describes, as a comparable integer. */
#define PATHIME_VERSION \
    PATHIME_VERSION_ENCODE(PATHIME_VERSION_MAJOR, \
                           PATHIME_VERSION_MINOR, \
                           PATHIME_VERSION_PATCH)

/**
 * The version of the library actually linked, encoded as by
 * PATHIME_VERSION_ENCODE. May differ from PATHIME_VERSION when the library is
 * a shared object resolved at load time. Safe to call before pathime_init().
 * Callback-safe.
 */
PATHIME_API uint32_t pathime_version(void);

/**
 * The linked library's version as a string, e.g. "0.1.0". Never NULL.
 * Safe to call before pathime_init(). Callback-safe.
 */
PATHIME_API const char *pathime_version_string(void);

/* =========================================================================
 * Status
 * ========================================================================= */

/**
 * Result of any fallible call. PATHIME_OK is zero; every other value is an
 * error.
 *
 * Errors fall into two classes, and the difference matters to recovery.
 *
 * Rejections are decided before any work begins: the arguments, the client's
 * callback table, or the context's state made the call impossible. Nothing
 * changed, no callbacks were dispatched, and the client may simply try
 * something else. PATHIME_ERROR_INVALID_ARGUMENT, _UNKNOWN_ENGINE,
 * _MISSING_CALLBACK, _UNSUPPORTED, _NOT_INITIALIZED and _ALREADY_INITIALIZED
 * are all rejections.
 *
 * Failures happen partway through, when a backend or an allocation gives out
 * with work already done. The library cannot describe how far it got, and
 * makes no claim that callbacks already dispatched are consistent with the
 * state left behind. The context remains a valid handle but its composition
 * state is indeterminate: call pathime_context_reset() before trusting or
 * displaying anything from it. PATHIME_ERROR_OUT_OF_MEMORY and
 * PATHIME_ERROR_BACKEND are failures.
 *
 * Values are assigned explicitly and are part of the ABI. New codes are
 * appended at the end of the enum once a release has shipped them, never
 * inserted into a block: the two blocks below are a documentation grouping, and
 * a client tells rejections from failures by the code, not by its position.
 */
typedef enum pathime_status {
    PATHIME_OK = 0,

    /* Rejections — nothing happened. */
    PATHIME_ERROR_INVALID_ARGUMENT    = 1,  /**< NULL handle, bad index, bad UTF-8, bad struct_size. */
    PATHIME_ERROR_UNKNOWN_ENGINE      = 2,  /**< Engine not available in this library. */
    PATHIME_ERROR_MISSING_CALLBACK    = 3,  /**< Client lacks a callback the engine requires. */
    PATHIME_ERROR_UNSUPPORTED         = 4,  /**< Engine does not implement this operation. */
    PATHIME_ERROR_NOT_INITIALIZED     = 5,  /**< pathime_init() has not been called. */
    PATHIME_ERROR_ALREADY_INITIALIZED = 6,  /**< pathime_init() has already succeeded. */

    /* Failures — composition state is indeterminate until reset. */
    PATHIME_ERROR_OUT_OF_MEMORY       = 7,
    PATHIME_ERROR_BACKEND             = 8   /**< Backend library or data file failure. */
} pathime_status_t;

/**
 * Human-readable, English, static description of a status code. Never NULL,
 * including for a value this library does not define, which describes itself as
 * unknown. A static table lookup: safe to call before pathime_init().
 * For diagnostics; not intended for end users. Callback-safe.
 */
PATHIME_API const char *pathime_status_string(pathime_status_t status);

/* =========================================================================
 * Strings
 * ========================================================================= */

/**
 * A borrowed UTF-8 string slice. @a bytes is NUL-terminated when produced by
 * the library, but @a len is authoritative. When @a len is 0, @a bytes points
 * at "" rather than NULL. The first @a len bytes never contain a NUL: a slice
 * is a run of text, not a buffer that might end early.
 *
 * Note that @a len is the one byte-denominated quantity in this API: it sizes
 * a buffer, it does not locate anything. Every offset and count elsewhere is
 * in Unicode scalar values.
 *
 * This type is deliberately not versioned by a struct_size member. It is a
 * two-word value passed by copy on hot paths, and its shape is fixed.
 */
typedef struct pathime_str {
    const char *bytes;
    size_t len;  /**< Length in bytes, excluding any NUL. */
} pathime_str_t;

/* =========================================================================
 * Library lifetime
 * ========================================================================= */

/**
 * Process-global setup, supplied to pathime_init().
 *
 * This is deliberately not an option in the sense of the Options section below.
 * Everything here is consumed once, while global state is being built, and
 * cannot be changed afterward without a full shutdown.
 *
 * Initialize it in the declaration, so that the members you do not care about
 * are zeroed by the language rather than by you remembering to:
 *
 *     pathime_init_params_t params = { sizeof params };
 *     params.data_dir = "/some/writable/path";
 *
 * That form matters more than it looks. Setting struct_size tells the library
 * every member of this layout is filled in, so it reads all of them — see
 * struct_size below.
 */
typedef struct pathime_init_params {
    /**
     * Set to sizeof(pathime_init_params_t).
     *
     * Doing so asserts that *every* member below holds a value you chose, and
     * the library reads each of them on that basis. Members left uninitialized
     * are read anyway: a pointer member is dereferenced to validate it, so
     * stack garbage there is a wild pointer rather than a default. Declaring
     * the struct with an initializer, as above, is what makes that impossible.
     */
    size_t struct_size;

    /**
     * This is the whole of the library's persistent-storage surface.
     *
     * A directory the library may read and write, holding every piece of
     * per-user state any engine accumulates: learned word frequencies,
     * user-defined phrases, personal dictionaries, and backend caches. The
     * client owns this path and controls its lifetime; the library creates the
     * directory and whatever structure it needs beneath it.
     *
     * NULL selects a platform-appropriate default beneath the user's
     * configuration directory.
     *
     * A NUL-terminated filesystem path, in UTF-8 on every platform.
     *
     * The empty string is PATHIME_ERROR_INVALID_ARGUMENT rather than a second
     * spelling of NULL, so that a caller who built the path and got nothing is
     * told, instead of silently writing to the default. The same holds for
     * @a resource_dir.
     */
    const char *data_dir;

    /**
     * The directory holding the read-only data files this library ships with:
     * the Japanese dictionary, the Chinese phrase database, and whatever a
     * future engine adds. The library only reads from it, and never writes
     * there — everything an engine learns goes under @a data_dir.
     *
     * NULL selects the directory named `pathime-data`, beside the libpathime
     * binary itself: next to the shared library that is executing, or next to
     * the program when the library is linked statically. So a client that
     * keeps `pathime-data/` alongside the library it already ships needs to
     * set nothing here, wherever that pair is installed and whatever the
     * process's working directory happens to be. Supplying a path is for a
     * client whose layout separates the two — a system package with the code
     * in a library directory and the data under a shared one, say.
     *
     * A NUL-terminated filesystem path, in UTF-8 on every platform.
     *
     * An engine whose data is not found is reported unavailable by
     * pathime_has_engine(); it does not fail pathime_init() or affect the
     * other engines.
     */
    const char *resource_dir;
} pathime_init_params_t;

/**
 * Initialize process-global state shared by all engines: dictionary and
 * database files, keyboard-layout registries, and other one-time backend
 * setup.
 *
 * This is the one call in the API that may take a perceptible amount of time,
 * because it opens on-disk dictionaries. It is still synchronous; a caller
 * that cares should invoke it off its UI thread before creating any context.
 *
 * @param params May be NULL, which is equivalent to `{ sizeof params }`: every
 *               member at its default. A non-NULL struct must be initialized
 *               in full — see pathime_init_params_t::struct_size.
 *
 * Calling it again after it has succeeded, without an intervening shutdown, is
 * PATHIME_ERROR_ALREADY_INITIALIZED and changes nothing — in particular it does
 * not adopt the new @a params. A call that failed leaves the library
 * uninitialized, so it may simply be retried.
 *
 * Every function in this header requires it to have succeeded, with these
 * exceptions, which read no global state and are usable at any time:
 * pathime_version, pathime_version_string, pathime_status_string,
 * pathime_engine_name, pathime_option_count and pathime_option_name.
 * pathime_has_engine is usable
 * too but answers false for everything before initialization, since no engine
 * can be supplied yet; call it again afterward for a meaningful answer.
 * pathime_option_value_name has specific caveats; see its own docstring.
 */
PATHIME_API pathime_status_t pathime_init(const pathime_init_params_t *params);

/**
 * Release process-global state. All engines and input contexts must already
 * have been destroyed. A no-op when the library is not initialized, so it is
 * safe on the failure path of a caller that does not track whether
 * pathime_init() succeeded.
 */
PATHIME_API void pathime_shutdown(void);

/* =========================================================================
 * Engine
 * ========================================================================= */

/**
 * The set of input methods this library can provide. Which of them a given
 * build actually contains depends on the backends compiled in; query with
 * pathime_has_engine() or the PATHIME_WITH_* macros.
 *
 * Pinyin and Bopomofo are distinct entries rather than an option on one
 * engine because the underlying backend fixes the phonetic scheme when the
 * context is created; switching between them means creating a new context.
 *
 * PATHIME_ENGINE_TABLE is the opposite case: one entry covers every
 * table-driven method (Wubi, Cangjie, Stroke5, Zhuyin, …) because they differ
 * only in the table loaded, and which table that is is an option
 * (PATHIME_OPT_TABLE_FILE) rather than a separate id. Its implementation lives
 * in this library — see docs/ibus-table-mapping.md — rather than in a vendored
 * one, because the reference implementation is Python.
 *
 * Values are assigned explicitly and are part of the ABI: new engines are
 * appended, never inserted.
 */
typedef enum pathime_engine_id {
    PATHIME_ENGINE_HANGUL   = 0,  /**< Korean Hangul composition. */
    PATHIME_ENGINE_ANTHY    = 1,  /**< Japanese kana-kanji conversion. */
    PATHIME_ENGINE_PINYIN   = 2,  /**< Chinese, Pinyin phonetic input. */
    PATHIME_ENGINE_BOPOMOFO = 3,  /**< Chinese, Bopomofo/Zhuyin phonetic input. */
    PATHIME_ENGINE_TABLE    = 4   /**< Table-driven input from a loaded table. */
} pathime_engine_id_t;

/**
 * A stable, machine-readable name for an engine id: "hangul", "anthy",
 * "pinyin", "bopomofo", "table". The engine counterpart of
 * pathime_option_name(), with the same contract — a key for a client's own
 * configuration storage, never NULL, never changed once an engine ships, and
 * not text to put in front of a user. A value that is not an engine id yields
 * "", which is never a valid engine name.
 *
 * Answered for every engine this header names whether or not this build
 * contains it: what a name means is not conditional on what is installed.
 * Availability is pathime_has_engine()'s question.
 *
 * A static table lookup: safe to call before pathime_init(). Callback-safe.
 */
PATHIME_API const char *pathime_engine_name(pathime_engine_id_t id);

/**
 * True iff pathime_engine_create() can currently supply @a id. False both for
 * an engine this library does not contain and for one whose runtime
 * prerequisites, such as its dictionaries, are unavailable — and false for
 * every engine before pathime_init() has succeeded, or for a value that is not
 * an engine id at all. There is no error channel here because there is nothing
 * a client would do differently: false means "do not try to create this."
 * Callback-safe.
 */
PATHIME_API bool pathime_has_engine(pathime_engine_id_t id);

/**
 * An engine: one input method implementation plus whatever state it shares
 * across input contexts, such as loaded dictionaries and user history.
 * Engines are comparatively expensive; create one per input method and share
 * it across every input context using that method.
 */
typedef struct pathime_engine pathime_engine_t;

/**
 * Create an engine.
 *
 * @param id         Which input method to instantiate.
 * @param out_engine Receives the new engine on success, untouched on failure.
 *
 * Returns PATHIME_ERROR_UNKNOWN_ENGINE if pathime_has_engine() is false for
 * @a id. The caller owns the result and must pass it to
 * pathime_engine_destroy() after destroying every context created from it.
 */
PATHIME_API pathime_status_t pathime_engine_create(pathime_engine_id_t id,
                                                   pathime_engine_t **out_engine);

/**
 * Destroy an engine. All input contexts created from it must already have been
 * destroyed. NULL is a no-op.
 */
PATHIME_API void pathime_engine_destroy(pathime_engine_t *engine);

/**
 * Which input method @a engine was created for. Saves a client juggling several
 * engines from carrying the id alongside every handle. Callback-safe.
 */
PATHIME_API pathime_engine_id_t pathime_engine_id(const pathime_engine_t *engine);

/** Bits returned by pathime_engine_requirements(). */
enum {
    /**
     * The engine cannot work correctly unless the client keeps
     * pathime_context_set_surrounding_text() up to date.
     *
     * "Up to date" is a stronger obligation than it sounds: the snapshot
     * the client last supplied is the only text the engine can see, and every
     * commit_text the engine performs invalidates it. The engine
     * therefore depends on the client refreshing the snapshot after each
     * dispatch — not merely when the user moves the caret. A client that
     * refreshes only on caret movement will find the engine progressively
     * unable to revise its own output.
     */
    PATHIME_REQUIRES_SURROUNDING_TEXT = 1u << 0,

    /**
     * The engine will ask the client to delete text it has already inserted,
     * so pathime_client::delete_surrounding_text must be supplied. Always
     * accompanied by PATHIME_REQUIRES_SURROUNDING_TEXT, since the engine can
     * only ask to delete text it can see.
     */
    PATHIME_REQUIRES_DELETE_SURROUNDING = 1u << 1
};

/**
 * What this engine needs from its client, as a bitwise OR of
 * PATHIME_REQUIRES_*.
 *
 * Requirements depend on how the engine is configured — Hangul needs both
 * surrounding text and deletion under PATHIME_HANGUL_PREEDIT_NONE and neither
 * otherwise — so query this after configuring the engine and before creating
 * contexts. pathime_context_create() enforces it: a client missing a required
 * callback is rejected with PATHIME_ERROR_MISSING_CALLBACK rather than
 * silently losing engine output.
 *
 * The bits describe the engine's own resolved configuration, which is the
 * configuration a context inherits when it overrides nothing. A context that
 * overrides the responsible option needs whatever *its* resolved value needs;
 * ask pathime_context_requirements() for that. A context-level set the client
 * cannot satisfy is rejected at that point, with the same
 * PATHIME_ERROR_MISSING_CALLBACK. Callback-safe.
 */
PATHIME_API uint32_t pathime_engine_requirements(const pathime_engine_t *engine);

/* =========================================================================
 * Key events
 * ========================================================================= */

/**
 * Modifier bits in pathime_key_event_t::modifiers.
 *
 * Modifiers are reported alongside the keysym, never folded into it and never
 * applied to it. A client reports what its layout produced, not what the
 * engine should derive: Shift+Q is keysym 'Q' with PATHIME_MOD_SHIFT set, and
 * an engine may not turn 'q' into 'Q' because Shift was present, nor assume
 * 'Q' implies Shift — a Shift-Lock layout can produce one without the other.
 *
 * Each bit is consulted for a specific reason and none is decorative:
 *
 *   SHIFT    combines with layout_key to reach the shifted position, which is
 *            how Hangul selects the doubled jamo on a physical key.
 *   CONTROL  a chorded key is a client shortcut. Engines decline these rather
 *   ALT      than absorb them, which is the main reason modifiers must reach
 *   SUPER    the engine at all.
 *   CAPS     lets the engine undo the layout's capitalization where a backend
 *            expects case to mean Shift.
 *   NUMLOCK  reported for completeness; no current engine consults it.
 */
enum {
    PATHIME_MOD_SHIFT   = 1u << 0,
    PATHIME_MOD_CONTROL = 1u << 1,
    PATHIME_MOD_ALT     = 1u << 2,
    PATHIME_MOD_SUPER   = 1u << 3,  /**< Windows / Command key. */
    PATHIME_MOD_CAPS    = 1u << 4,  /**< CapsLock latched. */
    PATHIME_MOD_NUMLOCK = 1u << 5
};

/**
 * Named keys, for pathime_key_event_t::keysym. Printable keys need no constant:
 * their keysym is derived from the Unicode scalar as described below. These
 * cover the non-printable keys engines actually dispatch on; the values are
 * X11 keysyms, so a client on an X11-derived stack can pass keysyms straight
 * through.
 *
 * "Actually dispatch on" is the whole membership test, and it excludes keys a
 * CJK keyboard has that this library nonetheless never acts on. Hangul and
 * Hiragana_Katakana are the two that look like they belong here and do not.
 * Both are rebindable hotkeys in their reference engines, and each drives
 * something this API places on the client's side of the boundary: Hangul
 * toggles Hangul/Latin mode, which is engine activation state and excluded from
 * the model, so a client leaves Latin by not offering keys to the engine; and
 * Hiragana_Katakana selects a kana script, which is PATHIME_OPT_ANTHY_KANA_SCRIPT
 * for the client to set. Passing either to pathime_context_process_key() is
 * harmless and reports the key unhandled. The rule generalizes: a key that
 * changes a mode rather than the composition is the client's to interpret.
 */
enum {
    PATHIME_KEY_BACKSPACE = 0xff08,
    PATHIME_KEY_TAB       = 0xff09,
    PATHIME_KEY_RETURN    = 0xff0d,
    PATHIME_KEY_ESCAPE    = 0xff1b,
    PATHIME_KEY_SPACE     = 0x0020,  /**< Printable, but the usual convert key. */
    PATHIME_KEY_DELETE    = 0xffff,

    PATHIME_KEY_HOME      = 0xff50,
    PATHIME_KEY_LEFT      = 0xff51,
    PATHIME_KEY_UP        = 0xff52,
    PATHIME_KEY_RIGHT     = 0xff53,
    PATHIME_KEY_DOWN      = 0xff54,
    PATHIME_KEY_PAGE_UP   = 0xff55,
    PATHIME_KEY_PAGE_DOWN = 0xff56,
    PATHIME_KEY_END       = 0xff57,

    /* Japanese conversion keys, present on JIS keyboards. */
    PATHIME_KEY_MUHENKAN = 0xff22,  /**< Cancel conversion. */
    PATHIME_KEY_HENKAN   = 0xff23   /**< Begin/advance conversion. */
};

/**
 * One key press offered to the engine. Key releases are not represented; a
 * client should not report them.
 *
 * @a keysym is the logical key — the character or named key the client's own
 * layout produced, with every modifier the layout applies already applied.
 * Values are X11 keysyms: for printable characters below U+0100 the keysym
 * equals the Unicode scalar, above that it is the usual 0x01000000 + codepoint
 * encoding, and named keys use the PATHIME_KEY_* constants. This is the field
 * engines dispatch on.
 *
 * Keysyms are passed through unvalidated. The X11 keysym space is open-ended
 * and no useful membership test exists, so an unrecognized value is not an
 * error: engines dispatch on the ones they know and report the rest unhandled,
 * which is also what a client wants for keys it has invented. The one thing
 * the library does check is that @a keysym is nonzero.
 *
 * @a layout_key identifies the *physical* key, expressed as the keysym that
 * key would produce **unmodified** on a US QWERTY layout, or 0 when the client
 * has no physical key to report (on-screen and predictive keyboards). It is
 * therefore independent of @a modifiers by construction: pressing Shift+Q on a
 * US keyboard gives keysym 'Q', layout_key 'q', modifiers PATHIME_MOD_SHIFT,
 * and an engine that cares about position recombines the two itself.
 *
 * Hangul composition is defined by key position rather than by character, so a
 * client driving Hangul from a physical non-US layout must supply this; every
 * engine tolerates 0 and falls back to @a keysym. Expressing position as a
 * US-QWERTY keysym rather than a raw scancode keeps platform scancode tables
 * on the side of the API that already has them.
 */
typedef struct pathime_key_event {
    /**
     * Set to sizeof(pathime_key_event_t). Lets the library tell which fields a
     * caller compiled against; a value it does not recognize is
     * PATHIME_ERROR_INVALID_ARGUMENT.
     */
    size_t struct_size;

    uint32_t keysym;
    uint32_t layout_key;
    uint32_t modifiers;  /**< Bitwise OR of PATHIME_MOD_*. */
} pathime_key_event_t;

/* =========================================================================
 * Composition data
 * ========================================================================= */

/**
 * The complete composition state of an input context, as defined in
 * docs/CONCEPTS.md: preedit text and a candidate list.
 *
 * This is a snapshot owned by the input context, with the ordinary lifetime:
 * it and everything it reaches stay valid until the next call that mutates
 * that context. Copy anything that must outlive that.
 *
 * An empty preedit and a zero candidate count each mean "not currently
 * present".
 */
typedef struct pathime_composition {
    /**
     * Filled in by the library with the size of the struct it knows how to
     * write. A client compiled against a newer header than the loaded library
     * must not read past this many bytes.
     */
    size_t struct_size;

    /**
     * Provisional, uncommitted text. Plain UTF-8, no attributes.
     *
     * One rule fixes what this contains, for every engine:
     *
     *   The preedit is what the user has settled, followed by what they have
     *   typed and not yet settled, rendered in the script they are composing
     *   in. No engine rewrites it with a conversion the user has not chosen.
     *
     * So Japanese shows kana, Pinyin shows syllables, Bopomofo shows zhuyin,
     * and a table method shows its key run — each with whatever the user has
     * already chosen standing in front of it, up to @a preedit_settled. An
     * engine's guesses live in the candidate list, where the user can take one
     * or ignore it; they never appear here unasked.
     *
     * The practical guarantee a client gets from that: this is the text that
     * would be committed if the composition ended right now, up to two kinds of
     * departure, neither of which changes which characters the user chose.
     *
     * The first is normalization an engine applies at the moment of commit and
     * cannot apply earlier — Japanese displays a trailing romaji "n" as "n",
     * because one more key still decides whether it becomes ん or な, and commits
     * it as ん; Pinyin and Bopomofo drop the separators they render between
     * syllables.
     *
     * The second is a table method whose table supplies *key legends*. Such a
     * table names a symbol for each key — Cangjie's `a` is 日, `b` is 月, `h` is
     * 竹 — and the preedit shows those symbols rather than the letters, because
     * they are what is printed on the keyboard the method was designed for, and
     * showing the letters instead would hide the very thing being composed.
     * Committing without choosing a candidate then yields the *letters*, which
     * is the method's own escape hatch to Latin text. Read this as still
     * satisfying the rule above: the preedit and the commit are two renderings
     * of one key run, not two different pieces of text. A client that must have
     * the literal run has it either way, since it typed the keys.
     */
    pathime_str_t preedit;

    /**
     * Preedit display position: the number of Unicode scalar values at the
     * start of @a preedit that the engine considers settled. Text before this
     * position is not expected to change; text at or after it is still subject
     * to change as input continues.
     *
     * Ranges from 0, meaning nothing is settled, to the number of scalar
     * values in @a preedit, meaning all of it is — which is a transient state
     * just before the engine commits, not a resting one.
     */
    size_t preedit_settled;

    /**
     * Number of candidates, at absolute positions [0, candidate_count).
     * Retrieve them with pathime_context_candidate().
     *
     * Every candidate in the list is an alternative for the same span: the
     * leftmost unsettled portion of the preedit, which begins at
     * @a preedit_settled. There is never more than one span under
     * consideration, and the client never chooses which one that is —
     * selecting a candidate settles this span and produces a fresh list for
     * whatever follows it.
     *
     * This is the whole list the client may choose from. Engines whose
     * backends enumerate lazily materialize up to PATHIME_OPT_MAX_CANDIDATES
     * and stop; the client is never obliged to ask for more, and pagination is
     * purely a display concern.
     *
     * Whether the list is *complete* is therefore readable from its length,
     * and a client that offers to page beyond it needs the test: a count below
     * the resolved PATHIME_OPT_MAX_CANDIDATES was not truncated by the cap, so
     * there is nothing further to be had and raising the cap would only leave
     * the option changed for nothing. A count equal to the cap may or may not
     * have more behind it. That one case is genuinely undecidable from here —
     * a lazy backend sitting on exactly the cap looks the same as a truncated
     * one — and a client that wants the rest raises the cap and re-reads,
     * which appends without renumbering anything already handed out.
     */
    size_t candidate_count;

    /**
     * The candidate a client draws highlighted. Always < @a candidate_count
     * when the candidate list is non-empty, or 0 when the list is empty.
     *
     * Highlighting is not decoration, because on some engines moving the
     * cursor also rewrites the unsettled span of @a preedit — the highlight
     * and the text the user is looking at are then two views of one fact,
     * which is why the cursor is composition data rather than something the
     * client tracks privately.
     *
     * When that happens follows from the rule at @a preedit rather than being
     * a per-engine quirk: the preedit only ever shows a conversion the user
     * asked for, so the cursor rewrites it exactly where the user has already
     * asked, and only there. It is a property of the moment, not of the
     * engine. Candidates that arrived unasked — Pinyin and Bopomofo from the
     * first keystroke, Japanese while PATHIME_OPT_PREDICTION offers them
     * before any convert key — are being *browsed*, and moving the cursor
     * through them leaves the preedit alone. On Japanese,
     * PATHIME_KEY_SPACE is the request: after it the same cursor chooses
     * *among* conversions, so the preedit follows it. Either way the
     * invariant a client depends on holds: ending the composition commits
     * what is on screen.
     *
     * It follows that a client must draw its highlight from this field, and
     * must not assume the value it last set is still here. The cursor moves
     * three ways and two of them are not the client's:
     *
     *   - pathime_context_set_candidate_cursor(), which is the client's.
     *   - PATHIME_KEY_SPACE, on an engine that converts by cycling. The first
     *     press begins previewing at whatever the cursor already points to —
     *     a hover made while browsing is adopted, never discarded — and each
     *     further press advances it.
     *   - Any change that replaces the list — a span settling, a fresh
     *     conversion, new input — which starts the new list at 0.
     *
     * Every one of those arrives with a composition_changed, so a client that
     * redraws from this struct on that callback is never out of step. That is
     * the whole of the obligation: set it freely, read it back always.
     */
    size_t candidate_cursor;
} pathime_composition_t;

/* =========================================================================
 * Client interface
 * ========================================================================= */

/**
 * Operations the engine performs on the client's text.
 *
 * All callbacks are invoked synchronously, on the calling thread, before the
 * pathime_context_* call that triggered them returns. When one call produces
 * several of them they are dispatched in the order the client must apply them;
 * composition_changed always arrives last, so after every call the client's
 * view of the composition matches the engine's.
 *
 * A NULL member means the client does not support that operation, and is how
 * a client declares its capabilities. Creating a context whose engine requires
 * a missing callback fails rather than degrading silently.
 *
 * @a user_data is the pointer given to pathime_context_create() and is passed
 * back unchanged.
 *
 * A callback may call the callback-safe functions listed at the top of this
 * header and nothing else.
 */
typedef struct pathime_client {
    /**
     * Set to sizeof(pathime_client_t). Members beyond the size given are not
     * read, so a client built against an older header stays usable against a
     * newer library. A value the library does not recognize is
     * PATHIME_ERROR_INVALID_ARGUMENT from pathime_context_create().
     */
    size_t struct_size;

    /**
     * Insert finalized text at the insertion position, using the client's
     * ordinary text-editing behaviour. @a text is borrowed for the duration of
     * the call. Required; may not be NULL.
     */
    void (*commit_text)(void *user_data, pathime_str_t text);

    /**
     * Delete client text near the insertion position.
     *
     * The range is expressed against the text most recently given to
     * pathime_context_set_surrounding_text(), with the origin at the cursor
     * position reported by that call — not against wherever the client's
     * insertion point has since moved, and not against the result of any
     * earlier callback in this same dispatch. The engine only ever asks to
     * delete text it can see, so the requested range always lies within that
     * supplied text; a client whose document has changed since it last called
     * set_surrounding_text may safely ignore the request rather than delete
     * the wrong thing.
     *
     * Ordering is fixed to make this unambiguous: within one dispatch, any
     * delete_surrounding_text arrives before any commit_text, so the deletion
     * is always relative to the document as the engine last saw it.
     *
     * At most one delete_surrounding_text arrives per dispatch.
     *
     * The engine's own commits are what most often move the document out from
     * under this frame of reference: a commit_text invalidates the snapshot
     * immediately, and until the client supplies a fresh one the engine can no
     * longer see the text it just inserted. When an engine wants to revise text
     * the current snapshot does not cover — because it is stale, absent, or too
     * short — it does not guess: it abandons the revision, discards the
     * composition state that was to be revised, and treats what is already in
     * the document as final, continuing from the next input as if starting
     * fresh. No deletion is requested and no key is refused; the user sees the
     * partial text stay where it is. This is the same recovery ibus-hangul
     * performs when its caret-sanity check fails.
     *
     * @param offset Start of the range relative to that cursor position, in
     *               Unicode scalar values; negative is before it.
     * @param count  Number of Unicode scalar values to delete. Never 0.
     *
     * Optional. Engines that require it say so through
     * pathime_engine_requirements().
     */
    void (*delete_surrounding_text)(void *user_data, ptrdiff_t offset, size_t count);

    /**
     * The composition data has been replaced. @a composition is borrowed with
     * the ordinary lifetime — it is the same object pathime_context_composition()
     * returns, valid until the next mutating call, so a client that only reads
     * it during the callback need not copy. Positions in any previous candidate
     * list are now obsolete.
     *
     * Optional: a client with no way to display composition state may omit it,
     * though most engines are then of little use.
     */
    void (*composition_changed)(void *user_data, const pathime_composition_t *composition);
} pathime_client_t;

/* =========================================================================
 * Input context
 * ========================================================================= */

/**
 * One independently editable client destination and the engine state that
 * belongs to it: composition state, surrounding text, and per-context
 * settings.
 */
typedef struct pathime_context pathime_context_t;

/**
 * Create an input context.
 *
 * @param engine    The engine that will serve this context. Must outlive it.
 * @param client    Callback table. Borrowed by pointer and must remain valid
 *                  and unchanged for the context's lifetime; the library does
 *                  not copy it.
 * @param user_data Opaque pointer passed to every callback.
 * @param out_ctx   Receives the new context on success.
 *
 * Fails with PATHIME_ERROR_MISSING_CALLBACK if @a client omits a callback that
 * pathime_engine_requirements() reports as required.
 *
 * A new context starts with empty composition data, no surrounding text, and no
 * options overridden from the engine values.
 */
PATHIME_API pathime_status_t pathime_context_create(pathime_engine_t *engine,
                                                    const pathime_client_t *client,
                                                    void *user_data,
                                                    pathime_context_t **out_ctx);

/**
 * Destroy an input context, discarding composition state without committing
 * it. No callbacks are dispatched. NULL is a no-op.
 */
PATHIME_API void pathime_context_destroy(pathime_context_t *ctx);

/**
 * The engine that serves @a ctx, and the @a user_data given to
 * pathime_context_create(), both returned unchanged. These exist for language
 * bindings, which are routinely handed a bare context handle by their own
 * callback plumbing and need to find the wrapper object that owns it.
 * Callback-safe.
 */
PATHIME_API pathime_engine_t *pathime_context_engine(const pathime_context_t *ctx);
PATHIME_API void *pathime_context_user_data(const pathime_context_t *ctx);

/**
 * What one context needs from its client, as a bitwise OR of PATHIME_REQUIRES_*: 
 * the same bits pathime_engine_requirements() returns, resolved against this
 * context's own settings rather than the engine's.
 *
 * Zero for NULL. Callback-safe.
 */
PATHIME_API uint32_t pathime_context_requirements(const pathime_context_t *ctx);

/* ---- Key input -------------------------------------------------------- */

/**
 * Offer a key press to the engine.
 *
 * Any commit, deletion, or composition change caused by the event is
 * dispatched through pathime_client before this returns — including when the
 * engine reports the event unhandled. An engine may therefore absorb a key
 * into its composition state, emit the resulting text, and still hand the
 * original key back for the client to act on, with the ordering already
 * correct.
 *
 * @param out_handled Set to true if the engine accepted responsibility for the
 *                    event, in which case the client must not also process the
 *                    original event through its normal text-input path; false
 *                    if it declined, leaving the client free to treat the key
 *                    as it normally would. Always written: set to false on any
 *                    error return, so the client's fallback path is correct
 *                    even if it ignores the status.
 *
 * "Handled" describes the incoming event only, and is independent of whatever
 * output was produced while processing it.
 *
 * Two keys have a fixed meaning across every engine that composes, and a
 * client should not rebind them into each other:
 *
 * - PATHIME_KEY_SPACE asks for conversion. It is what advances a composition
 *   from what was typed toward what the engine thinks was meant. Where
 *   candidates are already showing unasked — see PATHIME_OPT_PREDICTION — the
 *   conversion begins at the hovered candidate, so a hover made while
 *   browsing is adopted rather than discarded. With nothing
 *   composing there is nothing to convert, and it inserts a space instead, at
 *   the width PATHIME_OPT_LATIN_WIDTH selects. Hangul is the exception: it
 *   implements no width option, so it declines the key and the client inserts
 *   its own space, which is the same character in the same place.
 * - PATHIME_KEY_RETURN ends the composition *without* applying any conversion
 *   the user has not explicitly chosen. Whatever the user did settle — a
 *   candidate selected, a segment converted — is kept; the rest commits as
 *   typed. Return is therefore the way out of a composition the engine is
 *   converting wrongly, without backspacing through it.
 *
 *   Because no engine previews an unchosen conversion, Return commits the
 *   preedit the client was last shown, subject only to the commit-time
 *   normalizations named at pathime_composition_t::preedit.
 *
 * A key that starts no composition may still be handled. An engine that emits
 * punctuation or full-width text of its own — see PATHIME_OPT_LATIN_WIDTH and
 * PATHIME_OPT_PUNCTUATION_WIDTH — commits it outright and reports the key
 * handled, with nothing composing before or after. Where such a key arrives
 * mid-composition, the composition is ended first and both commits are
 * dispatched in order, so the client never has to reorder them.
 */
PATHIME_API pathime_status_t pathime_context_process_key(pathime_context_t *ctx,
                                                         const pathime_key_event_t *event,
                                                         bool *out_handled);

/* ---- Composition ------------------------------------------------------ */

/**
 * The context's current composition data. Never NULL for a valid context.
 * Borrowed; invalidated by the next mutating call. Use this to re-render after
 * a redraw; use the composition_changed callback to learn that it changed.
 * Callback-safe.
 */
PATHIME_API const pathime_composition_t *pathime_context_composition(const pathime_context_t *ctx);

/**
 * Fetch one candidate by its absolute position in the current candidate list.
 *
 * @param index Absolute 0-based position, unrelated to any pagination the
 *              client performs. Must be < composition.candidate_count.
 * @param out   Receives a borrowed slice, invalidated by the next mutating
 *              call.
 *
 * A candidate is text. There are no labels or annotations in this API.
 *
 * Callback-safe: candidates are fully materialized before composition_changed
 * is dispatched, so this reads an array and never re-enters a backend.
 */
PATHIME_API pathime_status_t pathime_context_candidate(const pathime_context_t *ctx,
                                                       size_t index,
                                                       pathime_str_t *out);

/**
 * Move pathime_composition_t::candidate_cursor to absolute position @a index,
 * without choosing what is there.
 *
 * This is how a client navigates a candidate list: it is what the arrow keys
 * of a desktop client and the swipe of a phone keyboard both end in. The
 * library deliberately offers no "next" and "previous" — the client owns the
 * key bindings, knows its own pagination, and decides whether either end of
 * the list wraps, so an absolute index is the only part of that it needs from
 * here.
 *
 * There is no matching getter, and that is the point: the cursor is
 * composition data, read from the struct like everything else there. This call
 * is a request rather than an assignment, and the value it produces is not
 * necessarily the value it leaves behind — Space and any replacement of the
 * list move the cursor too. See the field for the full rule; the short form is
 * that a client sets the cursor freely and always redraws from the struct.
 *
 * The distinction from pathime_context_select_candidate() is the whole point
 * of having both: moving the cursor changes what is *shown* and settles
 * nothing, so it can be undone by moving it back and it commits no text.
 * Selecting is irrevocable — it settles the span and advances the composition.
 *
 * An engine that previews its candidates rewrites the unsettled span of the
 * preedit to match. Either way composition_changed is dispatched before this
 * returns, so the cursor a client draws from is never the one it just asked
 * for on trust.
 *
 * @param index Absolute 0-based position, unrelated to any pagination the
 *              client performs. Must be < composition.candidate_count, which
 *              also means this fails for an engine that produces no candidates.
 *
 * Returns PATHIME_ERROR_INVALID_ARGUMENT for an index outside the current
 * list, and PATHIME_ERROR_UNSUPPORTED from an engine that has candidates but
 * cannot show one without choosing it.
 */
PATHIME_API pathime_status_t pathime_context_set_candidate_cursor(pathime_context_t *ctx,
                                                                  size_t index);

/**
 * Tell the engine the client has chosen the candidate at absolute position
 * @a index in the most recently supplied candidate list.
 *
 * Selection is greedy and resolves left to right: choosing a candidate settles
 * the portion of the composition it covers, extends the settled region of the
 * preedit, and produces a fresh candidate list for whatever input remains. The
 * client never navigates or resizes segments; when nothing remains, the engine
 * commits. All resulting output is dispatched before this returns.
 *
 * Selecting from an obsolete list is a client error, and only partly a
 * detectable one: an index past the end of the current list is
 * PATHIME_ERROR_INVALID_ARGUMENT, but an index that is still in range is
 * indistinguishable from a deliberate selection and silently selects from the
 * current list. No generation counter is carried to close that gap. The API is
 * synchronous and every list replacement is announced by composition_changed
 * before the triggering call returns, so a stale index can only come from a
 * client that ignored the announcement — a bug a counter would report rather
 * than prevent.
 */
PATHIME_API pathime_status_t pathime_context_select_candidate(pathime_context_t *ctx,
                                                              size_t index);

/* ---- Client text ------------------------------------------------------ */

/**
 * Supply text near the client's insertion position as conversion context.
 *
 * @param text   Borrowed UTF-8, as a (pointer, byte length) slice of whatever
 *               buffer the client already holds. May be a fragment of the
 *               document; the engine must not treat its ends as document
 *               boundaries.
 * @param cursor Insertion position, in Unicode scalar values from the start of
 *               @a text. There is no selection anchor. Must not exceed the
 *               number of scalar values in @a text.
 *
 * Note which unit each argument is in: text.len sizes a buffer and is in bytes,
 * @a cursor locates a position and is in scalar values. That split is the
 * general rule of this API, and this is the function where getting it wrong is
 * easiest.
 *
 * The library copies what it needs. This call also establishes the frame of
 * reference for delete_surrounding_text: requests are expressed relative to
 * @a cursor and bounded by @a text, so a client that supplies surrounding text
 * must refresh it whenever the document changes. This includes changes from
 * the engine's own commit_text, but the update must happen outside the callback,
 * typically right after pathime_context_process_key() and any processing for
 * unhandled keypresses.
 *
 * The snapshot is also what an engine reads for the rules that depend on the
 * character before the insertion position — the Chinese engines decide a full
 * stop after a digit is a decimal point, and alternate a quotation mark with
 * the one before it. Supplying surrounding text makes those correct across the
 * things the engine cannot see: a caret moved, a paste, an undo, or a
 * composition that ended. Without it the engine falls back to remembering what
 * it emitted itself, which is right until one of those happens.
 *
 * That fallback is why the snapshot is read as evidence and never as the whole
 * document. What it shows is believed; what it omits decides nothing, since
 * @a text may be a fragment and the quotation mark that matters may lie before
 * its start.
 *
 * MOVING THE CARET. The library is told about a caret only through this call,
 * and a client that moves the insertion point without refreshing the snapshot
 * leaves the engine describing the wrong place — a deletion request framed
 * against text that has moved, and look-behind rules reading a character that
 * is no longer there. What to do depends on where the caret went:
 *
 *   - Away from a composition in progress: decide what the composition was
 *     worth. pathime_context_commit() keeps it, pathime_context_reset()
 *     discards it. Doing neither leaves a preedit anchored to a position the
 *     user has left.
 *   - Within the same field, with nothing composing: refresh the snapshot.
 *     Nothing else is needed, and the engine picks the new context up from it.
 *
 * See also PATHIME_REQUIRES_SURROUNDING_TEXT.
 */
PATHIME_API pathime_status_t pathime_context_set_surrounding_text(pathime_context_t *ctx,
                                                                  pathime_str_t text,
                                                                  size_t cursor);

/* ---- Lifecycle -------------------------------------------------------- */

/*
 * There is no focus concept. A context is a client destination, so which
 * destination the user is typing into is expressed by which context the
 * client sends key presses to. The client owns what happens when
 * the user leaves a field: pathime_context_commit() to keep the half-typed
 * text or pathime_context_reset() to throw it away, and then simply stop
 * offering keys. Entering another field, the client may update the engine
 * status with pathime_context_set_surrounding_text().
 *
 * A client with several fields keeps a context per field and routes to the
 * right one. The library cannot check that routing for it — knowing which
 * context should be receiving input is exactly the thing only the client
 * knows.
 */

/**
 * End the composition now, committing what it holds.
 *
 * What arrives through commit_text is exactly what
 * pathime_composition_t::preedit says would be committed if the composition
 * ended right now by pressing the engine's own commit key. No conversion the
 * user did not choose is applied: an engine showing a preview commits what
 * is on screen, not the candidate it happens to be hovering.
 *
 * This is what a client calls when the user leaves a text field and the
 * half-typed text should be kept. It is an ordinary commit that the client
 * asked for rather than a key did, which is what distinguishes it from
 * pathime_context_reset(): the engine ends up knowing that the committed text
 * is what now precedes the insertion position, so the next key is interpreted
 * in its light. A reset instead leaves the engine knowing nothing about what
 * the document holds, because after one it genuinely does not.
 *
 * An empty composition commits nothing and dispatches no callbacks, returning
 * PATHIME_OK. A client leaving a field can therefore call this unconditionally
 * rather than reading the composition first to find out whether it needs to.
 *
 * Otherwise commit_text is dispatched, then composition_changed reporting the
 * now-empty composition, in the library's usual order.
 */
PATHIME_API pathime_status_t pathime_context_commit(pathime_context_t *ctx);

/**
 * Discard transient composition state and return to a neutral state, without
 * destroying the context or its settings.
 *
 * This is a hard end. Everything the composition held is gone: the preedit,
 * the candidate list, any settled-but-uncommitted text, and whatever the
 * backing library was holding behind them. Nothing is committed and nothing
 * can be — no engine is offered a way to emit text from a reset, so there is
 * no case in which calling this puts characters in the client's document. A
 * client that wants the text calls pathime_context_commit() first.
 *
 * The engine also stops remembering what precedes the insertion position,
 * which is the observable difference from a commit: a reset means the client
 * is starting somewhere else, so context-sensitive behaviour that depends on
 * the preceding text — quote alternation, the digit look-behind that keeps
 * "1.5" intact — has nothing of its own left to go on.
 *
 * A client that supplies surrounding text gets that back. A reset followed by
 * a refreshed snapshot leaves the engine correctly informed about a *new*
 * position, which is exactly what leaving one field for another means. See
 * pathime_context_set_surrounding_text().
 *
 * Produces a composition_changed callback unless the composition was already
 * empty.
 *
 * This is also the recovery path after a failure status: it restores a context
 * whose composition state was left indeterminate to a known-empty one. On that
 * path the callback is always dispatched, because a context whose state is
 * indeterminate cannot be said to have been "already empty" and the client's
 * view of it must be replaced either way.
 */
PATHIME_API pathime_status_t pathime_context_reset(pathime_context_t *ctx);

/* =========================================================================
 * Options
 * =========================================================================
 *
 * ---------------------------------------------------------------------------
 * What is and is not an option
 * ---------------------------------------------------------------------------
 *
 * An option is a value the client sets that changes what the engine produces.
 * Three large families of setting found in the typical IME implementations are
 * deliberately absent:
 *
 *   Key bindings, candidate labels, page size, list orientation, and every
 *   other presentation choice belong to the client. docs/CONCEPTS.md excludes
 *   them from the model.
 *
 *   Direct or Latin passthrough is engine activation state, which the model
 *   excludes. A client that wants Latin stops sending key presses to the
 *   engine, and processes them itself.
 *
 * ---------------------------------------------------------------------------
 * Two levels, four tiers
 * ---------------------------------------------------------------------------
 *
 * The same options are settable on an engine and on an input context, and the
 * two are not separate namespaces. An engine value is the default its contexts
 * use; a context value overrides it for that context alone. Resolving an
 * option for a context consults, in order:
 *
 *   1. a value explicitly set on that context,
 *   2. a value explicitly set on its engine,
 *   3. the value the effective table declares, for PATHIME_ENGINE_TABLE only,
 *   4. the library default, which pathime_engine_option_info() reports.
 *
 * Tier 3 exists because a table file declares behaviour its author chose for
 * that table — whether reaching a key-run boundary auto-commits, whether
 * wildcards are implicit — and those are defaults a client should be able to
 * accept without knowing they exist, not values baked into the data. It sits
 * below the client's own values in both levels, so a client that sets an
 * option gets what it asked for, and one that does not gets what the table
 * author intended. No other engine has a tier 3.
 *
 * An engine resolves options too, through the same list minus the tier that
 * does not apply to it: 2, then 3, then 4. Its tier 3 is whatever table the
 * engine-level PATHIME_OPT_TABLE_FILE names. This is what the engine-level
 * getters report, and it is the value a context inherits when it overrides
 * nothing — but for a table option it is not necessarily what any given context
 * sees, because a context naming a different table draws tier 3 from that one
 * instead. Where a client needs the value a particular context is actually
 * using, it must ask that context.
 *
 * Resolution is late: an engine-level set changes the effective value for every
 * context that has not overridden that option, immediately, and dispatches
 * composition_changed to each of them. This is the useful behaviour — a client
 * changing one preference sees every open field follow — but it means an engine
 * setter can invoke callbacks belonging to contexts it was not passed, so
 * engine setters are not callback-safe. Getters and info queries are.
 *
 * A context that has overridden the option is untouched by all of that: tier 1
 * already wins for it, so nothing about it changed. A context can claim that
 * immunity for its whole inventory at once with
 * pathime_context_isolate_options(), which turns the engine level into a
 * template read once rather than a live influence, for the client that wants
 * exactly that.
 *
 * ---------------------------------------------------------------------------
 * When a change takes effect
 * ---------------------------------------------------------------------------
 *
 * Always immediately. Most options can be changed partway through a composition
 * without disturbing it, and are. A few cannot: switching the kana input method
 * mid-word leaves a meaningless pending romaji buffer, switching the Pinyin
 * scheme forces the backend context to be rebuilt, and switching the table
 * changes what the accumulated keys even mean. Those options reset the context
 * as pathime_context_reset() would, unconditionally and whether or not a
 * composition is in progress, and they are marked resets_composition in their
 * descriptor so a client can warn before asking.
 *
 * This is the one place options differ from each other in behaviour, and it is
 * declared as data rather than left to the reader to discover per engine.
 */

/** The type of an option's value; reported by pathime_engine_option_info(). */
typedef enum pathime_option_type {
    PATHIME_OPTION_BOOL   = 0,  /**< Set with _set_option_bool. */
    PATHIME_OPTION_INT    = 1,  /**< Integer in [min_value, max_value]. */
    PATHIME_OPTION_ENUM   = 2,  /**< One of the values in valid_values. */
    PATHIME_OPTION_FLAGS  = 3,  /**< Bitwise OR of the bits in valid_values. */
    PATHIME_OPTION_STRING = 4   /**< Set with _set_option_string. */
} pathime_option_type_t;

/**
 * Every option this library defines.
 *
 * An option is named without an engine prefix when its meaning does not depend
 * on which engine is loaded, whether or not every engine implements it —
 * PATHIME_OPT_CHINESE_VARIANT means the same thing to the Pinyin, Bopomofo and
 * table engines, and nothing at all to the other two. An option whose meaning
 * exists only inside one engine carries that engine's prefix.
 *
 * Setting an option the engine does not implement is PATHIME_ERROR_UNSUPPORTED
 * and changes nothing, so crossing engines is diagnosed rather than silently
 * ignored. pathime_engine_option_info() answers the same question in advance.
 *
 * Each option below names its type, its library default, and the engines that
 * implement it.
 *
 * Values are assigned explicitly, are dense, and are part of the ABI. The
 * headings below group options for a reader; they are not ranges. Future options
 * will take the next free value at the end of the enum. Density is a promise
 * too: it is what makes pathime_option_count() a usable way to walk every
 * option, including options a client's own header never named.
 */
typedef enum pathime_option {
    /* =====================================================================
     * Common — meaning does not depend on which engine is loaded
     * ===================================================================== */

    /**
     * INT, default PATHIME_DEFAULT_MAX_CANDIDATES, minimum 1. Anthy, Pinyin,
     * Bopomofo, Table.
     *
     * Caps how many candidates one composition produces. Some backends
     * enumerate lazily from a database with no meaningful total; rather than
     * expose that, the library materializes up to this many and presents the
     * result as the complete list.
     *
     * Composition-safe, and specifically so: a client displaying a growing list
     * raises the cap as the user scrolls. Candidates are only ever appended,
     * never reordered, so raising it cannot renumber positions already handed
     * out; lowering it removes entries from the tail.
     *
     * Zero is rejected rather than treated as a way to suppress the list.
     * Engines that convert by selection cannot make progress without a
     * candidate, so a cap of zero would deadlock the composition; a client that
     * does not want to show candidates simply does not display them.
     *
     * Hangul does not implement it, and is the one engine that produces no
     * candidates at all: libhangul composes syllables from jamo and has nothing
     * to choose between. A candidate cap there would configure a list that is
     * always empty, so the option reports itself unsupported rather than
     * accepting a value that means nothing.
     */
    PATHIME_OPT_MAX_CANDIDATES = 0,

    /**
     * BOOL, default true. Anthy, Table.
     *
     * Whether the engine adapts to what the user chooses — the learned
     * frequencies and phrases that make a candidate the user picked last time
     * appear sooner. Turning it off means nothing is written to the data
     * directory for this engine.
     *
     * Hangul does not implement it: libhangul has no learning to disable.
     *
     * Pinyin and Bopomofo do not implement it either, and report themselves
     * unsupported rather than accept a value they would ignore. Two things
     * prevent it. pyzy learns *inside* the selection and commit calls with no
     * public switch to withhold, offering only a way to unlearn one entry
     * afterwards; and its learned data is process-global, while this option is
     * per-context, so two contexts disagreeing about learning could not both
     * be honoured. Rather than half-implement it, the library says no.
     */
    PATHIME_OPT_LEARNING = 1,

    /**
     * ENUM of pathime_width_t, default PATHIME_WIDTH_HALF. Anthy, Pinyin,
     * Bopomofo, Table.
     *
     * Whether Latin letters, digits and space the engine emits are the ASCII
     * forms or their full-width CJK counterparts.
     *
     * "The engine emits" is the operative phrase, and it has a consequence
     * worth stating: for these to apply at all, the engine must handle the
     * key rather than leave it to the client. Pinyin and Bopomofo therefore
     * report every printable key handled, including the ones they pass
     * through unchanged at half width. Space is an exception: while a
     * composition is in progress it is the conversion key and emits nothing.
     */
    PATHIME_OPT_LATIN_WIDTH = 2,

    /**
     * ENUM of pathime_width_t, default PATHIME_WIDTH_FULL. Anthy, Pinyin,
     * Bopomofo, Table.
     *
     * The same choice for punctuation. It is a separate option rather than one
     * width setting because the useful combination is full-width punctuation
     * with half-width digits. The defaults are that combination.
     *
     * Full width means the language's punctuation and not merely a wider
     * glyph, so what a key produces depends on the engine and, for Chinese, on
     * PATHIME_OPT_CHINESE_VARIANT: the comma key gives 、under Anthy, ，under
     * Pinyin, and the bracket keys give 【】under simplified Chinese but 「」
     * under traditional. Where a key has no counterpart in the language — @, #,
     * % and the like — it gets the plain full-width form.
     *
     * Two substitutions depend on what came before. The quote keys alternate
     * between their opening and closing forms, and a period directly after a
     * digit stays a period so that "1.5" is not mangled. Both are reset by
     * pathime_context_reset() and can be primed with
     * pathime_context_set_surrounding_text(), as long as sufficent context to
     * the left of the cursor is provided.
     */
    PATHIME_OPT_PUNCTUATION_WIDTH = 3,

    /**
     * ENUM of pathime_chinese_variant_t, default
     * PATHIME_CHINESE_SIMPLIFIED_ONLY. Pinyin, Bopomofo, Table.
     *
     * Which Chinese character repertoire candidates are drawn from. The table
     * engine supports all five values; the Pinyin and Bopomofo engines support
     * only the two exclusive ones, because pyzy models this as a single
     * simplified-or-traditional flag with no mixed mode. That difference is
     * reported through valid_values rather than hidden, so a client can present
     * exactly the choices that will work.
     */
    PATHIME_OPT_CHINESE_VARIANT = 4,

    /**
     * BOOL, default true. Anthy, Table.
     *
     * Whether the engine volunteers candidates the user has not asked to
     * convert — the always-populated strip of a phone keyboard, which is what
     * Japanese IMEs ship under the name 予測入力.
     *
     * On Anthy it fills the candidate list from the first keystroke by
     * converting the growing reading eagerly, exactly as Gboard and the iOS
     * Japanese keyboard do. The preedit stays kana — the rule at
     * pathime_composition_t::preedit is not suspended — and the cursor
     * browses without previewing until PATHIME_KEY_SPACE asks for
     * conversion, which begins at the hovered entry. Selecting from that
     * list settles the leftmost span it describes, greedily, as selection
     * always does. Off, candidates appear only once conversion is asked
     * for, which is what every desktop Japanese IME does; the difference
     * between the two is deliberate on both sides, which is why this is an
     * option and not a fixed behaviour. On, the engine re-converts on every
     * keystroke, so the candidate list can reshuffle as input grows and
     * anthy re-segments; that churn is the measured cost of eagerness and
     * is ordinary for phrase-at-a-time typing.
     *
     * Anthy's own history-based multi-word completions are *not* what
     * this enables.
     *
     * The table engine accepts this option but currently produces nothing
     * from it. The intended meaning there is suggestion mode: after a
     * commit, continuations of what was just committed, drawn from the
     * table's suggestion data. No table this library compiles carries that
     * data, so the setting has no observable effect.
     *
     * Pinyin and Bopomofo have no row here because for them unbidden
     * candidates are not a choice: conversion by selection is the only route
     * from pinyin to Chinese text, so their list is always populated while
     * composing.
     */
    PATHIME_OPT_PREDICTION = 5,

    /**
     * BOOL, default true. Pinyin, Bopomofo.
     *
     * Whether the user-editable phrase table contributes candidates — date and
     * time macros and similar expansions.
     */
    PATHIME_OPT_SPECIAL_PHRASES = 6,

    /**
     * BOOL, default true. Pinyin, Bopomofo, Table.
     *
     * Whether a partial spelling can match a longer entry, so that "nh" reaches
     * 你好 without typing "nihao" in full. Pinyin and Bopomofo call this
     * incomplete pinyin. The choice a client is making is whether the engine
     * guesses ahead from an unfinished spelling, at the cost of a longer
     * candidate list.
     *
     * The table engine reaches the same result by appending a wildcard to the
     * key sequence before searching, but does so on its table's own
     * declaration rather than on this option: a client's value is accepted
     * and has no effect there.
     */
    PATHIME_OPT_INCOMPLETE_INPUT = 7,

    /* =====================================================================
     * Hangul
     * ===================================================================== */

    /**
     * ENUM of pathime_hangul_layout_t, default PATHIME_HANGUL_LAYOUT_2SET.
     *
     * Which jamo layout keys are interpreted against. Composition-safe:
     * libhangul changes layout without disturbing the syllable in progress.
     */
    PATHIME_OPT_HANGUL_LAYOUT = 8,

    /**
     * BOOL, default false.
     *
     * Whether jamo typed out of order still compose — typing ㅏ then ㄱ
     * yielding 가. This suits moa-chigi, the chorded style in which the keys of
     * a syllable are struck together and arrive in an arbitrary order.
     */
    PATHIME_OPT_HANGUL_AUTO_REORDER = 9,

    /**
     * BOOL, default false.
     *
     * Whether striking a consonant key twice in succession produces the tensed
     * consonant, ㄱㄱ yielding ㄲ. Only meaningful on two-set layouts; three-set
     * and Old Hangul layouts have dedicated keys for the tensed consonants, and
     * the option has no effect there.
     */
    PATHIME_OPT_HANGUL_DOUBLE_STROKE_COMBINE = 10,

    /**
     * BOOL, default true.
     *
     * Whether consonants may combine into clusters that are not valid
     * syllable-initial forms, ㄱ then ㅅ yielding ㄳ. Two-set layouts only, for
     * the same reason as above.
     */
    PATHIME_OPT_HANGUL_NON_CHOSEONG_COMBINE = 11,

    /**
     * ENUM of pathime_hangul_preedit_t, default PATHIME_HANGUL_PREEDIT_SYLLABLE.
     *
     * How much text the engine holds before committing it. libhangul exposes
     * only the syllable currently being assembled, so anything longer is
     * accumulated by this library; word mode does that, keeping finished
     * syllables in the preedit with preedit_settled marking how many are done,
     * and commits at a word boundary. The visible difference to a user is the
     * granularity of backspace and undo.
     *
     * PATHIME_HANGUL_PREEDIT_NONE is the third case and the one with
     * consequences for the client. It holds nothing at all: each jamo is
     * committed into the client's text as it is struck, and the syllable is
     * built up by deleting what was committed a moment ago and committing the
     * fuller form in its place — and a backspace, likewise, deletes the
     * committed syllable and recommits it one component shorter. It exists for
     * clients that cannot display a preedit, where the document itself is the
     * only place composition can be shown.
     *
     * That mode is the main reason this library has a surrounding-text
     * surface, and it is worth being exact about what it uses that surface
     * for. It requires both PATHIME_REQUIRES_SURROUNDING_TEXT and
     * PATHIME_REQUIRES_DELETE_SURROUNDING. The engine tracks the provisional
     * syllable it wrote, so it never reads the snapshot to learn what the
     * composing text is; it consults it only to confirm that the deletion it
     * is about to request still lands on that syllable. The snapshot is a
     * proof of a character to the left of the cursor, not a source of partial
     * syllables.
     *
     * A commit invalidates the snapshot, and this mode commits on every keystroke,
     * so the client must supply a surrounding text after every single key and not
     * merely when the caret moves. A client that does not gets the behaviour
     * described under delete_surrounding_text: the partial syllable is left in
     * the document and a new one begins. For a client that never supplies
     * surrounding text at all that happens on every jamo, so 한 arrives as ㅎㅏㄴ.
     *
     * Selecting it therefore interacts with what the client can do:
     *
     *   - Setting it on a context whose client lacks delete_surrounding_text is
     *     PATHIME_ERROR_MISSING_CALLBACK and changes nothing.
     *   - Creating a context whose engine resolves to it, from a client lacking
     *     that callback, is the same rejection from pathime_context_create().
     *   - Setting it on an engine always succeeds, since an engine has no
     *     client. For any already-existing context whose client lacks the
     *     callback, the value resolves to PATHIME_HANGUL_PREEDIT_SYLLABLE
     *     instead: the capability caps the effective value for that context
     *     alone, and its getter reports the capped value, so the client can see
     *     what it got.
     *
     * The two cases are decided differently on purpose. At creation the client
     * still has a callback table it can fix, so a rejection is useful; for a
     * context already running there is nothing left to reject, and capping is
     * the only choice that neither loses the engine's output nor makes an
     * engine-level call fail because of some unrelated context. This is the one
     * place in the API where a client capability caps an option's value rather
     * than refusing the call.
     */
    PATHIME_OPT_HANGUL_PREEDIT = 12,




    /* =====================================================================
     * Anthy
     * ===================================================================== */

    /**
     * ENUM of pathime_anthy_typing_t, default PATHIME_ANTHY_TYPING_ROMAJI.
     * Resets the composition.
     *
     * Whether keys spell kana in Latin letters or strike kana directly. anthy
     * itself has no notion of keystrokes and accepts only finished kana, so
     * both state machines belong to this library; the option chooses between
     * them. It resets because a pending romaji fragment has no meaning once the
     * keys are read as kana.
     *
     * The two read a key event differently, and a client must supply
     * pathime_key_event_t::layout_key for the second to work. Romaji entry
     * reads the character, because the user is spelling; kana entry reads the
     * *position*, because the user is striking a key whose kana legend the
     * client's keymap has no way to report. The arrangement is the standard JIS
     * kana layout mapped onto US-QWERTY positions, so a client never has to
     * describe the attached keyboard, but a client that leaves layout_key zero
     * gets the keysym instead, and on a non-US layout that will be the wrong
     * kana.
     *
     * Under kana entry the dakuten and handakuten keys combine with the kana
     * before them rather than standing alone, where one exists: か followed by
     * ゛is が. Where none exists the mark stays a character of its own.
     */
    PATHIME_OPT_ANTHY_TYPING_METHOD = 13,

    /**
     * ENUM of pathime_anthy_script_t, default PATHIME_ANTHY_SCRIPT_HIRAGANA.
     *
     * Which kana script typing produces before conversion.
     */
    PATHIME_OPT_ANTHY_KANA_SCRIPT = 14,

    /**
     * ENUM of pathime_anthy_period_t, default PATHIME_ANTHY_PERIOD_KUTEN.
     *
     * Which glyphs sentence-ending and separating punctuation produce: the
     * Japanese kuten and touten, or the full-width period and comma. This is
     * about glyph choice, not width, and is orthogonal to
     * PATHIME_OPT_PUNCTUATION_WIDTH.
     */
    PATHIME_OPT_ANTHY_PERIOD_STYLE = 15,

    /**
     * ENUM of pathime_anthy_symbol_t, default PATHIME_ANTHY_SYMBOL_CORNER_SLASH.
     *
     * Which glyphs the quoting and separator keys produce — corner brackets or
     * square brackets, solidus or middle dot. The four values are the four
     * combinations.
     */
    PATHIME_OPT_ANTHY_SYMBOL_STYLE = 16,

    /**
     * ENUM of pathime_anthy_on_period_t, default PATHIME_ANTHY_ON_PERIOD_NOTHING.
     *
     * What typing sentence-ending punctuation does beyond inserting it: nothing,
     * begin conversion, or commit outright. A workflow convenience for users who
     * would otherwise reach for the convert key at the end of every sentence.
     *
     * The set of characters that counts as sentence-ending is fixed, rather than
     * being a second option: it is the six characters , . 、 。 ， ．
     */
    PATHIME_OPT_ANTHY_ON_PERIOD = 17,

    /**
     * BOOL, default true.
     *
     * Whether holding Shift while typing kana produces Latin letters instead,
     * so a user can drop into Latin for a word without leaving kana input.
     */
    PATHIME_OPT_ANTHY_LATIN_WITH_SHIFT = 18,

    /* =====================================================================
     * Pinyin
     * ===================================================================== */

    /**
     * ENUM of pathime_pinyin_scheme_t, default PATHIME_PINYIN_SCHEME_FULL.
     * Resets the composition.
     *
     * Whether syllables are spelled out in full or compressed onto two keys,
     * and by which of the six double-pinyin schemes. The two questions are one
     * option because they are one decision to a user: which spelling they
     * memorized. It resets because pyzy fixes this when its context is created,
     * so changing it rebuilds that context.
     */
    PATHIME_OPT_PINYIN_SCHEME = 19,

    /**
     * FLAGS of PATHIME_PINYIN_FUZZY_*, default every bit. Pinyin, Bopomofo.
     *
     * Which pronunciation mergers the matcher tolerates. Each corresponds to a
     * real merger in some regional Mandarin — a speaker who does not
     * distinguish z from zh should not have to remember which one a word is
     * spelled with. Users settle on a set that reflects their own speech, which
     * is why these are independent rules rather than a tolerance level.
     *
     * Directions are separate bits: tolerating c typed for ch is not the same
     * as tolerating ch typed for c.
     */
    PATHIME_OPT_PINYIN_FUZZY = 20,

    /**
     * FLAGS of PATHIME_PINYIN_CORRECT_*, default every bit. Pinyin only.
     *
     * Which mis-spellings are silently accepted — writing "iou" for "iu", "gn"
     * for "ng", and six more. Distinct from PATHIME_OPT_PINYIN_FUZZY: these are
     * typing slips with one correct form, not pronunciations that genuinely
     * differ between speakers.
     *
     * Unlike PATHIME_OPT_PINYIN_FUZZY this one does not extend to Bopomofo, and
     * the reason is the distinction above: a correction repairs a Latin
     * spelling, and there is no Latin spelling to slip in when the syllable was
     * typed as bopomofo. Fuzzy rules do extend, because bopomofo is parsed into
     * pinyin before it is matched and the merger applies to the result.
     */
    PATHIME_OPT_PINYIN_CORRECTION = 21,

    /* =====================================================================
     * Bopomofo
     * ===================================================================== */

    /**
     * ENUM of pathime_bopomofo_layout_t, default
     * PATHIME_BOPOMOFO_LAYOUT_STANDARD. Resets the composition.
     *
     * Which arrangement of bopomofo symbols across the keys is assumed. It
     * resets because pyzy stores the new arrangement without re-reading the keys
     * already typed, so a composition spanning the change would be decoded half
     * one way and half the other.
     */
    PATHIME_OPT_BOPOMOFO_LAYOUT = 22,

    /* =====================================================================
     * Table
     * ===================================================================== */

    /**
     * STRING, no default, required. Resets the composition.
     *
     * Path to the table this context inputs from. Table engines differ only in
     * the table loaded, which is why one engine id covers Cangjie, Wubi,
     * Stroke5 and the rest; the engine caches compiled tables and shares them
     * across every context naming the same one, so per-context tables cost
     * little.
     *
     * This is also the option that supplies tier 3 for every other table
     * option: a table declares the behaviour its author chose, and those values
     * apply wherever the client has expressed no preference.
     *
     * A context with no table resolved produces nothing and reports every key
     * unhandled. Reading the option in that state yields an empty string, which
     * is how "no table" is spelled: there is no tier-4 default to fall back to,
     * and no distinction between unset and empty. Setting it to the empty string
     * returns to that state and always succeeds.
     *
     * **Setting this option loads the table**, and so is the one setter in this
     * API that reads a file and can fail on its contents: a name that resolves
     * to nothing, or to something that is not an ibus-table database, is
     * PATHIME_ERROR_BACKEND and changes nothing — the previously resolved table,
     * if any, survives. Failing here rather than at the first keystroke is
     * deliberate; it is the only point at which the failure can still be
     * attributed to the name that caused it. It also means every other table
     * option resolves against the new table's declarations immediately, with no
     * window in which they answer with library defaults instead.
     *
     * Loading is cached per engine, so naming a table a second time — from
     * another context, or after clearing the option — costs nothing.
     */
    PATHIME_OPT_TABLE_FILE = 23,

    /**
     * BOOL, default false.
     *
     * Whether a key run that has reached the table's maximum length stages its
     * current candidate automatically, rather than waiting to be selected —
     * and, separately, whether a run that matches exactly one entry commits
     * that entry outright, without waiting for the run to end. The second is
     * the one that surprises: it delivers text mid-run, at any length.
     *
     * Staging at a boundary the table's own rules declare happens either way
     * and is not this option's doing.
     *
     * Table authors treat this and PATHIME_OPT_TABLE_AUTO_SELECT as one
     * behavioural profile and set them together, so a client changing one will
     * usually want both.
     */
    PATHIME_OPT_TABLE_AUTO_COMMIT = 24,

    /**
     * BOOL, default false.
     *
     * What happens when a key run stops matching anything at all: with this
     * on, the run is rewound to its last matching form, the candidate standing
     * at that moment is selected, and the key that broke the match starts a
     * fresh run — so typing continues into the next character without an
     * explicit selection. Off, the run simply stops matching.
     *
     * It selects the candidate under the cursor, not unconditionally the
     * first, so a client's highlight is honoured here as everywhere else.
     */
    PATHIME_OPT_TABLE_AUTO_SELECT = 25,

    /**
     * STRING, default empty. One character, or empty to disable.
     *
     * The character that stands for exactly one unknown key in a search, and
     * empty when the table offers no single-character wildcard.
     *
     * Read it rather than assume it. Most tables declare no wildcard of their
     * own, but a table compiled by this library is given one where its
     * alphabet leaves room, so on most of the tables that ship this reads back
     * as a character the table's author never wrote — which is exactly why the
     * value is worth querying before showing a user how to type a wildcard.
     *
     * One position is exempt: a wildcard character at the *start* of a key run
     * is a literal key, not a wildcard. Tables that use the character as an
     * ordinary key in first position — cangjie5 reaches several hundred
     * punctuation codes that way — would otherwise lose them.
     *
     * "One character" means one Unicode scalar value. A string holding more
     * than one is PATHIME_ERROR_INVALID_ARGUMENT and changes nothing.
     *
     * A value set by a client is currently accepted and stored but does not
     * reach the search: the wildcards in force are the resolved table's.
     */
    PATHIME_OPT_TABLE_SINGLE_WILDCARD = 26,

    /**
     * STRING, default empty. One character, or empty to disable.
     *
     * The character that stands for any run of unknown keys, conventionally an
     * asterisk. One Unicode scalar value, on the same terms as
     * PATHIME_OPT_TABLE_SINGLE_WILDCARD, including that a client's own value
     * does not currently reach the search.
     */
    PATHIME_OPT_TABLE_MULTI_WILDCARD = 27,

    /**
     * BOOL, default false.
     *
     * Whether the candidate list is restricted to single characters, excluding
     * the multi-character phrases the table also holds.
     */
    PATHIME_OPT_TABLE_SINGLE_CHAR_ONLY = 28,

    /**
     * ENUM of pathime_table_invalid_t, default
     * PATHIME_TABLE_INVALID_COMMIT_CANDIDATE.
     *
     * What happens when a key arrives that the table's alphabet does not
     * contain: commit the candidate standing at that moment, or commit the raw
     * keys the user typed. The choice matters most to users who mix table input
     * with Latin text.
     *
     * It governs only keys outside the alphabet. A key the alphabet does
     * contain, arriving where it can no longer extend the run, is
     * PATHIME_OPT_TABLE_AUTO_SELECT's case rather than this one. And with no
     * candidate standing, the first value falls back to the second, since
     * there is nothing else to commit.
     */
    PATHIME_OPT_TABLE_INVALID_INPUT = 29,

    /**
     * BOOL, default false.
     *
     * Whether pinyin may be typed as a fallback when the user does not know a
     * character's table code. Turning it on requires the resolved table to have
     * been compiled with pinyin data; where it was not, setting it true is
     * PATHIME_ERROR_UNSUPPORTED.
     */
    PATHIME_OPT_TABLE_PINYIN_FALLBACK = 30
} pathime_option_t;

/** Values of PATHIME_OPT_LATIN_WIDTH and PATHIME_OPT_PUNCTUATION_WIDTH. */
typedef enum pathime_width {
    PATHIME_WIDTH_HALF = 0,
    PATHIME_WIDTH_FULL = 1
} pathime_width_t;

/** Values of PATHIME_OPT_CHINESE_VARIANT. */
typedef enum pathime_chinese_variant {
    PATHIME_CHINESE_SIMPLIFIED_ONLY   = 0,  /**< Simplified candidates only. */
    PATHIME_CHINESE_TRADITIONAL_ONLY  = 1,  /**< Traditional candidates only. */
    PATHIME_CHINESE_SIMPLIFIED_FIRST  = 2,  /**< Both, simplified ranked higher. */
    PATHIME_CHINESE_TRADITIONAL_FIRST = 3,  /**< Both, traditional ranked higher. */
    PATHIME_CHINESE_ANY               = 4   /**< Both, no variant preference. */
} pathime_chinese_variant_t;

/** Values of PATHIME_OPT_HANGUL_LAYOUT. The nine layouts libhangul builds in. */
typedef enum pathime_hangul_layout {
    PATHIME_HANGUL_LAYOUT_2SET         = 0,  /**< Dubeolsik; the common layout. */
    PATHIME_HANGUL_LAYOUT_2SET_YET     = 1,  /**< Dubeolsik Yetgeul, with Old Hangul. */
    PATHIME_HANGUL_LAYOUT_3SET_2       = 2,  /**< Sebeolsik on a two-set keyboard. */
    PATHIME_HANGUL_LAYOUT_3SET_390     = 3,  /**< Sebeolsik 390. */
    PATHIME_HANGUL_LAYOUT_3SET_FINAL   = 4,  /**< Sebeolsik Final. */
    PATHIME_HANGUL_LAYOUT_3SET_NOSHIFT = 5,  /**< Sebeolsik Noshift. */
    PATHIME_HANGUL_LAYOUT_3SET_YET     = 6,  /**< Sebeolsik Yetgeul, with Old Hangul. */
    PATHIME_HANGUL_LAYOUT_ROMAJA       = 7,  /**< Latin transliteration. */
    PATHIME_HANGUL_LAYOUT_AHNMATAE     = 8   /**< Ahnmatae. */
} pathime_hangul_layout_t;

/** Values of PATHIME_OPT_HANGUL_PREEDIT. */
typedef enum pathime_hangul_preedit {
    PATHIME_HANGUL_PREEDIT_SYLLABLE = 0,  /**< Commit each syllable as it finishes. */
    PATHIME_HANGUL_PREEDIT_WORD     = 1,  /**< Hold whole words before committing. */
    PATHIME_HANGUL_PREEDIT_NONE     = 2   /**< Hold nothing; build the syllable in the document. */
} pathime_hangul_preedit_t;

/** Values of PATHIME_OPT_ANTHY_TYPING_METHOD. */
typedef enum pathime_anthy_typing {
    PATHIME_ANTHY_TYPING_ROMAJI = 0,  /**< Spell kana in Latin letters. */
    PATHIME_ANTHY_TYPING_KANA   = 1   /**< Strike kana directly. */
} pathime_anthy_typing_t;

/** Values of PATHIME_OPT_ANTHY_KANA_SCRIPT. */
typedef enum pathime_anthy_script {
    PATHIME_ANTHY_SCRIPT_HIRAGANA           = 0,
    PATHIME_ANTHY_SCRIPT_KATAKANA           = 1,
    PATHIME_ANTHY_SCRIPT_HALFWIDTH_KATAKANA = 2
} pathime_anthy_script_t;

/** Values of PATHIME_OPT_ANTHY_PERIOD_STYLE. */
typedef enum pathime_anthy_period {
    PATHIME_ANTHY_PERIOD_KUTEN     = 0,  /**< 。 and 、 */
    PATHIME_ANTHY_PERIOD_FULLWIDTH = 1   /**< ． and ， */
} pathime_anthy_period_t;

/** Values of PATHIME_OPT_ANTHY_SYMBOL_STYLE. */
typedef enum pathime_anthy_symbol {
    PATHIME_ANTHY_SYMBOL_CORNER_SLASH   = 0,  /**< 「 」 ／ */
    PATHIME_ANTHY_SYMBOL_CORNER_MIDDOT  = 1,  /**< 「 」 ・ */
    PATHIME_ANTHY_SYMBOL_BRACKET_SLASH  = 2,  /**< ［ ］ ／ */
    PATHIME_ANTHY_SYMBOL_BRACKET_MIDDOT = 3   /**< ［ ］ ・ */
} pathime_anthy_symbol_t;

/** Values of PATHIME_OPT_ANTHY_ON_PERIOD. */
typedef enum pathime_anthy_on_period {
    PATHIME_ANTHY_ON_PERIOD_NOTHING = 0,  /**< Insert it and carry on. */
    PATHIME_ANTHY_ON_PERIOD_CONVERT = 1,  /**< Begin conversion. */
    PATHIME_ANTHY_ON_PERIOD_COMMIT  = 2   /**< Commit the composition. */
} pathime_anthy_on_period_t;

/** Values of PATHIME_OPT_PINYIN_SCHEME. */
typedef enum pathime_pinyin_scheme {
    PATHIME_PINYIN_SCHEME_FULL        = 0,  /**< Syllables spelled out in full. */
    PATHIME_PINYIN_SCHEME_DOUBLE_MSPY = 1,  /**< Microsoft double pinyin. */
    PATHIME_PINYIN_SCHEME_DOUBLE_ZRM  = 2,  /**< Ziranma. */
    PATHIME_PINYIN_SCHEME_DOUBLE_ABC  = 3,  /**< Zhineng ABC. */
    PATHIME_PINYIN_SCHEME_DOUBLE_ZGPY = 4,  /**< Zhongwen Zhixing. */
    PATHIME_PINYIN_SCHEME_DOUBLE_PYJJ = 5,  /**< Pinyin Jiajia. */
    PATHIME_PINYIN_SCHEME_DOUBLE_XHE  = 6   /**< Xiaohe. */
} pathime_pinyin_scheme_t;

/** Values of PATHIME_OPT_BOPOMOFO_LAYOUT. */
typedef enum pathime_bopomofo_layout {
    PATHIME_BOPOMOFO_LAYOUT_STANDARD   = 0,
    PATHIME_BOPOMOFO_LAYOUT_CHING_YEAH = 1,
    PATHIME_BOPOMOFO_LAYOUT_ETEN       = 2,
    PATHIME_BOPOMOFO_LAYOUT_IBM        = 3
} pathime_bopomofo_layout_t;

/** Values of PATHIME_OPT_TABLE_INVALID_INPUT. */
typedef enum pathime_table_invalid {
    PATHIME_TABLE_INVALID_COMMIT_CANDIDATE = 0,  /**< Commit the current candidate. */
    PATHIME_TABLE_INVALID_COMMIT_RAW       = 1   /**< Commit the keys as typed. */
} pathime_table_invalid_t;

/**
 * Bits of PATHIME_OPT_PINYIN_FUZZY. Each names the spelling typed and the
 * spelling it may also match, in that order, so the two directions of a merger
 * are separate bits.
 *
 * The final vowel pair governs more than it names: an/ang also covers ian/iang
 * and uan/uang, which the backend treats as one rule rather than three. Naming
 * them separately would imply a control that does not exist.
 */
enum {
    /* Initial consonants. */
    PATHIME_PINYIN_FUZZY_C_CH   = 1u << 0,
    PATHIME_PINYIN_FUZZY_CH_C   = 1u << 1,
    PATHIME_PINYIN_FUZZY_Z_ZH   = 1u << 2,
    PATHIME_PINYIN_FUZZY_ZH_Z   = 1u << 3,
    PATHIME_PINYIN_FUZZY_S_SH   = 1u << 4,
    PATHIME_PINYIN_FUZZY_SH_S   = 1u << 5,
    PATHIME_PINYIN_FUZZY_L_N    = 1u << 6,
    PATHIME_PINYIN_FUZZY_N_L    = 1u << 7,
    PATHIME_PINYIN_FUZZY_F_H    = 1u << 8,
    PATHIME_PINYIN_FUZZY_H_F    = 1u << 9,
    PATHIME_PINYIN_FUZZY_L_R    = 1u << 10,
    PATHIME_PINYIN_FUZZY_R_L    = 1u << 11,
    PATHIME_PINYIN_FUZZY_K_G    = 1u << 12,
    PATHIME_PINYIN_FUZZY_G_K    = 1u << 13,

    /* Final vowels. AN_ANG and ANG_AN also govern ian/iang and uan/uang. */
    PATHIME_PINYIN_FUZZY_AN_ANG = 1u << 14,
    PATHIME_PINYIN_FUZZY_ANG_AN = 1u << 15,
    PATHIME_PINYIN_FUZZY_EN_ENG = 1u << 16,
    PATHIME_PINYIN_FUZZY_ENG_EN = 1u << 17,
    PATHIME_PINYIN_FUZZY_IN_ING = 1u << 18,
    PATHIME_PINYIN_FUZZY_ING_IN = 1u << 19
};

/**
 * Bits of PATHIME_OPT_PINYIN_CORRECTION. Each names a mis-spelling and the
 * spelling it is taken to mean.
 */
enum {
    PATHIME_PINYIN_CORRECT_GN_NG  = 1u << 0,
    PATHIME_PINYIN_CORRECT_MG_NG  = 1u << 1,
    PATHIME_PINYIN_CORRECT_IOU_IU = 1u << 2,
    PATHIME_PINYIN_CORRECT_UEI_UI = 1u << 3,
    PATHIME_PINYIN_CORRECT_UEN_UN = 1u << 4,
    PATHIME_PINYIN_CORRECT_UE_VE  = 1u << 5,
    PATHIME_PINYIN_CORRECT_V_U    = 1u << 6,
    PATHIME_PINYIN_CORRECT_ON_ONG = 1u << 7
};

/** The candidate cap an engine starts with, absent any client value. */
#define PATHIME_DEFAULT_MAX_CANDIDATES 64

/**
 * Everything a client needs to present an option it does not know by name: its
 * type, whether this engine implements it, what values are legal, and what it
 * defaults to. This is what lets a client build a settings interface that
 * follows the inventory rather than hardcoding it.
 */
typedef struct pathime_option_info {
    /**
     * In and out, and the only member of this API's out-structs that is both.
     * The caller sets it to sizeof(pathime_option_info_t) before the call, so
     * the library knows how many bytes it may write; the library writes at most
     * that many and at most as many as it knows how to fill, then stores what
     * it actually wrote. On return it is the number of bytes the client may
     * read — never more than the caller supplied, and possibly fewer when the
     * caller's header is the newer of the two.
     *
     * A value the library does not recognize is PATHIME_ERROR_INVALID_ARGUMENT
     * and nothing is written. This is the pattern for any caller-allocated
     * struct the library fills; pathime_composition_t differs because the
     * library owns that one and the caller never allocates it.
     */
    size_t struct_size;

    pathime_option_type_t type;

    /**
     * False if this engine does not implement the option, in which case every
     * other member is unspecified and the setters return
     * PATHIME_ERROR_UNSUPPORTED.
     */
    bool supported;

    /**
     * True if setting this option discards composition state, as
     * pathime_context_reset() does. See the section header.
     */
    bool resets_composition;

    /** BOOL, INT, ENUM, FLAGS: the tier-4 library default. */
    int64_t default_value;

    /**
     * INT only: inclusive bounds. An option documented with no upper limit
     * reports INT64_MAX here, since the descriptor has no way to say "none"
     * and a client comparing against it should still get the right answer.
     */
    int64_t min_value;
    int64_t max_value;

    /**
     * ENUM: bit i is set if value i is legal for this engine. FLAGS: the set of
     * bits this engine honours. Unused for other types.
     *
     * This fixes a ceiling in the ABI, so it is stated rather than left to be
     * discovered: an ENUM option can never define a value of 64 or above, and a
     * FLAGS option can never define a 65th bit. The widest today are the nine
     * Hangul layouts and the twenty Pinyin fuzzy bits, so the room is ample —
     * but an option that would exceed it needs a different representation, not
     * a wider field here.
     */
    uint64_t valid_values;

    /**
     * STRING only: the tier-4 default, empty when the option has none.
     *
     * Static lifetime, unlike the strings the getters return: library defaults
     * are constants, so this one is not invalidated by any later call and may
     * be held indefinitely without copying.
     */
    pathime_str_t default_string;

    /**
     * STRING only: how many legal values this engine can enumerate, which
     * pathime_option_value_name() then names by index. Zero for every other
     * type, and zero for a string option whose values are not a closed set.
     *
     * This is the string counterpart of @a valid_values, and it is a count
     * rather than a bitmask for the obvious reason: the legal values are text.
     * PATHIME_OPT_TABLE_FILE is the only option with one today — the tables the
     * installation ships — and it is why this member exists.
     *
     * Unlike everything else in this struct it is *not* static: it describes
     * what was found beneath pathime_init_params_t::resource_dir, so it may
     * differ between two runs against different installations.
     */
    size_t valid_value_count;
} pathime_option_info_t;

/**
 * How many options this library defines. Option ids are dense and append-only,
 * so every option is a value in [0, pathime_option_count()) and a client can
 * walk the whole inventory:
 *
 *     for (size_t i = 0; i < pathime_option_count(); i++) {
 *         pathime_option_t opt = (pathime_option_t)i;
 *         ... pathime_option_name(opt), pathime_engine_option_info(e, opt, &info)
 *     }
 *
 * This is what makes the descriptor's promise real against a library newer than
 * the client's header: the loop reaches options the header never named, gets
 * their type and legal values from the descriptor and their storage key from
 * pathime_option_name(), and can present them without knowing what they mean.
 * A client that hardcodes its settings interface never needs this.
 *
 * A static table lookup: safe to call before pathime_init(). Callback-safe.
 */
PATHIME_API size_t pathime_option_count(void);

/**
 * A stable, machine-readable name for an option, such as "chinese-variant".
 * Suitable as a key in a client's own configuration storage; never NULL, and
 * never changes once an option ships. Not for display to end users. A value
 * that is not an option id yields "", which is never a valid option name.
 *
 * A static table lookup: safe to call before pathime_init(). Callback-safe.
 */
PATHIME_API const char *pathime_option_name(pathime_option_t option);

/**
 * A stable, machine-readable name for one *value* of an option, such as
 * "traditional-first" or "correct-gn-ng". The counterpart of
 * pathime_option_name(), and it draws the line in the same place: this is a
 * key a client maps to its own strings, not text to put in front of a user.
 * There is no localization surface in this header and should not be.
 *
 * It is what makes the inventory walk produce something a client can render.
 * Without it a client learns an option's type, bounds and legal values and
 * still cannot say what any of them is, so it either hardcodes a table of
 * labels — the very thing walking the inventory was meant to avoid — or shows
 * the user a bare number.
 *
 * @param value For PATHIME_OPTION_ENUM, the enumerator itself. For
 *              PATHIME_OPTION_FLAGS, a single bit — one of the bits set in
 *              pathime_option_info_t::valid_values, not a combination of them.
 *              For PATHIME_OPTION_STRING, a 0-based index below
 *              pathime_option_info_t::valid_value_count.
 *
 * BOOL and INT have no values worth naming and yield "". So does an option id
 * that does not exist, a value this option does not define, and a FLAGS
 * argument with more or fewer than one bit set. "" is never a valid name, so
 * one test covers all of them.
 *
 * The string case is what lets a client offer a *choice* of table rather than
 * requiring it to already know one: the names returned are exactly the values
 * PATHIME_OPT_TABLE_FILE accepts, so a picker is a loop over them and the
 * chosen entry is passed straight back to the setter.
 *
 * What comes back is a machine-readable key, the same as for every other type
 * — "cangjie5", not "CangJie5" or 倉頡第五代. The tables carry display names
 * and icons; this header does not surface them, because presentation is the
 * client's domain and there is no localization surface here. A client that
 * wants a pretty label maps the key to one of its own, exactly as it does for
 * "traditional-first".
 *
 * Together with the descriptor this enumerates from nothing: valid_values
 * gives the legal set — bit i meaning the value i for an ENUM, and the bit
 * itself for a FLAGS — and this names each one.
 *
 *     for (int bit = 0; bit < 64; bit++) {
 *         if (!(info.valid_values & (UINT64_C(1) << bit))) continue;
 *         int64_t value = info.type == PATHIME_OPTION_FLAGS
 *                             ? (int64_t)(UINT64_C(1) << bit) : bit;
 *         ... pathime_option_value_name(opt, value)
 *     }
 *
 * and for a string option the same walk is an index:
 *
 *     for (size_t i = 0; i < info.valid_value_count; i++)
 *         ... pathime_option_value_name(opt, (int64_t)i)
 *
 * A static table lookup for every type but PATHIME_OPTION_STRING, and so safe
 * to call before pathime_init() — except that a string option enumerates what
 * the installation holds, which before initialization is nothing. Callback-safe
 * throughout; the installed set is read once during pathime_init() and does not
 * change while the library is up, so the returned pointers stay valid until
 * pathime_shutdown().
 */
PATHIME_API const char *pathime_option_value_name(pathime_option_t option,
                                                  int64_t value);

/**
 * Describe @a option as this engine implements it. Fails with
 * PATHIME_ERROR_INVALID_ARGUMENT only for an unrecognized option or
 * struct_size; an option the engine does not implement is reported through
 * pathime_option_info_t::supported, not as an error. Callback-safe.
 */
PATHIME_API pathime_status_t pathime_engine_option_info(const pathime_engine_t *engine,
                                                        pathime_option_t option,
                                                        pathime_option_info_t *out_info);

/* ---- Setting ----------------------------------------------------------
 *
 * Each setter takes the value in its natural form. PATHIME_OPTION_ENUM and
 * PATHIME_OPTION_FLAGS use the int form. Calling the wrong one for an option's
 * type, or passing a value outside what the descriptor allows, is
 * PATHIME_ERROR_INVALID_ARGUMENT and changes nothing; an option this engine
 * does not implement is PATHIME_ERROR_UNSUPPORTED.
 *
 * The engine forms set the default inheriting contexts see and may dispatch
 * composition_changed to any of them, so they are not callback-safe. The
 * context forms affect one context.
 */

PATHIME_API pathime_status_t pathime_engine_set_option_bool(pathime_engine_t *engine,
                                                            pathime_option_t option,
                                                            bool value);
PATHIME_API pathime_status_t pathime_engine_set_option_int(pathime_engine_t *engine,
                                                           pathime_option_t option,
                                                           int64_t value);
PATHIME_API pathime_status_t pathime_engine_set_option_string(pathime_engine_t *engine,
                                                              pathime_option_t option,
                                                              const char *value);

PATHIME_API pathime_status_t pathime_context_set_option_bool(pathime_context_t *ctx,
                                                             pathime_option_t option,
                                                             bool value);
PATHIME_API pathime_status_t pathime_context_set_option_int(pathime_context_t *ctx,
                                                            pathime_option_t option,
                                                            int64_t value);
PATHIME_API pathime_status_t pathime_context_set_option_string(pathime_context_t *ctx,
                                                               pathime_option_t option,
                                                               const char *value);

/**
 * Drop the value explicitly set at this level, so the option resolves from the
 * next tier down again. Resetting an option that was never set is a no-op.
 * Behaves in every other respect like a setter, including resetting the
 * composition for options that require it.
 */
PATHIME_API pathime_status_t pathime_engine_reset_option(pathime_engine_t *engine,
                                                         pathime_option_t option);
PATHIME_API pathime_status_t pathime_context_reset_option(pathime_context_t *ctx,
                                                          pathime_option_t option);

/**
 * Isolate @a ctx from engine-level option changes: every option this engine
 * implements that the context has not itself set is set on the context, at its
 * current effective value. Afterwards an engine-level set or reset finds every
 * option overridden here and passes this context by — the same per-option
 * immunity any explicit override has, applied to the whole inventory at once —
 * so only calls naming this context change what it resolves.
 *
 * This exists for the client that would rather not hold the two-level model in
 * its head: a language binding wrapping contexts as self-contained objects, or
 * any caller whose callbacks must not run under an engine-level setter.
 * Configure the engine, create the context, isolate it; the engine level is
 * then a template read at isolation time rather than a live influence.
 *
 * Nothing resolves differently during the call — every value written is the
 * value already in effect, the PATHIME_OPT_HANGUL_PREEDIT capping rule
 * included — so it dispatches no callbacks and resets nothing, whichever
 * options are copied. The copies are ordinary overrides:
 * pathime_context_option_is_set() answers true for each, and
 * pathime_context_reset_option() drops one and re-attaches that option to the
 * engine like any other override. Isolation is that set of overrides, not a
 * mode; there is nothing to ask "is this context isolated", only which options
 * are set.
 *
 * On the table engine the isolation covers the table choice itself.
 * PATHIME_OPT_TABLE_FILE is copied like everything else — as the explicit
 * empty string when no table is named anywhere, so a context isolated before
 * any table was chosen does not acquire one when the engine later does; it
 * names its own or stays tableless. And the values the effective table
 * declares are copied like every other effective value, so a context-level
 * table switch after isolation changes which table is read but no longer
 * re-derives the options the old table declared; reset those options to let
 * the new table speak.
 *
 * The one failure is PATHIME_ERROR_OUT_OF_MEMORY, and it is a failure rather
 * than a rejection: options already copied stay copied. Each copy is inert on
 * its own, and calling again resumes where the failed call stopped, because
 * copied options answer is_set. Not callback-safe.
 */
PATHIME_API pathime_status_t pathime_context_isolate_options(pathime_context_t *ctx);

/* ---- Reading ----------------------------------------------------------
 *
 * Getters report the resolved effective value — what the engine is actually
 * doing — not whichever tier supplied it. A client that needs to distinguish an
 * inherited value from an overriding one asks pathime_context_option_is_set().
 * All are callback-safe.
 *
 * Strings returned are borrowed with the ordinary lifetime: valid until the
 * next call that mutates the same engine or context.
 */

PATHIME_API pathime_status_t pathime_engine_get_option_bool(const pathime_engine_t *engine,
                                                            pathime_option_t option,
                                                            bool *out_value);
PATHIME_API pathime_status_t pathime_engine_get_option_int(const pathime_engine_t *engine,
                                                           pathime_option_t option,
                                                           int64_t *out_value);
PATHIME_API pathime_status_t pathime_engine_get_option_string(const pathime_engine_t *engine,
                                                              pathime_option_t option,
                                                              pathime_str_t *out_value);

PATHIME_API pathime_status_t pathime_context_get_option_bool(const pathime_context_t *ctx,
                                                             pathime_option_t option,
                                                             bool *out_value);
PATHIME_API pathime_status_t pathime_context_get_option_int(const pathime_context_t *ctx,
                                                            pathime_option_t option,
                                                            int64_t *out_value);
PATHIME_API pathime_status_t pathime_context_get_option_string(const pathime_context_t *ctx,
                                                               pathime_option_t option,
                                                               pathime_str_t *out_value);

/**
 * True iff a value for @a option was explicitly set at this level, as opposed
 * to inherited from a lower tier. For a settings interface distinguishing "this
 * context overrides the default" from "this context follows it".
 *
 * False for everything the question cannot be asked of: a NULL handle, a value
 * that is not an option id, an option this engine does not implement, and any
 * call made before pathime_init() has succeeded. There is no error channel
 * because the useful reading of all of those is the same — no value has been
 * set here. Callback-safe.
 */
PATHIME_API bool pathime_engine_option_is_set(const pathime_engine_t *engine,
                                              pathime_option_t option);
PATHIME_API bool pathime_context_option_is_set(const pathime_context_t *ctx,
                                               pathime_option_t option);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* PATHIME_H */
