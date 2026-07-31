/*
 * pathime-demo — an interactive IME in a terminal, and a client of
 * <pathime/pathime.h> in the exact sense docs/CONCEPTS.md means.
 *
 * What it is for: watching the library work. Type Korean, Japanese or Chinese
 * into a text field this program owns, and see beside it the preedit, how much
 * of it the engine considers settled, the candidate list,
 * every callback the library made and in what order, and the whole option
 * inventory with everything the current engine implements editable at either
 * level. Nothing here is a test — tests/ does that — and nothing here is
 * required to build the library.
 *
 * This file is the bootstrap: process arguments, initialize, run the event
 * loop, shut down. Everything else is app.cc (the client), render.cc (the
 * screen) and keymap.cc (the one piece of platform glue a terminal makes
 * necessary).
 */

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include <cpp-terminal/event.hpp>
#include <cpp-terminal/input.hpp>
#include <cpp-terminal/iostream.hpp>
#include <cpp-terminal/key.hpp>
#include <cpp-terminal/options.hpp>
#include <cpp-terminal/screen.hpp>
#include <cpp-terminal/terminal.hpp>

#include <pathime/pathime.h>

#include "app.h"
#include "render.h"

namespace {

void usage(const char *argv0)
{
    std::printf(
        "usage: %s [options]\n"
        "\n"
        "  --engine NAME   start on hangul, anthy, pinyin, bopomofo or table\n"
        "  --data-dir DIR  where the engines keep what they learn\n"
        "  --list          print which engines this build can supply, and exit\n"
        "  --help          this\n",
        argv0);
}

int list_engines()
{
    static const pathime_engine_id_t kIds[] = {
        PATHIME_ENGINE_HANGUL, PATHIME_ENGINE_ANTHY, PATHIME_ENGINE_PINYIN,
        PATHIME_ENGINE_BOPOMOFO, PATHIME_ENGINE_TABLE};
    for (pathime_engine_id_t id : kIds) {
        std::printf("  %-9s %s\n", pathime_engine_name(id),
                    pathime_has_engine(id) ? "available" : "not available");
    }
    return 0;
}

/** The loop: read one event, act on it, redraw. */
void run(demo::App *app)
{
    while (!app->done()) {
        const Term::Screen screen = Term::screen_size();
        Term::cout << demo::render(*app, screen.rows(), screen.columns())
                   << std::flush;

        const Term::Event event = Term::read_event();
        switch (event.type()) {
        case Term::Event::Type::Key:
            app->on_key(static_cast<Term::Key>(event));
            break;
        case Term::Event::Type::CopyPaste:
            /* A terminal reports any burst of characters this way, so this is
             * both a real paste and what a fast typist's key repeat looks
             * like. Either way it is text the client received, not a key an
             * engine could have composed with. */
            app->on_paste(static_cast<std::string>(event));
            break;
        case Term::Event::Type::Screen:
        case Term::Event::Type::Empty:
        case Term::Event::Type::Focus:
            /* A resize or a focus change only needs the redraw at the top of
             * the loop. Terminal focus means nothing to the library, which has
             * no such concept: the input context keeps whatever this program
             * gave it. */
            break;
        default:
            break;
        }

        /* Anything the key just handled asked to be copied, on its way to the
         * clipboard. Here rather than in the App because it is a write to the
         * terminal, and the App does not have one. */
        const std::string clip = app->take_clipboard();
        if (!clip.empty()) Term::cout << demo::clipboard_copy(clip) << std::flush;
    }
}

}  // namespace

int main(int argc, char **argv)
{
    std::string engine;
    std::string data_dir;
    bool list = false;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "--list") {
            list = true;
        } else if (arg == "--engine" && i + 1 < argc) {
            engine = argv[++i];
        } else if (arg == "--data-dir" && i + 1 < argc) {
            data_dir = argv[++i];
        } else {
            std::fprintf(stderr, "unrecognized argument: %s\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }

#ifdef PATHIME_DEMO_DATA_DIR
    if (data_dir.empty()) data_dir = PATHIME_DEMO_DATA_DIR;
#endif

    /* Initialized in the declaration, which is the idiom the header asks for:
     * struct_size says every member is filled in, and the initializer is what
     * makes that true of the ones this program has no opinion about. */
    pathime_init_params_t params = {
        sizeof params,
        data_dir.empty() ? nullptr : data_dir.c_str(),
    };

    /* The one call in the API that may take a perceptible amount of time: it
     * opens the on-disk dictionaries. A backend whose data is missing is not
     * fatal here — it makes pathime_has_engine() false for that engine alone. */
    const pathime_status_t st = pathime_init(&params);
    if (st != PATHIME_OK) {
        std::fprintf(stderr, "pathime_init: %s\n", pathime_status_string(st));
        return 1;
    }

    if (list) {
        const int rc = list_engines();
        pathime_shutdown();
        return rc;
    }

    int rc = 0;
    {
        demo::App app;
        std::string error;
        if (!app.open(engine, &error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            pathime_shutdown();
            return 1;
        }

        try {
            /*
             * Raw so every key press arrives as itself, no signal keys so
             * Ctrl+C is a key like any other, and the alternate screen so the
             * user's scrollback survives.
             */
            Term::terminal.setOptions(Term::Option::Raw, Term::Option::NoSignalKeys,
                                      Term::Option::ClearScreen, Term::Option::Cursor);

            /*
             * Undo one thing Option::Raw did on our behalf. cpp-terminal turns
             * mouse reporting on with raw mode and offers no way to decline it,
             * and a terminal that is forwarding drags to the application is not
             * selecting text with them — so a user could not select the CJK
             * text this program had just produced. Nothing here wants mouse
             * events; Ctrl+Y copies the document for terminals where selection
             * is awkward anyway.
             *
             * This is written once, after setOptions(), because nothing in the
             * loop re-applies the terminal options. On a legacy Windows console
             * it does not help — there mouse input is a console mode flag
             * cpp-terminal sets directly, and QuickEdit is off besides — which
             * is what Ctrl+Y is really for.
             */
            Term::cout << demo::disable_mouse_reporting() << std::flush;

            run(&app);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "terminal: %s\n", e.what());
            rc = 1;
        } catch (...) {
            std::fprintf(stderr, "terminal: unknown error\n");
            rc = 1;
        }
    }  /* App destroyed here: every context and engine gone before shutdown. */

    pathime_shutdown();
    return rc;
}
