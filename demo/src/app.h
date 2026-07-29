/*
 * The demo's model: everything it knows about libpathime, and everything
 * libpathime has told it. render.cc draws this and touches no API of its own.
 *
 * The program is a *client* in the sense docs/CONCEPTS.md means: it owns a
 * document, it offers key presses to an engine, and it applies whatever the
 * engine asks it to do to that document. The three obligations that come with
 * that are all discharged here and nowhere else:
 *
 *   - the pathime_client_t callback table, whose commit_text and
 *     delete_surrounding_text edit the document, and whose
 *     composition_changed is what tells the program to redraw;
 *   - refreshing the surrounding-text snapshot after *every* dispatch, not
 *     merely when a caret moves, which is what
 *     PATHIME_REQUIRES_SURROUNDING_TEXT actually demands;
 *   - deciding what an unhandled key means, since a key the engine declines is
 *     the client's own to act on.
 *
 * One engine per input method and one input context per engine, all created up
 * front and kept alive; switching input methods only changes which context
 * this program offers keys to, rather than rebuilding anything. That is the
 * arrangement the header recommends, and it has a property worth watching in
 * the demo: a context that is not being keyed is not told so and does not
 * care, so an unfinished composition is still there when you switch back.
 */

#ifndef PATHIME_DEMO_APP_H
#define PATHIME_DEMO_APP_H

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include <cpp-terminal/key.hpp>
#include <pathime/pathime.h>

#include "options_view.h"

namespace demo {

/** One input method: the engine, and the context this program drives it with. */
struct EngineSlot {
    pathime_engine_id_t id;
    const char *name;      /**< "Korean · Hangul" */
    const char *hint;      /**< What to type to see something happen. */
    pathime_engine_t *engine;
    pathime_context_t *ctx;
};

/** Which half of the screen the keyboard is talking to. */
enum class Pane { Input, Options };

/**
 * How much surrounding text this program offers the engine.
 *
 * A real client has one answer and keeps it. This one cycles, because the
 * three answers produce visibly different engine behaviour and that difference
 * is otherwise invisible — the snapshot is the one input to the library a user
 * cannot see on screen.
 *
 * `Fragment` is the instructive middle. The header says the supplied text may
 * be a fragment whose ends are not document boundaries, and that an engine must
 * therefore read it as evidence rather than as the whole truth. One scalar is
 * enough for the digit look-behind, which needs only the character before the
 * caret, and never enough for the quote alternation, which has to find the last
 * quotation mark. So the two rules visibly part company here.
 */
enum class Surrounding {
    None,      /**< Never call. The engine keeps whatever it last had. */
    Fragment,  /**< One scalar before the caret. */
    Full       /**< The whole document. */
};

/**
 * What a line of the event log is, which is the whole point of the log: the
 * traffic across the API boundary runs in both directions, and a reader who
 * cannot tell which way a line went cannot learn anything from it.
 */
enum class LogKind {
    Call,      /**< This program calling into libpathime, and what it returned. */
    Callback,  /**< libpathime calling back into this program. */
    Info       /**< This program talking to itself. Not API traffic at all. */
};

struct LogEntry {
    LogKind kind;
    std::string text;
    /**
     * Identifies the entry for note_result(), which fills a call's outcome in
     * after the call returns — by which time the callbacks it caused have been
     * logged beneath it. A sequence number rather than an index because the log
     * is trimmed from the front.
     */
    std::uint64_t id;
};

class App {
public:
    App();
    ~App();

    App(const App &) = delete;
    App &operator=(const App &) = delete;

    /**
     * Create an engine and a context for every engine this build can supply.
     * Returns false only when there is not a single one, in which case
     * @a error explains what to check.
     */
    bool open(const std::string &initial_engine, std::string *error);

    /** Handle one key press. */
    void on_key(const Term::Key &key);

    /**
     * Handle pasted text.
     *
     * Nothing goes to the engine: a paste is not typing, and the API has no
     * way to offer text to an engine anyway — pathime_context_process_key()
     * takes one key press. So this is an ordinary client-side insertion, and
     * the composition in progress is left alone.
     */
    void on_paste(const std::string &text);

    bool done() const { return done_; }

    /* ---- What render.cc reads. Plain accessors; the demo keeps its view
     * layer read-only rather than handing it the API handles. ---- */

    const std::vector<EngineSlot> &engines() const { return engines_; }
    std::size_t active_index() const { return active_; }
    const EngineSlot &active() const { return engines_[active_]; }

    /** The document this program is the text field of. */
    const std::string &document() const { return document_; }

    /** The active context's composition. Never null once open() succeeded. */
    const pathime_composition_t *composition() const;

    /** Candidate @a index of the current list, or "" if out of range. */
    std::string candidate(std::size_t index) const;

    std::size_t page() const { return page_; }
    std::size_t page_size() const { return kPageSize; }

