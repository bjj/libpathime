/*
 * Drawing. Reads the App and calls no libpathime function of its own, so
 * everything on screen is something the client was told rather than something
 * the view went and asked for.
 */

#ifndef PATHIME_DEMO_RENDER_H
#define PATHIME_DEMO_RENDER_H

#include <cstddef>
#include <string>

#include "app.h"

namespace demo {

/**
 * One complete frame for a terminal of @a rows by @a columns, ending with the
 * cursor left where the user's caret is. Written as a single string and
 * printed in one write, which is what keeps a full redraw from flickering.
 */
std::string render(const App &app, std::size_t rows, std::size_t columns);

/**
 * The escape sequence that puts @a text on the system clipboard: OSC 52, which
 * a terminal may or may not honour — xterm, kitty, wezterm, iTerm2, Windows
 * Terminal and tmux (with `set-clipboard on`) do. A terminal that does not
 * ignores it, so this can be written unconditionally.
 *
 * It exists because a full-screen program in raw mode is an awkward place to
 * select text from, and the whole output of this one is text a user may want
 * elsewhere.
 */
std::string clipboard_copy(const std::string &text);

/**
 * The escape sequence that turns mouse reporting back off.
 *
 * cpp-terminal enables it — `?1002h ?1003h ?1006h` — as part of `Option::Raw`,
 * with no option to decline, and a terminal that is forwarding drags to the
 * application is not selecting text with them. Since this program never asked
 * for mouse events and ignores the ones it is sent, turning them off costs
 * nothing and gives the user back the terminal's own selection.
 */
std::string disable_mouse_reporting();

}  // namespace demo

#endif /* PATHIME_DEMO_RENDER_H */
