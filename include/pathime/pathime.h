/*
 * libpathime — public C API (DRAFT 3: core loop only)
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
 *     pathime_has_engine, pathime_engine_requirements,
 *     pathime_context_composition, pathime_context_candidate,
 *     pathime_option_name, pathime_engine_option_info,
 *     pathime_engine_get_option_*, pathime_context_get_option_*
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
 * Generated at configure time. Defines PATHIME_WITH_HANGUL, PATHIME_WITH_ANTHY
 * and PATHIME_WITH_PYZY as 0 or 1, according to which backends this build
 * actually contains, so that clients can compile out unavailable paths.
 * The matching runtime query is pathime_has_engine().
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
 */

#define PATHIME_VERSION_MAJOR 0
#define PATHIME_VERSION_MINOR 1
#define PATHIME_VERSION_PATCH 0

#define PATHIME_VERSION_STRING "0.1.0"

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
 * _MISSING_CALLBACK, _UNSUPPORTED, _NOT_INITIALIZED and _NOT_FOCUSED are all
 * rejections.
 *
 * Failures happen partway through, when a backend or an allocation gives out
 * with work already done. The library cannot describe how far it got, and
 * makes no claim that callbacks already dispatched are consistent with the
 * state left behind. The context remains a valid handle but its composition
 * state is indeterminate: call pathime_context_reset() before trusting or
 * displaying anything from it. PATHIME_ERROR_OUT_OF_MEMORY and
 * PATHIME_ERROR_BACKEND are failures.
 */
typedef enum pathime_status {
    PATHIME_OK = 0,

    /* Rejections — nothing happened. */
    PATHIME_ERROR_INVALID_ARGUMENT,  /**< NULL handle, bad index, bad UTF-8, bad struct_size. */
    PATHIME_ERROR_UNKNOWN_ENGINE,    /**< Engine not available in this library. */
    PATHIME_ERROR_MISSING_CALLBACK,  /**< Client lacks a callback the engine requires. */
    PATHIME_ERROR_UNSUPPORTED,       /**< Engine does not implement this operation. */
    PATHIME_ERROR_NOT_INITIALIZED,   /**< pathime_init() has not been called. */
    PATHIME_ERROR_NOT_FOCUSED,       /**< Context is not focused; see pathime_context_set_focused(). */

    /* Failures — composition state is indeterminate until reset. */
    PATHIME_ERROR_OUT_OF_MEMORY,
    PATHIME_ERROR_BACKEND            /**< Backend library or data file failure. */
} pathime_status_t;

/**
 * Human-readable, English, static description of a status code. Never NULL.
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
 */
