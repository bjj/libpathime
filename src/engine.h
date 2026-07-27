/*
 * The engine object — the middle layer of the two-layer lifetime (TODO.md §2,
 * Finding 3): one input method implementation plus whatever it shares across
 * every context using it.
 *
 * This header defines `struct pathime_engine` because three core files need to
 * reach inside it and none of them is engine.cc: options.cc reads and writes
 * the engine level of the two-level store, context.cc reads the engine's id
 * and registers itself for the engine-level broadcast, and candidates.cc
 * reaches the backend through it.
 *
 * The registry declared here is the other half of engine.cc's job. It is gated
 * on the PATHIME_WITH_* macros from <pathime/config.h> and is deliberately not
 * one-to-one with them: pyzy supplies both PATHIME_ENGINE_PINYIN and
 * PATHIME_ENGINE_BOPOMOFO.
 */

#ifndef LIBPATHIME_SRC_ENGINE_H
#define LIBPATHIME_SRC_ENGINE_H

#include <vector>

#include <pathime/pathime.h>

#include "options.h"

namespace pathime {

/**
 * True if pathime_engine_create() can supply @a id in this build *and* the
 * process is initialized. This is exactly pathime_has_engine()'s answer, and
 * the reason it is false for everything before pathime_init(): an engine's
 * runtime prerequisites — dictionaries, databases, the keyboard registry — are
 * what pathime_init() opens, so before it there is nothing to report on.
 */
bool engine_available(pathime_engine_id_t id);

}  // namespace pathime

/**
 * An engine handle. Comparatively expensive, shared by every context using the
 * same input method — see the public documentation on pathime_engine_t.
 *
 * Defined in the global namespace because that is where the public header's
 * `typedef struct pathime_engine pathime_engine_t` declares the tag; this
 * completes that same incomplete type.
 */
struct pathime_engine {
    /** Which input method. Reported unchanged by pathime_engine_id(). */
    pathime_engine_id_t id;

    /**
     * The engine level of the two-level store: tier 2, the default its
     * contexts see wherever they override nothing.
     */
    pathime::OptionStore options;

    /**
     * Every live context created from this engine, in creation order.
     *
     * Resolution is late, and this list is what makes that real: an
     * engine-level set changes the effective value for every context that has
     * not overridden the option, immediately, and dispatches
     * composition_changed to each of them. That is also why engine setters are
     * not callback-safe — they can invoke callbacks belonging to contexts the
     * caller never passed.
     *
     * Contexts add themselves on creation and remove themselves on
     * destruction; the public contract already requires every context to be
     * destroyed before its engine, so this never dangles.
     */
    std::vector<pathime_context_t *> contexts;

    /*
     * TODO(impl): the backend's engine-level state — the one thing an adapter
     * owns at this layer — goes here as whatever handle backend.h ends up
     * defining. Its shape waits on the composition representation
     * (TODO.md §3, question 1); see backend.h.
     */
};

#endif /* LIBPATHIME_SRC_ENGINE_H */