    const std::deque<LogEntry> &log() const { return log_; }
    const std::string &status() const { return status_; }
    const std::string &last_key() const { return last_key_; }

    /**
     * Text this program has been asked to put on the system clipboard, taken
     * and cleared. Empty when there is nothing to copy.
     *
     * The App decides *what* to copy; getting it into the clipboard is a
     * terminal escape sequence and belongs to the layer that writes to the
     * terminal, which is why this is a request rather than an action.
     */
    std::string take_clipboard();

    Pane pane() const { return pane_; }
    bool engine_level() const { return engine_level_; }
    bool help_visible() const { return help_; }
    Surrounding surrounding() const { return surrounding_; }
    const std::vector<OptionRow> &options() const { return options_; }
    std::size_t option_index() const { return option_index_; }

    /** Option row @a index, resolved and formatted at the level being edited. */
    std::string option_value_text(std::size_t index) const;
    /** True if row @a index has a value set at the level being edited. */
    bool is_option_set_here(std::size_t index) const;
    /**
     * True if the context overrides row @a index. Worth showing while the
     * engine's values are being edited, because a context value wins over
     * them: without it, an engine-level change that the context shadows looks
     * like it did nothing.
     */
    bool is_option_shadowed(std::size_t index) const;

    /** Requirement bits of the active engine, as PATHIME_REQUIRES_*. */
    std::uint32_t requirements() const;

    static const std::size_t kPageSize = 9;

private:
    /* ---- pathime_client_t. Static, because the library calls C function
     * pointers; each recovers the App from user_data. ---- */
    static void on_commit(void *user_data, pathime_str_t text);
    static void on_delete(void *user_data, ptrdiff_t offset, std::size_t count);
    static void on_changed(void *user_data, const pathime_composition_t *composition);

    void input_key(const Term::Key &key);
    void options_key(const Term::Key &key);
    bool hotkey(const Term::Key &key);

    /** The client's own handling of a key the engine declined. */
    void unhandled_key(const pathime_key_event_t &event);

    void select_candidate(std::size_t index);
    void page_forward();
    void page_back();
    /** Raise PATHIME_OPT_MAX_CANDIDATES by one page. Returns the new count. */
    std::size_t grow_candidate_list();

    /**
     * Move the hover by @a delta entries, growing the list and turning the
     * page as needed. The library takes an absolute index and offers no
     * next/previous, because which key moves which way — and whether either
     * end wraps — is the client's to decide. This is where this client decides
     * it.
     */
    void move_cursor(int delta);

    /** Move the hover to an absolute index, and follow it with the page. */
    void move_cursor_to(std::size_t index);

    /** Scroll so that the page on screen is the one holding the cursor. */
    void page_to_cursor();

    void set_active(std::size_t index);
    void refresh_surrounding_text();
    /** Cycle how much surrounding text is supplied, and say so. */
    void cycle_surrounding();

    /* ---- The log. One entry-point per direction, so that no call site can
     * accidentally record traffic as the wrong kind. ---- */

    /** A call into the library. Returns an id for note_result(). */
    std::uint64_t note_call(const std::string &line);
    /** The outcome of the call @a id, appended to that entry in place. */
    void note_result(std::uint64_t id, pathime_status_t status,
                     const std::string &extra = std::string());
    /** A callback from the library. */
    void note_callback(const std::string &line);
    /** This program's own commentary. */
    void note_info(const std::string &line);

    void note_status(const char *what, pathime_status_t status);

    pathime_context_t *ctx() { return engines_.empty() ? nullptr : engines_[active_].ctx; }
    const pathime_context_t *ctx() const { return engines_.empty() ? nullptr : engines_[active_].ctx; }
    pathime_engine_t *engine() { return engines_.empty() ? nullptr : engines_[active_].engine; }

    std::vector<EngineSlot> engines_;
    std::size_t active_ = 0;
    pathime_client_t client_{};

    std::string document_;

    /* The surrounding-text snapshot, as last given to the library. The
     * delete_surrounding_text frame of reference: requests are relative to
     * snapshot_cursor_ and bounded by snapshot_, and a document that has moved
     * on since is exactly the case the header says a client may decline. */
    std::string snapshot_;
    std::size_t snapshot_cursor_ = 0;
    Surrounding surrounding_ = Surrounding::Full;

    std::size_t page_ = 0;
    std::deque<LogEntry> log_;
    std::uint64_t next_log_id_ = 1;
    std::string status_;
    std::string last_key_;
    std::string clipboard_;

    Pane pane_ = Pane::Input;
    bool engine_level_ = false;  /**< false: edit options on the context. */
    bool help_ = false;
    bool done_ = false;
    std::vector<OptionRow> options_;
    std::size_t option_index_ = 0;
};

}  // namespace demo

#endif /* PATHIME_DEMO_APP_H */
