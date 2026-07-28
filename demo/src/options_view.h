/*
 * The options panel: reading the option inventory, formatting a value, and
 * changing one.
 *
 * Written the way the header describes rather than the way a shipping client
 * would: it walks [0, pathime_option_count()) and presents whatever that
 * inventory contains, so an option added to the library after this program was
 * compiled would still appear, be typed correctly, and be editable. A real
 * settings screen would hardcode the options it cares about and never need
 * this; the demo does it the general way because the general way is the thing
 * worth demonstrating.
 *
 * The one thing it cannot get from the library is a *display* name for an
 * option or an enum value. That is deliberate on the library's side — labels
 * and presentation belong to the client — so the tables below are the demo's
 * own, and anything they do not cover falls back to the machine-readable name
 * from pathime_option_name() and the bare number.
 */

#ifndef PATHIME_DEMO_OPTIONS_VIEW_H
#define PATHIME_DEMO_OPTIONS_VIEW_H

#include <string>
#include <vector>

#include <pathime/pathime.h>

namespace demo {

/** One row of the options panel: an option this engine implements. */
struct OptionRow {
    pathime_option_t option;
    pathime_option_info_t info;
};

/** Every option @a engine reports supported, in inventory order. */
std::vector<OptionRow> collect_options(const pathime_engine_t *engine);

/**
 * The option's resolved value at the level being edited, formatted for
 * display: "true", "64", "kuten", "0x000fffff (20 of 20 bits)".
 *
 * @a engine_level chooses which getter answers — the engine's resolved value
 * (tiers 2, 3, 4) or the context's (tiers 1, 2, 3, 4). They differ exactly
 * when the context overrides the option, which is the thing the panel is
 * showing.
 */
std::string value_text(const OptionRow &row,
                       const pathime_engine_t *engine,
                       const pathime_context_t *ctx,
                       bool engine_level);

/**
 * Change the option at the given level.
 *
 * @param step  +1/-1 to move through an enum's legal values or an int's range,
 *              larger magnitudes to move an int faster; 0 to toggle a bool or
 *              flip a flags option between all its bits and none.
 *
 * Strings are the one type this does not edit, and the reason is that the only
 * string option is PATHIME_OPT_TABLE_FILE, which belongs to an engine this
 * build cannot contain yet. It returns PATHIME_ERROR_UNSUPPORTED, and the
 * panel says so.
 */
pathime_status_t adjust_option(const OptionRow &row,
                               pathime_engine_t *engine,
                               pathime_context_t *ctx,
                               bool engine_level,
                               int step);

/** Drop the value set at this level, so the option resolves from below again. */
pathime_status_t reset_option(const OptionRow &row,
                              pathime_engine_t *engine,
                              pathime_context_t *ctx,
                              bool engine_level);

/** True if a value is explicitly set at this level, rather than inherited. */
bool is_set_here(const OptionRow &row,
                 const pathime_engine_t *engine,
                 const pathime_context_t *ctx,
                 bool engine_level);

}  // namespace demo

#endif /* PATHIME_DEMO_OPTIONS_VIEW_H */
