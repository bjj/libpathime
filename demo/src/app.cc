#include "app.h"

#include <cstdio>
#include <cstring>

#include "keymap.h"
#include "text.h"

namespace demo {
namespace {

/*
 * The engines this program offers, in the order they appear in the header, with
 * something to type in each. Every one of them is offered only if
 * pathime_has_engine() says so, which is false both for an engine this build
 * does not contain and for one whose dictionaries are missing.
 */
struct EngineDef {
    pathime_engine_id_t id;
    const char *key;   /**< What --engine accepts. */
    const char *name;
    const char *hint;
};

const EngineDef kEngineDefs[] = {
    {PATHIME_ENGINE_HANGUL,   "hangul",   "Korean · Hangul",
     "type  gksrnr  for \xED\x95\x9C\xEA\xB5\xAD"},
    {PATHIME_ENGINE_ANTHY,    "anthy",    "Japanese · Anthy",
     "type  nihon  then Space to convert"},
    {PATHIME_ENGINE_PINYIN,   "pinyin",   "Chinese · Pinyin",
     "type  nihao  then Space, or 1-9 to pick"},
    {PATHIME_ENGINE_BOPOMOFO, "bopomofo", "Chinese · Bopomofo",
     "type  su3cl3  then Space; digits are tones here, so Alt+1-9 picks"},
    /*
     * Two tables rather than one, because the engine's whole point is that it
     * is the same code driving unrelated methods — and because the codes look
     * nothing alike, which is the part a hint can show and prose cannot.
     * Both spell 我; cangjie5 keys by radical shape, wubi by stroke groups.
     */
    {PATHIME_ENGINE_TABLE,    "table",    "Table-driven",
     "set table-file: cangjie5, type  hqi  \xE2\x80\x94 or wubi-jidian86, type  trn"},
};

/*
 * How much history the log keeps. Generous, because it is not what limits what
 * is on screen — render.cc gives the log as many rows as it has entries to
 * fill, up to two thirds of what is left over. This is the ceiling on
 * scrollback, and one full key press is three or four entries.
 */
const std::size_t kLogLines = 40;

}  // namespace

App::App() = default;

App::~App()
{
    /* Contexts first: an engine may not be destroyed until every context
     * created from it is gone. */
    for (EngineSlot &slot : engines_) {
        pathime_context_destroy(slot.ctx);
        pathime_engine_destroy(slot.engine);
    }
}

bool App::open(const std::string &initial_engine, std::string *error)
{
    client_.struct_size = sizeof(client_);
    client_.commit_text = &App::on_commit;
    client_.delete_surrounding_text = &App::on_delete;
    client_.composition_changed = &App::on_changed;

    for (const EngineDef &def : kEngineDefs) {
        if (!pathime_has_engine(def.id)) continue;

        pathime_engine_t *engine = nullptr;
        pathime_status_t st = pathime_engine_create(def.id, &engine);
        if (st != PATHIME_OK) continue;

        /*
         * The callback table is a member of this object and outlives every
         * context created from it, which is what the header requires: it is
         * borrowed by pointer and must stay valid and unchanged for the
         * context's lifetime.
         *
         * Supplying all three callbacks is also what lets every option be
         * reachable from the panel — PATHIME_HANGUL_PREEDIT_NONE needs
         * delete_surrounding_text, and a client without it would be refused
         * that value rather than shown it.
         */
        pathime_context_t *ctx = nullptr;
        st = pathime_context_create(engine, &client_, this, &ctx);
        if (st != PATHIME_OK) {
            pathime_engine_destroy(engine);
            continue;
        }
        engines_.push_back(EngineSlot{def.id, def.name, def.hint, engine, ctx});
    }

    if (engines_.empty()) {
        *error =
            "no engine is available.\n"
            "pathime_has_engine() is false for every id, which means either "
            "this build contains no backend or none of them could find its "
            "runtime data.\n"
            "Run the demo from the build tree it was built in — see "
            "demo/README.md.";
        return false;
    }

    if (!initial_engine.empty()) {
        for (std::size_t i = 0; i < engines_.size(); i++) {
            for (const EngineDef &def : kEngineDefs) {
                if (def.id == engines_[i].id && initial_engine == def.key)
                    active_ = i;
            }
        }
    }

    options_ = collect_options(engine());
    refresh_surrounding_text();
    note_info("ready — F1 for help");
    return true;
}

/* ---- The client callbacks ---------------------------------------------- */

void App::on_commit(void *user_data, pathime_str_t text)
{
    App *app = static_cast<App *>(user_data);
    /* Deletions are dispatched before commits precisely so the document can be
     * put right before anything is inserted into it. */
    app->document_.append(text.bytes, text.len);
    app->note_callback("commit_text \"" +
                       escape_for_log(std::string(text.bytes, text.len)) + "\"");
}

void App::on_delete(void *user_data, ptrdiff_t offset, std::size_t count)
{
    App *app = static_cast<App *>(user_data);

    char buf[96];
    std::snprintf(buf, sizeof(buf), "delete_surrounding_text offset=%td count=%zu",
                  offset, count);
    std::string line = buf;

    /*
     * The range is expressed against the snapshot last given to
     * pathime_context_set_surrounding_text(), with the origin at the cursor
     * that call reported — not against wherever the document has since got to.
     * A client whose document has moved on may decline rather than delete the
     * wrong thing, and saying so out loud is more useful in a demo than
     * silently doing nothing.
     */
    /*
     * The snapshot is a *suffix* of the document, not necessarily all of it —
     * under Surrounding::Fragment it is one scalar. So the test is whether the
     * document still ends with what was supplied, which is the fragment-aware
     * form of "has the document moved on since". Comparing the two whole
     * strings would decline every deletion the moment a fragment was offered.
     */
    if (app->document_.size() < app->snapshot_.size() ||
        app->document_.compare(app->document_.size() - app->snapshot_.size(),
                               app->snapshot_.size(), app->snapshot_) != 0) {
        app->note_callback(line + "  — declined, snapshot is stale");
        return;
    }

    const ptrdiff_t start = static_cast<ptrdiff_t>(app->snapshot_cursor_) + offset;
    if (start < 0 ||
        static_cast<std::size_t>(start) + count > scalar_count(app->snapshot_)) {
        app->note_callback(line + "  — declined, outside the snapshot");
        return;
    }

    /*
     * Applied on the spot. The header guarantees at most one
     * delete_surrounding_text per dispatch, so there is never an earlier
     * deletion whose coordinates this one would invalidate — and all deletions
     * arrive before any commit, so the document is in the state the engine
     * described before anything is inserted into it.
     *
     * @a start above is a position in the *snapshot*, which is only the same
     * position in the document when the snapshot is the whole document. The
     * translation is this simple because both end at the caret: an offset back
     * from the end means the same thing in either. Doing the arithmetic in the
     * document's own terms is what makes a fragment work.
     */
    const ptrdiff_t doc_start =
        static_cast<ptrdiff_t>(scalar_count(app->document_)) + offset;
    erase_scalars(&app->document_, static_cast<std::size_t>(doc_start), count);
    app->note_callback(line);
}

void App::on_changed(void *user_data, const pathime_composition_t *composition)
{
    App *app = static_cast<App *>(user_data);
    char buf[224];
    /*
     * Only the non-mutating queries may be called from inside a callback, and
     * reading the composition is one of them — this is the same object
     * pathime_context_composition() returns. The demo copies nothing here: it
     * re-reads the composition when it draws, which is what the borrowed
     * lifetime is for.
     *
     * Every field of the struct is logged, cursor included, because the cursor
     * is the field a reader is most likely to think the client owns. Seeing it
     * arrive on a callback the client did not trigger — the second press of
     * Space under anthy — is what shows it does not.
     */
    std::snprintf(buf, sizeof(buf),
                  "composition_changed preedit=\"%s\" settled=%zu candidates=%zu "
                  "cursor=%zu",
                  escape_for_log(std::string(composition->preedit.bytes,
                                             composition->preedit.len)).c_str(),
                  composition->preedit_settled, composition->candidate_count,
                  composition->candidate_cursor);
    app->note_callback(buf);
}

/* ---- Key handling ------------------------------------------------------- */

void App::on_key(const Term::Key &key)
{
    status_.clear();
    last_key_ = key_label(key);
    if (hotkey(key)) return;
    if (pane_ == Pane::Options) {
        options_key(key);
        return;
    }
    input_key(key);
}

void App::on_paste(const std::string &text)
{
    status_.clear();
    document_ += text;
    refresh_surrounding_text();
    last_key_ = "paste of " + std::to_string(scalar_count(text)) + " characters";
}

bool App::hotkey(const Term::Key &key)
{
    if (key == Term::Key::Ctrl_Q || key == Term::Key::Ctrl_C) {
        done_ = true;
        return true;
    }
    if (help_) {
        /* Any other key closes the help page; it is a page, not a mode. */
        help_ = false;
        return true;
    }

    switch (key.value) {
    case Term::Key::Ctrl_E:
        set_active((active_ + 1) % engines_.size());
        return true;

    case Term::Key::Ctrl_R: {
        /* Discards composition state without committing it, and the engine
         * stops knowing what precedes the caret. The other half of the pair is
         * Ctrl+T. */
        const std::uint64_t call = note_call("context_reset");
        const pathime_status_t st = pathime_context_reset(ctx());
        note_result(call, st);
        note_status("context_reset", st);
        page_ = 0;
        refresh_surrounding_text();
        return true;
    }

    case Term::Key::Ctrl_T: {
        /*
         * The other half: end the composition and keep the text. This is what
         * a real client does when the user leaves a field, and the log is
         * where the difference from Ctrl+R shows — a commit_text comes back
         * out under the call, where a reset produces only the
         * composition_changed.
         *
         * Called unconditionally, without first asking whether anything is
         * composing: an empty composition is a no-op with no callbacks, which
         * is exactly so that a client need not ask.
         */
        const std::uint64_t call = note_call("context_commit");
        const pathime_status_t st = pathime_context_commit(ctx());
        note_result(call, st);
        note_status("context_commit", st);
        page_ = 0;
        refresh_surrounding_text();
        return true;
    }

    case Term::Key::Ctrl_O: {
        /*
         * What a real client does when the user leaves this field for another
         * one, and the reason the two calls are separate rather than one.
         *
         * Commit first, because the half-typed text belongs to the field being
         * left and should survive in it. Reset second, because what the engine
         * knows about the text around the caret describes *this* field, and
         * carrying it into the next one is how a quotation mark opens twice or
         * a "1.5" is punctuated as a sentence end. Neither call alone is the
         * behaviour: commit without reset keeps stale context, reset without
         * commit loses the user's text.
         *
         * There is no focus concept in the library, so this is not a
         * notification — it is the client doing the two things a focus-out
         * would otherwise have had to mean, at the moment it decides they
         * apply.
         */
        const std::uint64_t commit = note_call("context_commit   (leaving the field)");
        note_result(commit, pathime_context_commit(ctx()));
        const std::uint64_t reset = note_call("context_reset    (leaving the field)");
        note_result(reset, pathime_context_reset(ctx()));
        page_ = 0;
        refresh_surrounding_text();
        note_info("field left: text kept, engine context forgotten");
        return true;
    }

    case Term::Key::Ctrl_U:
        cycle_surrounding();
        return true;

    case Term::Key::Ctrl_D:
        document_.clear();
        refresh_surrounding_text();
        note_info("document cleared by the client — no engine involved");
        return true;

    case Term::Key::Ctrl_Y:
        /* The composed text, out of the terminal and into the clipboard. The
         * demo produces text a user may well want elsewhere, and a full-screen
         * program in raw mode is an awkward place to select it from. */
        clipboard_ = document_;
        status_ = clipboard_.empty()
                      ? "nothing to copy"
                      : "document copied — needs a terminal that honours OSC 52";
        return true;

    case Term::Key::Ctrl_L:
        log_.clear();
        return true;

    case Term::Key::Tab:
        pane_ = pane_ == Pane::Input ? Pane::Options : Pane::Input;
        return true;

    case Term::Key::F1:
        help_ = true;
        return true;

    default:
        break;
    }

    if (key.value >= Term::Key::F2 && key.value <= Term::Key::F9) {
        const std::size_t index =
            static_cast<std::size_t>(key.value - Term::Key::F2);
        if (index < engines_.size()) set_active(index);
        return true;
    }
    return false;
}

void App::input_key(const Term::Key &key)
{
    const pathime_composition_t *comp = composition();

    /*
     * Candidate selection and paging are the client's, not the engine's: the
     * library takes an absolute index through
     * pathime_context_select_candidate() and has nothing to say about which
     * keys reach it or how the list is laid out. So which keys select is a
     * decision this program makes, and it is a real one with a real cost.
     *
     * Digits pick from the visible page, which is what every IME does and why
     * typing a digit into a Chinese composition selects rather than inserting
     * the digit — except under Bopomofo, where 3, 4, 6 and 7 are the tone keys
     * and shadowing them would make the engine's own layout untypable. Alt and
     * a digit therefore selects under every engine, and is the only way to
     * select under that one.
     */
    if (comp != nullptr && comp->candidate_count > 0) {
        const bool alt = key.hasAlt();
        const std::int32_t plain =
            key.value & ~static_cast<std::int32_t>(Term::MetaKey::Value::Alt);
        const bool digits_are_the_engine_s =
            engines_[active_].id == PATHIME_ENGINE_BOPOMOFO;

        if (plain >= Term::Key::One && plain <= Term::Key::Nine &&
            (alt || !digits_are_the_engine_s)) {
            select_candidate(page_ * kPageSize +
                             static_cast<std::size_t>(plain - Term::Key::One));
            return;
        }
        if (key == Term::Key::PageDown) { page_forward(); return; }
        if (key == Term::Key::PageUp)   { page_back(); return; }

        /*
         * The arrows move the hover, and they are bound *here* — before the
         * key is offered to the engine — because that is what owning the
         * binding means. An engine that saw them first and reported them
         * handled would take the decision back, and a client that draws its own
         * candidate list cannot have that decision made for it.
         *
         * The result is visible rather than cosmetic: on an engine that
         * previews its candidates, the preedit rewrites itself to whatever is
         * hovered, so this is the same operation the user would otherwise have
         * to reach by pressing Space repeatedly and hoping.
         */
        if (key == Term::Key::ArrowDown) { move_cursor(1); return; }
        if (key == Term::Key::ArrowUp)   { move_cursor(-1); return; }
    }

    pathime_key_event_t event;
    if (!to_pathime_key(key, &event)) {
        status_ = key_label(key) + " is this program's own; no engine sees it";
        return;
    }

    /*
     * The call goes into the log before it is made, so that the callbacks it
     * causes appear beneath it and in the order the library dispatched them;
     * its result is filled into the same line afterwards. That ordering is the
     * log's whole value — reading downward is reading the dispatch.
     */
    const std::uint64_t call = note_call("process_key  " + event_label(event));

    bool handled = false;
    const pathime_status_t st = pathime_context_process_key(ctx(), &event, &handled);
    note_result(call, st, handled ? "handled" : "declined");

    /*
     * Follow the cursor rather than jumping to page 0. A new list resets the
     * cursor to 0 and this lands on the first page anyway, but Space advances
     * the cursor without replacing the list, and on a long list that walks off
     * the first page — at which point the highlighted entry has to still be
     * the one on screen.
     */
    page_to_cursor();
    last_key_ = key_label(key) + "   " + event_label(event) +
                (handled ? "   handled" : "   declined");

    if (st != PATHIME_OK) {
        note_status("process_key", st);
        /*
         * A failure — as opposed to a rejection — leaves the composition state
         * indeterminate, and the documented recovery is to reset before
         * trusting or displaying anything from the context.
         */
        if (st == PATHIME_ERROR_OUT_OF_MEMORY || st == PATHIME_ERROR_BACKEND) {
            const std::uint64_t recovery = note_call("context_reset  (recovery)");
            note_result(recovery, pathime_context_reset(ctx()));
            note_info("the state was indeterminate; reset is the documented recovery");
        }
    } else if (!handled) {
        unhandled_key(event);
    }

    refresh_surrounding_text();
}

void App::unhandled_key(const pathime_key_event_t &event)
{
    /*
     * The engine declined, so the key is the client's to act on through its
     * ordinary text-input path — the whole point of the handled/unhandled
     * verdict. Note that output may still have been produced while processing
     * it: "handled" describes the incoming event only.
     */
    if (event.keysym == PATHIME_KEY_BACKSPACE) {
        if (document_.empty()) return;
        erase_scalars(&document_, scalar_count(document_) - 1, 1);
        return;
    }

    std::uint32_t scalar = 0;
    if (keysym_scalar(event.keysym, &scalar) && scalar >= 0x20) {
        document_ += utf8_encode(scalar);
        return;
    }
    status_ = "declined, and this client has nothing to do with it";
}

void App::options_key(const Term::Key &key)
{
    if (options_.empty()) {
        status_ = "this engine implements no options";
        return;
    }

    const auto apply = [&](int step) {
        OptionRow &row = options_[option_index_];

        /*
         * For a FLAGS option, Left/Right walk the option's bits rather than
         * changing anything; the toggle key flips the one they landed on. That
         * split only became worth offering when pathime_option_value_name()
         * arrived — a user stepping through twenty anonymous bits learns
         * nothing, but stepping through "c-ch", "z-zh", "an-ang" is an
         * interface.
         */
        if (row.info.type == PATHIME_OPTION_FLAGS && step != 0) {
            std::size_t bits = 0;
            for (int b = 0; b < 64; b++)
                if (row.info.valid_values & (UINT64_C(1) << b)) bits++;
            if (bits == 0) return;
            const std::size_t forward = step > 0 ? 1 : bits - 1;
            row.flags_bit = (row.flags_bit + forward) % bits;
            status_ = std::string(pathime_option_name(row.option)) + " — " +
                      value_text(row, engine(), ctx(), engine_level_);
            return;
        }

        /* An option set is a call like any other, and one worth watching: it
         * can reset the composition, it re-materializes the candidate list,
         * and at engine level it dispatches composition_changed to contexts
         * the caller never passed. Without the call line those callbacks look
         * like they came from nowhere. */
        const std::uint64_t call =
            note_call(std::string(engine_level_ ? "engine" : "context") +
                      "_set_option  " + pathime_option_name(row.option));
        const pathime_status_t st =
            adjust_option(row, engine(), ctx(), engine_level_, step);
        note_result(call, st, value_text(row, engine(), ctx(), engine_level_));
        if (st != PATHIME_OK) {
            note_status(pathime_option_name(row.option), st);
            return;
        }
        status_ = std::string(pathime_option_name(row.option)) + " = " +
                  value_text(row, engine(), ctx(), engine_level_) +
                  (engine_level_ ? "   (on the engine)" : "   (on the context)");
    };

    switch (key.value) {
    case Term::Key::Esc:
        /* Escape means the engine's "cancel" in the text field, and this
         * panel's "I am done here". */
        pane_ = Pane::Input;
        return;

    case Term::Key::ArrowUp:
        option_index_ = (option_index_ + options_.size() - 1) % options_.size();
        return;
    case Term::Key::ArrowDown:
        option_index_ = (option_index_ + 1) % options_.size();
        return;
    case Term::Key::ArrowLeft:  apply(-1);  return;
    case Term::Key::ArrowRight: apply(+1);  return;
    case Term::Key::PageUp:     apply(-16); return;
    case Term::Key::PageDown:   apply(+16); return;
    case Term::Key::Space:
    case Term::Key::Enter:      apply(0);   return;

    case Term::Key::r:
    case Term::Key::R:
    case Term::Key::Backspace: {
        const OptionRow &row = options_[option_index_];
        const std::uint64_t call =
            note_call(std::string(engine_level_ ? "engine" : "context") +
                      "_reset_option  " + pathime_option_name(row.option));
        const pathime_status_t st =
            reset_option(row, engine(), ctx(), engine_level_);
        note_result(call, st, value_text(row, engine(), ctx(), engine_level_));
        if (st != PATHIME_OK) {
            note_status(pathime_option_name(row.option), st);
        } else {
            status_ = std::string(pathime_option_name(row.option)) +
                      " now resolves from below: " +
                      value_text(row, engine(), ctx(), engine_level_);
        }
        return;
    }

    case Term::Key::l:
    case Term::Key::L:
        /* The two levels are not separate namespaces: an engine value is the
         * default its contexts use, and a context value overrides it. */
        engine_level_ = !engine_level_;
        status_ = engine_level_ ? "editing the engine's values"
                                : "editing this context's values";
        return;

    default:
        break;
    }
}

/* ---- Candidates --------------------------------------------------------- */

void App::select_candidate(std::size_t index)
{
    const pathime_composition_t *comp = composition();
    if (comp == nullptr || index >= comp->candidate_count) {
        status_ = "no candidate at that position";
        return;
    }
    const std::uint64_t call =
        note_call("select_candidate  index " + std::to_string(index) + "  \"" +
                  escape_for_log(candidate(index)) + "\"");
    const pathime_status_t st = pathime_context_select_candidate(ctx(), index);
    note_result(call, st);

    page_ = 0;
    if (st != PATHIME_OK) note_status("select_candidate", st);
    refresh_surrounding_text();
}

void App::page_forward()
{
    const pathime_composition_t *comp = composition();
    if (comp == nullptr) return;

    std::size_t count = comp->candidate_count;
    if ((page_ + 1) * kPageSize >= count) count = grow_candidate_list();
    if ((page_ + 1) * kPageSize < count) {
        page_++;
        /* Turning the page takes the hover with it, so the highlighted entry
         * is always one the user can see and 1-9 always pick from the page in
         * front of them. Otherwise the highlight would sit on a page nobody is
         * looking at, and the preedit would show a candidate that is not on
         * screen. */
        move_cursor_to(page_ * kPageSize);
    } else {
        status_ = "that is the whole list";
    }
}

void App::page_back()
{
    if (page_ == 0) return;
    page_--;
    move_cursor_to(page_ * kPageSize);
}

void App::move_cursor(int delta)
{
    const pathime_composition_t *comp = composition();
    if (comp == nullptr || comp->candidate_count == 0) {
        status_ = "no candidates to move through";
        return;
    }

    const std::size_t cursor = comp->candidate_cursor;

    if (delta < 0) {
        /* The top of the list does not wrap: a user holding Up expects to
         * arrive at the best candidate and stop there, not to be thrown to the
         * end of a list they were scrolling away from. */
        const std::size_t back = static_cast<std::size_t>(-delta);
        move_cursor_to(cursor < back ? 0 : cursor - back);
        return;
    }

    std::size_t target = cursor + static_cast<std::size_t>(delta);

    /*
     * Running off the end is where the list may need to grow, and the same
     * stop condition applies as when paging: a list shorter than the cap was
     * not truncated by it, so there is nothing more to be had. Without that
     * test this would raise max-candidates on every press at the bottom of a
     * complete list.
     */
    std::size_t count = comp->candidate_count;
    if (target >= count) count = grow_candidate_list();
    if (target >= count) {
        target = count - 1;
        if (target == cursor) status_ = "that is the whole list";
    }

    move_cursor_to(target);
}

void App::move_cursor_to(std::size_t index)
{
    const pathime_composition_t *comp = composition();
    if (comp == nullptr || index >= comp->candidate_count) return;
    if (index == comp->candidate_cursor) {
        page_to_cursor();
        return;
    }

    const std::uint64_t call =
        note_call("context_set_candidate_cursor  " + std::to_string(index));
    const pathime_status_t st = pathime_context_set_candidate_cursor(ctx(), index);
    note_result(call, st);

    if (st != PATHIME_OK) {
        /* UNSUPPORTED is the honest answer from an engine that has candidates
         * but cannot show one without choosing it. Nothing moved, so there is
         * nothing to undo — just say so. */
        note_status("set_candidate_cursor", st);
        return;
    }
    page_to_cursor();
}

void App::page_to_cursor()
{
    /*
     * Read back from the composition rather than from whatever was last asked
     * for. The library is explicit that the cursor is not the client's to
     * assume: Space moves it, and any change that replaces the list returns it
     * to 0. Paging off a remembered value would put the highlight on the wrong
     * page the first time the engine moved it — which under anthy is the
     * second press of Space.
     */
    const pathime_composition_t *comp = composition();
    if (comp == nullptr || comp->candidate_count == 0) return;
    page_ = comp->candidate_cursor / kPageSize;
}

std::size_t App::grow_candidate_list()
{
    const pathime_composition_t *comp = composition();
    const std::size_t before = comp != nullptr ? comp->candidate_count : 0;

    std::int64_t cap = 0;
    if (pathime_context_get_option_int(ctx(), PATHIME_OPT_MAX_CANDIDATES, &cap)
        != PATHIME_OK)
        return before;  /* Hangul: no candidates, so no cap to raise. */

    /* A list shorter than the cap was not truncated by it, so there is nothing
     * more to be had and raising it would only leave the option changed. This
     * is the only way to tell: the library presents what the cap allowed as
     * the complete list, precisely so a client never has to reason about a
     * backend that enumerates lazily. */
    if (static_cast<std::int64_t>(before) < cap) return before;

    /*
     * The cap is composition-safe on purpose, and this is the case it was made
     * safe for: a client showing a growing list raises it as the user scrolls.
     * Candidates are only ever appended, never reordered, so the numbers
     * already on screen keep meaning what they meant.
     */
    const std::int64_t next = cap + static_cast<std::int64_t>(kPageSize) * 4;
    const std::uint64_t call = note_call("context_set_option  max-candidates " +
                                         std::to_string(next) + "  (to page on)");
    const pathime_status_t st =
        pathime_context_set_option_int(ctx(), PATHIME_OPT_MAX_CANDIDATES, next);
    note_result(call, st);
    if (st != PATHIME_OK) {
        note_status("max-candidates", st);
        return before;
    }

    comp = composition();
    const std::size_t after = comp != nullptr ? comp->candidate_count : 0;
    note_info(std::to_string(after) + " candidates now, was " +
              std::to_string(before));
    return after;
}

/* ---- Plumbing ----------------------------------------------------------- */

void App::set_active(std::size_t index)
{
    if (index >= engines_.size() || index == active_) return;

    /* Switching is nothing but a change of which context gets the next key.
     * The library is told nothing, neither context is disturbed, and the one
     * being left keeps its composition exactly as the user left it — which is
     * why no call is logged here and the log stays silent across the switch.
     * That silence is the thing worth seeing. */
    active_ = index;

    options_ = collect_options(engine());
    option_index_ = 0;
    page_ = 0;
    refresh_surrounding_text();
    note_info(std::string("now typing into ") + engines_[active_].name);
}

void App::refresh_surrounding_text()
{
    /*
     * After *every* dispatch, not merely when a caret moves. The snapshot is
     * the only text the engine can see, and the engine's own commit_text
     * invalidates it — a client that refreshes only on caret movement finds the
     * engine progressively unable to revise its own output, which under
     * PATHIME_HANGUL_PREEDIT_NONE means syllables stop assembling.
     *
     * Except under Surrounding::None, which is here to show exactly that. A
     * client that stops refreshing does not thereby retract what it already
     * supplied — there is no call for that and no need of one — so the library
     * keeps the last snapshot and it goes stale under it. That is what the log
     * shows: deletions declined because this program can see its own document
     * has moved on.
     */
    if (surrounding_ == Surrounding::None) {
        return;
    }

    /*
     * The cursor is always at the end of what is supplied because this
     * program's caret is always at the end of its document: it is a demo text
     * field, not an editor. text.len sizes a buffer and is in bytes, the cursor
     * locates a position and is in scalar values, and getting that pair the
     * wrong way round is the easiest mistake in this API.
     */
    snapshot_ = surrounding_ == Surrounding::Full ? document_ : last_scalars(document_, 1);
    snapshot_cursor_ = scalar_count(snapshot_);

    const pathime_str_t text{snapshot_.c_str(), snapshot_.size()};
    const std::uint64_t call =
        note_call("context_set_surrounding_text  " + std::to_string(snapshot_cursor_) +
                  " scalars, cursor=" + std::to_string(snapshot_cursor_));
    const pathime_status_t st =
        pathime_context_set_surrounding_text(ctx(), text, snapshot_cursor_);
    note_result(call, st);
    if (st != PATHIME_OK) note_status("set_surrounding_text", st);
}

void App::cycle_surrounding()
{
    switch (surrounding_) {
    case Surrounding::Full:     surrounding_ = Surrounding::Fragment; break;
    case Surrounding::Fragment: surrounding_ = Surrounding::None;     break;
    case Surrounding::None:     surrounding_ = Surrounding::Full;     break;
    }

    switch (surrounding_) {
    case Surrounding::Full:
        note_info("surrounding text: the whole document");
        break;
    case Surrounding::Fragment:
        note_info("surrounding text: one scalar — enough for \"1.5\", not for quotes");
        break;
    case Surrounding::None:
        note_info("surrounding text: none — the last snapshot is now going stale");
        break;
    }
    refresh_surrounding_text();
}

std::uint64_t App::note_call(const std::string &line)
{
    const std::uint64_t id = next_log_id_++;
    log_.push_back(LogEntry{LogKind::Call, line, id});
    while (log_.size() > kLogLines) log_.pop_front();
    return id;
}

void App::note_result(std::uint64_t id, pathime_status_t status,
                      const std::string &extra)
{
    /* From the back: the call is the most recent entry that is not one of the
     * callbacks it caused. It may also have been trimmed off the front while
     * those piled up, which is why this is a search and not an index. */
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        if (it->id != id) continue;
        it->text += "  = ";
        it->text += status == PATHIME_OK ? "OK" : pathime_status_string(status);
        if (!extra.empty()) it->text += ", " + extra;
        return;
    }
}