typedef struct pathime_init_params {
    /** Set to sizeof(pathime_init_params_t). */
    size_t struct_size;

    /**
     * A directory the library may read and write, holding every piece of
     * per-user state any engine accumulates: learned word frequencies,
     * user-defined phrases, personal dictionaries, and backend caches. The
     * client owns this path and controls its lifetime; the library creates the
     * directory and whatever structure it needs beneath it.
     *
     * This is the whole of the library's persistent-storage surface. The
     * backends would each otherwise pick their own location from the
     * environment — anthy from HOME or XDG_CONFIG_HOME plus a "personality"
     * name, pyzy from XDG_CACHE_HOME and XDG_CONFIG_HOME, and the table engine
     * from wherever its user database lives. All of them are redirected here
     * instead, so a client that wants a second independent profile supplies a
     * second directory rather than reaching for a per-engine identity setting.
     *
     * Notably this is what lets anthy's "personality" disappear from the API.
     * It is process-global and write-once in anthy's public interface; by
     * making the directory the identity we set it once, to a fixed name, and
     * never contend with that restriction.
     *
     * A NUL-terminated filesystem path, in UTF-8 on every platform. This is the
     * one string in the API that is not a (pointer, length) pair: it names a
     * file rather than carrying text, and no caller has a reason to pass a
     * slice of one. NULL selects a platform-appropriate default beneath the
     * user's configuration directory. Borrowed for the duration of the call.
     */
    const char *data_dir;
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
 * @param params May be NULL, which is equivalent to a zero-initialized struct
 *               with only struct_size set: every default applies.
 *
 * Calling it twice without an intervening shutdown is an error. Every function
 * in this header except the version queries requires it to have succeeded.
 */
PATHIME_API pathime_status_t pathime_init(const pathime_init_params_t *params);

/**
 * Release process-global state. All engines and input contexts must already
 * have been destroyed.
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
 * only in the table loaded, and which table that is will be an engine option
 * rather than a separate id. Its implementation lives in this library — see
 * docs/ibus-table-spec.md — and is not written yet, so builds currently report
 * it absent through pathime_has_engine() and PATHIME_WITH_TABLE.
 */
typedef enum pathime_engine_id {
    PATHIME_ENGINE_HANGUL = 0,  /**< Korean Hangul composition. */
    PATHIME_ENGINE_ANTHY,       /**< Japanese kana-kanji conversion. */
    PATHIME_ENGINE_PINYIN,      /**< Chinese, Pinyin phonetic input. */
    PATHIME_ENGINE_BOPOMOFO,    /**< Chinese, Bopomofo/Zhuyin phonetic input. */
    PATHIME_ENGINE_TABLE        /**< Table-driven input from a loaded table. */
} pathime_engine_id_t;

/**
 * True iff pathime_engine_create() can currently supply @a id. False both for
 * an engine this library does not contain and for one whose runtime
 * prerequisites, such as its dictionaries, are unavailable. Callback-safe.
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

/** Bits returned by pathime_engine_requirements(). */
enum {
    /**
     * The engine cannot work correctly unless the client keeps
     * pathime_context_set_surrounding_text() up to date.
     */
    PATHIME_REQUIRES_SURROUNDING_TEXT = 1u << 0,

    /**
     * The engine will ask the client to delete text it has already inserted,
     * so pathime_client::delete_surrounding_text must be supplied.
     */
    PATHIME_REQUIRES_DELETE_SURROUNDING = 1u << 1
};

/**
 * What this engine needs from its client, as a bitwise OR of
 * PATHIME_REQUIRES_*.
 *
 * Requirements depend on how the engine is configured — Hangul, for instance,
 * needs both surrounding text and deletion only when Hanja conversion is
 * enabled — so query this after configuring the engine and before creating
 * contexts. pathime_context_create() enforces it: a client missing a required
 * callback is rejected with PATHIME_ERROR_MISSING_CALLBACK rather than
 * silently losing engine output. Callback-safe.
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
    PATHIME_KEY_MUHENKAN          = 0xff22,  /**< Cancel conversion. */
    PATHIME_KEY_HENKAN            = 0xff23,  /**< Begin/advance conversion. */
    PATHIME_KEY_HIRAGANA_KATAKANA = 0xff27,  /**< Toggle kana script. */

    /* Korean keys, present on Korean keyboards. */
    PATHIME_KEY_HANGUL       = 0xff31,  /**< Toggle Hangul input. */
    PATHIME_KEY_HANGUL_HANJA = 0xff34   /**< Begin Hanja conversion. */
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
 * docs/CONCEPTS.md: preedit text, auxiliary text, and a candidate list.
 *
 * This is a snapshot owned by the input context, with the ordinary lifetime:
 * it and everything it reaches stay valid until the next call that mutates
 * that context. Copy anything that must outlive that.
 *
 * Empty preedit, empty auxiliary text, and a zero candidate count each mean
 * "not currently present".
 */
typedef struct pathime_composition {
    /**
     * Filled in by the library with the size of the struct it knows how to
     * write. A client compiled against a newer header than the loaded library
     * must not read past this many bytes.
     */
    size_t struct_size;

    /** Provisional, uncommitted text. Plain UTF-8, no attributes. */
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

    /** Supplemental text; never committed. May be empty. */
    pathime_str_t auxiliary;

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
     */
    size_t candidate_count;
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
 * belongs to it: composition state, surrounding text, focus, and per-context
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
 * A new context starts unfocused, with empty composition data, no surrounding
 * text, and a candidate cap of PATHIME_DEFAULT_MAX_CANDIDATES.
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

/* ---- Key input -------------------------------------------------------- */

/**
 * Offer a key press to the engine. Requires the context to be focused;
 * otherwise returns PATHIME_ERROR_NOT_FOCUSED and does nothing.
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
 * Callback-safe: candidates are fully materialized before composition_changed
 * is dispatched, so this reads an array and never re-enters a backend.
 */
PATHIME_API pathime_status_t pathime_context_candidate(const pathime_context_t *ctx,
                                                       size_t index,
                                                       pathime_str_t *out);

/**
 * Tell the engine the client has chosen the candidate at absolute position
 * @a index in the most recently supplied candidate list. Requires the context
 * to be focused; otherwise returns PATHIME_ERROR_NOT_FOCUSED and does nothing.
 *
 * Selection is greedy and resolves left to right: choosing a candidate settles
 * the portion of the composition it covers, extends the settled region of the
 * preedit, and produces a fresh candidate list for whatever input remains. The
 * client never navigates or resizes segments; when nothing remains, the engine
 * commits. All resulting output is dispatched before this returns.
 *
 * Selecting from an obsolete list is a client error and returns
 * PATHIME_ERROR_INVALID_ARGUMENT when the index is out of range.
 */
PATHIME_API pathime_status_t pathime_context_select_candidate(pathime_context_t *ctx,
                                                              size_t index);

/* ---- Client text ------------------------------------------------------ */

/**
 * Supply text near the client's insertion position as conversion context.
 *
 * @param text   Borrowed UTF-8. May be a fragment of the document; the engine
 *               must not treat its ends as document boundaries.
 * @param len    Length of @a text in bytes.
 * @param cursor Insertion position, in Unicode scalar values from the start of
 *               @a text. There is no selection anchor. Must not exceed the
 *               number of scalar values in @a text.
 *
 * The library copies what it needs. This call also establishes the frame of
 * reference for delete_surrounding_text: requests are expressed relative to
 * @a cursor and bounded by @a text, so a client that supplies surrounding text
 * must refresh it whenever the document changes, or the engine will be
 * reasoning about text that is no longer there.
 */
PATHIME_API pathime_status_t pathime_context_set_surrounding_text(pathime_context_t *ctx,
                                                                  const char *text,
                                                                  size_t len,
                                                                  size_t cursor);

/* ---- Lifecycle -------------------------------------------------------- */

/**
 * Report whether this context is the client destination currently receiving
 * input. A new context is unfocused; a client driving a single text field
 * should focus it once after creation.
 *
 * Focus gates input, and only input: an unfocused context rejects
 * pathime_context_process_key() and pathime_context_select_candidate() with
 * PATHIME_ERROR_NOT_FOCUSED, so a client that forgets to focus gets a
 * diagnosable error rather than silence. Everything else — reading the
 * composition, supplying surrounding text, changing settings, resetting —
 * works regardless of focus.
 *
 * Losing focus neither commits nor discards. Composition state is preserved
 * exactly, so refocusing resumes where the user left off, and no callbacks are
 * dispatched. A client that wants the preedit finalized or thrown away when
 * the user leaves the field decides that for itself, before dropping focus.
 * Redundant transitions are no-ops.
 */
PATHIME_API pathime_status_t pathime_context_set_focused(pathime_context_t *ctx, bool focused);

/**
 * Discard transient composition state and return to a neutral state, without
 * destroying the context or its settings. Does not commit preedit text; an
 * engine that must preserve text commits it explicitly as part of handling the
 * reset. Produces a composition_changed callback unless the composition was
 * already empty.
 *
 * This is also the recovery path after a failure status: it restores a context
 * whose composition state was left indeterminate to a known-empty one.
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
 * Three large families of setting found in the reference implementations are
 * deliberately absent, and their absence is not an oversight:
 *
 *   Key bindings, candidate labels, page size, list orientation, and every
 *   other presentation choice belong to the client. docs/CONCEPTS.md excludes
 *   them from the model.
 *
 *   Direct or Latin passthrough is engine activation state, which the model
 *   excludes as distinct from focus. A client that wants Latin stops offering
 *   keys to the engine.
 *
 *   Encodings, dictionary paths, and backend init parameters are consumed
 *   internally. Where they carry a genuine client decision it has been lifted
 *   out: see pathime_init_params_t::data_dir.
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
 * Resolution is late: an engine-level set changes the effective value for every
 * context that has not overridden that option, immediately, and dispatches
 * composition_changed to each of them. This is the useful behaviour — a client
 * changing one preference sees every open field follow — but it means an engine
 * setter can invoke callbacks belonging to contexts it was not passed, so
 * engine setters are not callback-safe. Getters and info queries are.
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
    PATHIME_OPTION_BOOL,    /**< Set with _set_option_bool. */
    PATHIME_OPTION_INT,     /**< Integer in [min_value, max_value]. */
    PATHIME_OPTION_ENUM,    /**< One of the values in valid_values. */
    PATHIME_OPTION_FLAGS,   /**< Bitwise OR of the bits in valid_values. */
    PATHIME_OPTION_STRING   /**< Set with _set_option_string. */
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
 */
typedef enum pathime_option {
    /* =====================================================================
     * Common — meaning does not depend on which engine is loaded
     * ===================================================================== */

    /**
     * INT, default PATHIME_DEFAULT_MAX_CANDIDATES, minimum 1. All engines.
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
     */
    PATHIME_OPT_MAX_CANDIDATES = 0,

    /**
     * BOOL, default true. Anthy, Pinyin, Bopomofo, Table.
     *
     * Whether the engine adapts to what the user chooses — the learned
     * frequencies and phrases that make a candidate the user picked last time
     * appear sooner. Turning it off means nothing is written to the data
     * directory for this engine.
     *
     * Only the table engine has a native switch for this. Anthy and pyzy learn
     * unconditionally on their commit paths, so for those the library
     * implements the option by withholding the learning commit.
     *
     * Hangul does not implement it: libhangul has no learning to disable.
     */
    PATHIME_OPT_LEARNING,

    /**
     * ENUM of pathime_width_t, default PATHIME_WIDTH_HALF. Anthy, Pinyin,
     * Bopomofo, Table.
     *
     * Whether Latin letters, digits and space the engine emits are the ASCII
     * forms or their full-width CJK counterparts.
     */
    PATHIME_OPT_LATIN_WIDTH,

    /**
     * ENUM of pathime_width_t, default PATHIME_WIDTH_FULL. Anthy, Pinyin,
     * Bopomofo, Table.
     *
     * The same choice for punctuation. It is a separate option rather than one
     * width setting because the useful combination is full-width punctuation
     * with half-width digits, and every reference engine models these two
     * independently for that reason. The defaults are that combination.
     */
    PATHIME_OPT_PUNCTUATION_WIDTH,

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
    PATHIME_OPT_CHINESE_VARIANT,

    /**
     * BOOL, default false. Anthy, Table.
     *
     * Whether the engine offers continuations of what has been typed so far, in
     * addition to conversions of it. Anthy's prediction and the table engine's
     * suggestions are the same feature under two names.
     */
    PATHIME_OPT_PREDICTION,

    /**
     * BOOL, default true. Pinyin, Bopomofo.
     *
     * Whether the user-editable phrase table contributes candidates — date and
     * time macros and similar expansions.
     */
    PATHIME_OPT_SPECIAL_PHRASES,

    /**
     * BOOL, default true. Pinyin, Bopomofo, Table.
     *
     * Whether a partial spelling can match a longer entry, so that "nh" reaches
     * 你好 without typing "nihao" in full. Pinyin and Bopomofo call this
     * incomplete pinyin; the table engine reaches the same result by appending
     * a wildcard to the key sequence before searching. They are one option
     * because the choice a client is making is identical: whether the engine
     * guesses ahead from an unfinished spelling, at the cost of a longer
     * candidate list.
     */
    PATHIME_OPT_INCOMPLETE_INPUT,

    /* =====================================================================
     * Hangul
     * ===================================================================== */

    /**
     * ENUM of pathime_hangul_layout_t, default PATHIME_HANGUL_LAYOUT_2SET.
     *
     * Which jamo layout keys are interpreted against. Composition-safe:
     * libhangul changes layout without disturbing the syllable in progress.
     */
    PATHIME_OPT_HANGUL_LAYOUT,

    /**
     * BOOL, default false.
     *
     * Whether jamo typed out of order still compose — typing ㅏ then ㄱ
     * yielding 가. This suits moa-chigi, the chorded style in which the keys of
     * a syllable are struck together and arrive in an arbitrary order.
     */
    PATHIME_OPT_HANGUL_AUTO_REORDER,

    /**
     * BOOL, default false.
     *
     * Whether striking a consonant key twice in succession produces the tensed
     * consonant, ㄱㄱ yielding ㄲ. Only meaningful on two-set layouts; three-set
     * and Old Hangul layouts have dedicated keys for the tensed consonants, and
     * the option has no effect there.
     */
    PATHIME_OPT_HANGUL_DOUBLE_STROKE_COMBINE,

    /**
     * BOOL, default true.
     *
     * Whether consonants may combine into clusters that are not valid
     * syllable-initial forms, ㄱ then ㅅ yielding ㄳ. Two-set layouts only, for
     * the same reason as above.
     */
    PATHIME_OPT_HANGUL_NON_CHOSEONG_COMBINE,

    /**
     * ENUM of pathime_hangul_preedit_t, default PATHIME_HANGUL_PREEDIT_SYLLABLE.
     *
     * How much text the engine holds before committing it. libhangul exposes
     * only the syllable currently being assembled, so anything longer is
     * accumulated by this library; word mode does that, keeping finished
     * syllables in the preedit with preedit_settled marking how many are done,
     * and commits at a word boundary. The visible difference to a user is the
     * granularity of backspace and undo.
     */
    PATHIME_OPT_HANGUL_PREEDIT,

    /**
     * BOOL, default false.
     *
     * Whether committed Hangul can be converted to Hanja, which is the only
     * source of candidates this engine has. Enabling it changes what the engine
     * requires of its client: conversion reads the text already committed and
     * replaces it, so both surrounding text and delete_surrounding_text become
     * necessary, and pathime_engine_requirements() reports them only while this
     * is on.
     *
     * Setting it on a context whose client lacks delete_surrounding_text is
     * PATHIME_ERROR_MISSING_CALLBACK and changes nothing — the same check
     * pathime_context_create() makes, applied at the point the requirement
     * appears. Setting it on an engine succeeds regardless, since an engine has
     * no client; contexts created afterward are checked as they are created.
     */
    PATHIME_OPT_HANGUL_HANJA,

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
     */
    PATHIME_OPT_ANTHY_TYPING_METHOD,

    /**
     * ENUM of pathime_anthy_script_t, default PATHIME_ANTHY_SCRIPT_HIRAGANA.
     *
     * Which kana script typing produces before conversion.
     */
    PATHIME_OPT_ANTHY_KANA_SCRIPT,

    /**
     * ENUM of pathime_anthy_period_t, default PATHIME_ANTHY_PERIOD_KUTEN.
     *
     * Which glyphs sentence-ending and separating punctuation produce: the
     * Japanese kuten and touten, or the full-width period and comma. This is
     * about glyph choice, not width, and is orthogonal to
     * PATHIME_OPT_PUNCTUATION_WIDTH.
     */
    PATHIME_OPT_ANTHY_PERIOD_STYLE,

    /**
     * ENUM of pathime_anthy_symbol_t, default PATHIME_ANTHY_SYMBOL_CORNER_SLASH.
     *
     * Which glyphs the quoting and separator keys produce — corner brackets or
     * square brackets, solidus or middle dot. The four values are the four
     * combinations.
     */
    PATHIME_OPT_ANTHY_SYMBOL_STYLE,

    /**
     * ENUM of pathime_anthy_on_period_t, default PATHIME_ANTHY_ON_PERIOD_NOTHING.
     *
     * What typing sentence-ending punctuation does beyond inserting it: nothing,
     * begin conversion, or commit outright. A workflow convenience for users who
     * would otherwise reach for the convert key at the end of every sentence.
     *
     * The set of characters that counts as sentence-ending is fixed, rather than
     * being a second option: it is the six characters , . 、 。 ， ． and no
     * plausible client needs to change it.
     */
    PATHIME_OPT_ANTHY_ON_PERIOD,

    /**
     * BOOL, default true.
     *
     * Whether holding Shift while typing kana produces Latin letters instead,
     * so a user can drop into Latin for a word without leaving kana input.
     */
    PATHIME_OPT_ANTHY_LATIN_WITH_SHIFT,

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
    PATHIME_OPT_PINYIN_SCHEME,

    /**
     * FLAGS of PATHIME_PINYIN_FUZZY_*, default every bit.
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
    PATHIME_OPT_PINYIN_FUZZY,

    /**
     * FLAGS of PATHIME_PINYIN_CORRECT_*, default every bit.
     *
     * Which mis-spellings are silently accepted — writing "iou" for "iu", "gn"
     * for "ng", and six more. Distinct from PATHIME_OPT_PINYIN_FUZZY: these are
     * typing slips with one correct form, not pronunciations that genuinely
     * differ between speakers.
     */
    PATHIME_OPT_PINYIN_CORRECTION,

    /**
     * BOOL, default false.
     *
     * Whether the auxiliary text shows the keys as typed alongside the syllables
     * they decoded to. Useful while learning a double-pinyin scheme; meaningless
     * under PATHIME_PINYIN_SCHEME_FULL, where the two are the same text.
     */
    PATHIME_OPT_PINYIN_SHOW_RAW,

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
    PATHIME_OPT_BOPOMOFO_LAYOUT,

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
     * unhandled.
     */
    PATHIME_OPT_TABLE_FILE,

    /**
     * BOOL, default false.
     *
     * Whether a key sequence that can no longer be extended stages its best
     * match automatically, rather than waiting to be selected. Table authors
     * treat this and PATHIME_OPT_TABLE_AUTO_SELECT as one behavioural profile
     * and set them together, so a client changing one will usually want both.
     */
    PATHIME_OPT_TABLE_AUTO_COMMIT,

    /**
     * BOOL, default false.
     *
     * Whether the first candidate is selected implicitly when the key sequence
     * reaches its maximum length, so that typing continues into the next
     * character without an explicit selection.
     */
    PATHIME_OPT_TABLE_AUTO_SELECT,

    /**
     * STRING, default empty. One character, or empty to disable.
     *
     * The character that stands for exactly one unknown key in a search. Empty
     * means the table offers no single-character wildcard, which is the common
     * case: of the tables surveyed only one defines it.
     */
    PATHIME_OPT_TABLE_SINGLE_WILDCARD,

    /**
     * STRING, default empty. One character, or empty to disable.
     *
     * The character that stands for any run of unknown keys, conventionally an
     * asterisk.
     */
    PATHIME_OPT_TABLE_MULTI_WILDCARD,

    /**
     * BOOL, default false.
     *
     * Whether the candidate list is restricted to single characters, excluding
     * the multi-character phrases the table also holds.
     */
    PATHIME_OPT_TABLE_SINGLE_CHAR_ONLY,

    /**
     * ENUM of pathime_table_invalid_t, default
     * PATHIME_TABLE_INVALID_COMMIT_CANDIDATE.
     *
     * What happens when a key arrives that the table's alphabet does not
     * contain: commit the candidate standing at that moment, or commit the raw
     * keys the user typed. The choice matters most to users who mix table input
     * with Latin text.
     */
    PATHIME_OPT_TABLE_INVALID_INPUT,

    /**
     * BOOL, default false.
     *
     * Whether pinyin may be typed as a fallback when the user does not know a
     * character's table code. Turning it on requires the resolved table to have
     * been compiled with pinyin data; where it was not, setting it true is
     * PATHIME_ERROR_UNSUPPORTED.
     */
    PATHIME_OPT_TABLE_PINYIN_FALLBACK
} pathime_option_t;

/** Values of PATHIME_OPT_LATIN_WIDTH and PATHIME_OPT_PUNCTUATION_WIDTH. */
typedef enum pathime_width {
    PATHIME_WIDTH_HALF = 0,
    PATHIME_WIDTH_FULL
} pathime_width_t;

/** Values of PATHIME_OPT_CHINESE_VARIANT. */
typedef enum pathime_chinese_variant {
    PATHIME_CHINESE_SIMPLIFIED_ONLY = 0,  /**< Simplified candidates only. */
    PATHIME_CHINESE_TRADITIONAL_ONLY,     /**< Traditional candidates only. */
    PATHIME_CHINESE_SIMPLIFIED_FIRST,     /**< Both, simplified ranked higher. */
    PATHIME_CHINESE_TRADITIONAL_FIRST,    /**< Both, traditional ranked higher. */
    PATHIME_CHINESE_ANY                   /**< Both, no variant preference. */
} pathime_chinese_variant_t;

/** Values of PATHIME_OPT_HANGUL_LAYOUT. The nine layouts libhangul builds in. */
typedef enum pathime_hangul_layout {
    PATHIME_HANGUL_LAYOUT_2SET = 0,     /**< Dubeolsik; the common layout. */
    PATHIME_HANGUL_LAYOUT_2SET_YET,     /**< Dubeolsik Yetgeul, with Old Hangul. */
    PATHIME_HANGUL_LAYOUT_3SET_2,       /**< Sebeolsik on a two-set keyboard. */
    PATHIME_HANGUL_LAYOUT_3SET_390,     /**< Sebeolsik 390. */
    PATHIME_HANGUL_LAYOUT_3SET_FINAL,   /**< Sebeolsik Final. */
    PATHIME_HANGUL_LAYOUT_3SET_NOSHIFT, /**< Sebeolsik Noshift. */
    PATHIME_HANGUL_LAYOUT_3SET_YET,     /**< Sebeolsik Yetgeul, with Old Hangul. */
    PATHIME_HANGUL_LAYOUT_ROMAJA,       /**< Latin transliteration. */
    PATHIME_HANGUL_LAYOUT_AHNMATAE      /**< Ahnmatae. */
} pathime_hangul_layout_t;

/** Values of PATHIME_OPT_HANGUL_PREEDIT. */
typedef enum pathime_hangul_preedit {
    PATHIME_HANGUL_PREEDIT_SYLLABLE = 0, /**< Commit each syllable as it finishes. */
    PATHIME_HANGUL_PREEDIT_WORD          /**< Hold whole words before committing. */
} pathime_hangul_preedit_t;

/** Values of PATHIME_OPT_ANTHY_TYPING_METHOD. */
typedef enum pathime_anthy_typing {
    PATHIME_ANTHY_TYPING_ROMAJI = 0, /**< Spell kana in Latin letters. */
    PATHIME_ANTHY_TYPING_KANA        /**< Strike kana directly. */
} pathime_anthy_typing_t;

/** Values of PATHIME_OPT_ANTHY_KANA_SCRIPT. */
typedef enum pathime_anthy_script {
    PATHIME_ANTHY_SCRIPT_HIRAGANA = 0,
    PATHIME_ANTHY_SCRIPT_KATAKANA,
    PATHIME_ANTHY_SCRIPT_HALFWIDTH_KATAKANA
} pathime_anthy_script_t;

/** Values of PATHIME_OPT_ANTHY_PERIOD_STYLE. */
typedef enum pathime_anthy_period {
    PATHIME_ANTHY_PERIOD_KUTEN = 0,  /**< 。 and 、 */
    PATHIME_ANTHY_PERIOD_FULLWIDTH   /**< ． and ， */
} pathime_anthy_period_t;

/** Values of PATHIME_OPT_ANTHY_SYMBOL_STYLE. */
typedef enum pathime_anthy_symbol {
    PATHIME_ANTHY_SYMBOL_CORNER_SLASH = 0,  /**< 「 」 ／ */
    PATHIME_ANTHY_SYMBOL_CORNER_MIDDOT,     /**< 「 」 ・ */
    PATHIME_ANTHY_SYMBOL_BRACKET_SLASH,     /**< ［ ］ ／ */
    PATHIME_ANTHY_SYMBOL_BRACKET_MIDDOT     /**< ［ ］ ・ */
} pathime_anthy_symbol_t;

/** Values of PATHIME_OPT_ANTHY_ON_PERIOD. */
typedef enum pathime_anthy_on_period {
    PATHIME_ANTHY_ON_PERIOD_NOTHING = 0, /**< Insert it and carry on. */
    PATHIME_ANTHY_ON_PERIOD_CONVERT,     /**< Begin conversion. */
    PATHIME_ANTHY_ON_PERIOD_COMMIT       /**< Commit the composition. */
} pathime_anthy_on_period_t;

/** Values of PATHIME_OPT_PINYIN_SCHEME. */
typedef enum pathime_pinyin_scheme {
    PATHIME_PINYIN_SCHEME_FULL = 0,     /**< Syllables spelled out in full. */
    PATHIME_PINYIN_SCHEME_DOUBLE_MSPY,  /**< Microsoft double pinyin. */
    PATHIME_PINYIN_SCHEME_DOUBLE_ZRM,   /**< Ziranma. */
    PATHIME_PINYIN_SCHEME_DOUBLE_ABC,   /**< Zhineng ABC. */
    PATHIME_PINYIN_SCHEME_DOUBLE_ZGPY,  /**< Zhongwen Zhixing. */
    PATHIME_PINYIN_SCHEME_DOUBLE_PYJJ,  /**< Pinyin Jiajia. */
    PATHIME_PINYIN_SCHEME_DOUBLE_XHE    /**< Xiaohe. */
} pathime_pinyin_scheme_t;

/** Values of PATHIME_OPT_BOPOMOFO_LAYOUT. */
typedef enum pathime_bopomofo_layout {
    PATHIME_BOPOMOFO_LAYOUT_STANDARD = 0,
    PATHIME_BOPOMOFO_LAYOUT_CHING_YEAH,
    PATHIME_BOPOMOFO_LAYOUT_ETEN,
    PATHIME_BOPOMOFO_LAYOUT_IBM
} pathime_bopomofo_layout_t;

/** Values of PATHIME_OPT_TABLE_INVALID_INPUT. */
typedef enum pathime_table_invalid {
    PATHIME_TABLE_INVALID_COMMIT_CANDIDATE = 0, /**< Commit the current candidate. */
    PATHIME_TABLE_INVALID_COMMIT_RAW            /**< Commit the keys as typed. */
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
     * Filled in by the library with the size of the struct it knows how to
     * write. A client compiled against a newer header must not read past it.
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

    /** INT only: inclusive bounds. */
    int64_t min_value;
    int64_t max_value;

    /**
     * ENUM: bit i is set if value i is legal for this engine. FLAGS: the set of
     * bits this engine honours. Unused for other types.
     */
    uint64_t valid_values;

    /** STRING only: the tier-4 default, empty when the option has none. */
    pathime_str_t default_string;
} pathime_option_info_t;

/**
 * A stable, machine-readable name for an option, such as "chinese-variant".
 * Suitable as a key in a client's own configuration storage; never NULL, and
 * never changes once an option ships. Not for display to end users.
 * Callback-safe.
 */
PATHIME_API const char *pathime_option_name(pathime_option_t option);

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
 * context overrides the default" from "this context follows it". Callback-safe.
 */
PATHIME_API bool pathime_engine_option_is_set(const pathime_engine_t *engine,
                                              pathime_option_t option);
PATHIME_API bool pathime_context_option_is_set(const pathime_context_t *ctx,
                                               pathime_option_t option);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* PATHIME_H */