void App::note_callback(const std::string &line)
{
    log_.push_back(LogEntry{LogKind::Callback, line, 0});
    while (log_.size() > kLogLines) log_.pop_front();
}

void App::note_info(const std::string &line)
{
    log_.push_back(LogEntry{LogKind::Info, line, 0});
    while (log_.size() > kLogLines) log_.pop_front();
}

void App::note_status(const char *what, pathime_status_t status)
{
    if (status == PATHIME_OK) return;
    status_ = std::string(what) + ": " + pathime_status_string(status);
    note_info(status_);
}

std::string App::take_clipboard()
{
    std::string text;
    text.swap(clipboard_);
    return text;
}

const pathime_composition_t *App::composition() const
{
    return ctx() != nullptr ? pathime_context_composition(ctx()) : nullptr;
}

std::string App::candidate(std::size_t index) const
{
    pathime_str_t text{nullptr, 0};
    if (ctx() == nullptr) return std::string();
    if (pathime_context_candidate(ctx(), index, &text) != PATHIME_OK)
        return std::string();
    return std::string(text.bytes, text.len);
}

std::string App::option_value_text(std::size_t index) const
{
    if (index >= options_.size()) return std::string();
    return value_text(options_[index], engines_[active_].engine,
                      engines_[active_].ctx, engine_level_);
}

bool App::is_option_set_here(std::size_t index) const
{
    if (index >= options_.size()) return false;
    return is_set_here(options_[index], engines_[active_].engine,
                       engines_[active_].ctx, engine_level_);
}

bool App::is_option_shadowed(std::size_t index) const
{
    if (index >= options_.size()) return false;
    return pathime_context_option_is_set(engines_[active_].ctx,
                                         options_[index].option);
}

std::uint32_t App::requirements() const
{
    /*
     * The *context's* requirements, not the engine's. This panel sits over the
     * text field the user is typing into, and the option that drives these
     * bits — hangul-preedit — is editable per context from the options pane
     * two panels down. Asking the engine would leave the line reading
     * "nothing" while the context in front of the user was in the mode that
     * needs both callbacks.
     */
    if (engines_.empty()) return 0;
    return pathime_context_requirements(engines_[active_].ctx);
}

}  // namespace demo
