/*
 * Implementation of the Korean adapter declared in hangul_backend.h.
 *
 * The whole of this slice is keys in, preedit and commit out. Everything
 * libhangul offers beyond that — hanja lookup, the translate/transition
 * callbacks, HANGUL_OUTPUT_JAMO, external keyboard files — is either cut from
 * the API or not reached from here, and where it is cut the reason is at the
 * function that would have used it.
 *
 * Three shapes of libhangul behaviour drive the code below, and each was
 * checked against the submodule and against a scratch program that typed real
 * words through it rather than taken from the mapping doc alone:
 *
 *  1. hangul_ic_process() takes one US-QWERTY ASCII int. There is no modifier
 *     state: case *is* the shift state, and the value indexes a 0x80-entry
 *     table (engines/libhangul/hangul/hangulkeyboard.c:70, :444-460) whose contents
 *     depend on the selected layout.
 *  2. The preedit and commit strings are UCS-4, borrowed, and overwritten at
 *     the *start* of the next process/backspace call
 *     (hangulinputcontext.c:1084-1085). They are copied here immediately,
 *     which is backend.h rule 1.
 *  3. An unknown keyboard id is not reported. hangul_ic_select_keyboard()
 *     stores whatever hangul_keyboard_list_get_keyboard() returned, NULL
 *     included (hangulinputcontext.c:1470-1482, :1428 set_keyboard), and the
 *     next process() dereferences it. Every id this file uses is therefore
 *     resolved to a HangulKeyboard * first, and a NULL result is refused.
 */

#include "engines/hangul/hangul_backend.h"

#include <cstring>
#include <memory>
#include <string>

#include <hangul.h>

#include "composition.h"
#include "keys.h"
#include "utf8.h"

namespace pathime {
namespace {

/* ===========================================================================
 * Layout ids
 *
 * The nine values of PATHIME_OPT_HANGUL_LAYOUT against the nine built-in
 * tables. Every id below was read out of the built-in array in
 * engines/libhangul/hangul/hangulkeyboard.c:133-224 — the HangulKeyboard literals and
 * the hangul_builtin_keyboards[] list that indexes them — and then confirmed
 * at runtime by enumerating hangul_keyboard_list_get_keyboard_id() and looking
 * each one up again. Three of them are not what a reader would guess, which is
 * the reason for the paranoia: 390 is "39", Final is "3f", and Noshift is "3s"
 * (its struct is named hangul_keyboard_3sun). "390", "3final" and "3" are all
 * absent, and passing any of them would be the crash in note 3 above.
 * ======================================================================== */

const char *keyboard_id_for(int64_t layout)
{
    switch (layout) {
    case PATHIME_HANGUL_LAYOUT_2SET:         return "2";    /* Dubeolsik */
    case PATHIME_HANGUL_LAYOUT_2SET_YET:     return "2y";   /* Dubeolsik Yetgeul */
    case PATHIME_HANGUL_LAYOUT_3SET_2:       return "32";   /* Sebeolsik Dubeol Layout */
    case PATHIME_HANGUL_LAYOUT_3SET_390:     return "39";   /* Sebeolsik 390 */
    case PATHIME_HANGUL_LAYOUT_3SET_FINAL:   return "3f";   /* Sebeolsik Final */
    case PATHIME_HANGUL_LAYOUT_3SET_NOSHIFT: return "3s";   /* Sebeolsik Noshift */
    case PATHIME_HANGUL_LAYOUT_3SET_YET:     return "3y";   /* Sebeolsik Yetgeul */
    case PATHIME_HANGUL_LAYOUT_ROMAJA:       return "ro";   /* Romaja */
    case PATHIME_HANGUL_LAYOUT_AHNMATAE:     return "ahn";  /* Ahnmatae */
    default:                                 return nullptr;
    }
}

/**
 * The HangulKeyboard for a resolved PATHIME_OPT_HANGUL_LAYOUT value, or
 * nullptr if libhangul does not have it.
 *
 * A value outside the enum falls back to the documented default rather than
 * failing: options.cc validates enum ranges, so getting one here would already
 * be a library bug, and refusing every subsequent key because of it would turn
 * that bug into a dead input method. Falling back is visible in the composed
 * text and is not.
 */
const HangulKeyboard *resolve_keyboard(int64_t layout)
{
    const char *id = keyboard_id_for(layout);
    if (id == nullptr) {
        id = keyboard_id_for(PATHIME_HANGUL_LAYOUT_2SET);
    }
    /* The only safe query: it returns nullptr for an unknown id instead of
     * arming the next process() call with one. */
    return hangul_keyboard_list_get_keyboard(id);
}

/* ===========================================================================
 * KeyEvent to ASCII
 * ======================================================================== */

/** Returned by ascii_for_key() for a key that must never reach libhangul. */
constexpr int kNotAscii = -1;

/**
 * The single int hangul_ic_process() wants, or kNotAscii.
 *
 * The position-and-shift recombination itself is keys.cc's us_layout_char():
 * libhangul is not the only backend that dispatches on where a key is rather
 * than what it prints — PATHIME_ANTHY_TYPING_KANA does too — so the US-QWERTY
 * shift table lives in the key layer with the rest of that knowledge.
 *
 * What stays here is the range refusal, which is a libhangul requirement
 * rather than tidiness. hangul_keyboard_map_to_char() would answer 0 for an
 * out-of-range key and the jamo path would tolerate it, but
 * hangul_ic_process_romaja() calls isupper() on the raw argument
 * (hangulinputcontext.c:887), and isupper() outside unsigned char is
 * undefined. us_layout_char() answers 0 for exactly the keys that must not get
 * that far, so the two conditions are the same one.
 */
int ascii_for_key(const KeyEvent &key)
{
    const char c = us_layout_char(key);
    return c == 0 ? kNotAscii : static_cast<int>(c);
}

/** Drop the last Unicode scalar value from @a text, which must be valid UTF-8. */
void erase_last_scalar(std::string *text)
{
    const size_t scalars = utf8_scalar_count(text->data(), text->size());
    if (scalars == 0) {
        return;
    }
    const size_t offset = utf8_byte_offset(text->data(), text->size(), scalars - 1);
    if (offset != kUtf8NoPosition) {
        text->resize(offset);
    }
}

/* ===========================================================================
 * The context
 * ======================================================================== */

struct HicDeleter {
    void operator()(HangulInputContext *hic) const { hangul_ic_delete(hic); }
};
using HicPtr = std::unique_ptr<HangulInputContext, HicDeleter>;

class HangulContextBackend final : public ContextBackend {
public:
    explicit HangulContextBackend(HicPtr hic) : hic_(std::move(hic)) {}

    bool process_key(const KeyEvent &key,
                     const OptionReader &options,
                     const SurroundingTextView &doc,
                     Composition *model,
                     Output *out) override;

    void reset(Composition *model) override;
    void commit(const OptionReader &options,
                Composition *model,
                Output *out) override;

    pathime_status_t select_candidate(size_t index,
                                      const OptionReader &options,
                                      Composition *model,
                                      Output *out) override;

    void materialize_candidates(size_t cap,
                                const OptionReader &options,
                                Composition *model) override;

private:
    void apply_options(const OptionReader &options);
    void harvest(int64_t preedit_mode, Composition *model, Output *out);
    void end_composition(Composition *model, Output *out);
    bool prepare_revision(const SurroundingTextView &doc);
    void publish_in_document(Composition *model, Output *out);

    HicPtr hic_;

    /**
     * PATHIME_HANGUL_PREEDIT_NONE only: the provisional syllable this adapter
     * last wrote into the client's document, and therefore what it must delete
     * before writing a fuller form.
     *
     * Empty in the other two modes, and empty in NONE whenever there is
     * nothing provisional outstanding. It is the adapter's memory of the
     * document, which is a thing no other mode needs because no other mode
     * puts unfinished text there.
     */
    std::string in_document_;
};

/**
 * Push the three per-context options and the layout into libhangul.
 *
 * Called at the top of every key, never cached, which is backend.h rule 4 and
 * is what makes the header's "a change takes effect immediately" promise true
 * for free. The cost is four field stores: hangul_ic_set_option() sets a bool
 * (hangulinputcontext.c:1365-1378) and hangul_ic_set_keyboard() sets a pointer
 * and a table id (:1428-1435). Neither touches the jamo buffer, which is why
 * PATHIME_OPT_HANGUL_LAYOUT can advertise itself composition-safe.
 *
 * A layout that fails to resolve leaves the current keyboard in place. That is
 * the one thing this function must never get wrong — see note 3 at the top.
 */
void HangulContextBackend::apply_options(const OptionReader &options)
{
    const HangulKeyboard *keyboard =
        resolve_keyboard(options.number(PATHIME_OPT_HANGUL_LAYOUT));
    if (keyboard != nullptr) {
        hangul_ic_set_keyboard(hic_.get(), keyboard);
    }

    hangul_ic_set_option(hic_.get(), HANGUL_IC_OPTION_AUTO_REORDER,
                         options.flag(PATHIME_OPT_HANGUL_AUTO_REORDER));
    hangul_ic_set_option(hic_.get(), HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE,
                         options.flag(PATHIME_OPT_HANGUL_DOUBLE_STROKE_COMBINE));
    hangul_ic_set_option(hic_.get(), HANGUL_IC_OPTION_NON_CHOSEONG_COMBI,
                         options.flag(PATHIME_OPT_HANGUL_NON_CHOSEONG_COMBINE));
}

/**
 * Read both strings back out of libhangul and place them in the model.
 *
 * Both are borrowed and volatile, so both are copied through utf8.h before
 * anything else can call into the library. The commit string in particular is
 * cleared at the *start* of the next process/backspace
 * (hangulinputcontext.c:1084-1085), so "before anything else" is literal.
 *
 * Neither string is one character. The preedit is 1-3 UCS-4 codepoints —
 * choseong plus HANGUL_JUNGSEONG_FILLER, or a jaso combination with no
 * precomposed form — and the commit string can hold a finished syllable
 * *followed by* an appended character: typing "han." on the romaja layout
 * leaves U+D55C U+002E in it. utf8_from_ucs4_z() handles the run; nothing here
 * may assume a length.
 *
 * Where the commit string goes is the whole of PATHIME_OPT_HANGUL_PREEDIT.
 */
void HangulContextBackend::harvest(int64_t preedit_mode,
                                   Composition *model,
                                   Output *out)
{
    std::string committed;
    utf8_from_ucs4_z(hangul_ic_get_commit_string(hic_.get()), &committed);

    std::string preedit;
    utf8_from_ucs4_z(hangul_ic_get_preedit_string(hic_.get()), &preedit);

    if (preedit_mode == PATHIME_HANGUL_PREEDIT_NONE) {
        /*
         * The document is the display. Nothing is held: whatever libhangul
         * finished and whatever it is still assembling both go into the
         * client's text, and the provisional part is taken back out again on
         * the next key by the deletion issued here.
         *
         * Deletion first and commit second is not this function's choice to
         * make — refresh_composition() dispatches them in that order whatever
         * order they were recorded — but the *range* is this function's
         * responsibility: it describes text written by an earlier call, and it
         * is only correct because process_key() confirmed through
         * SurroundingTextView that the snapshot still covers it before letting
         * this run.
         */
        const size_t stale =
            utf8_scalar_count(in_document_.c_str(), in_document_.size());
        if (stale != 0) {
            out->request_deletion(-static_cast<ptrdiff_t>(stale), stale);
        }

        /*
         * One commit carrying both parts, rather than a commit of the finished
         * text and a second of the provisional syllable. The public API
         * dispatches at most one commit_text per call, and a client applying
         * this to its document must not see the syllable arrive separately
         * from the text that precedes it.
         */
        out->commit += committed;
        out->commit += preedit;
        in_document_ = preedit;

        /* Nothing is held, so there is no preedit for the client to draw —
         * which is the entire point of the mode, and why it is the one mode a
         * client without a preedit can use. */
        model->active.clear();
        return;
    }

    if (!committed.empty()) {
        if (preedit_mode == PATHIME_HANGUL_PREEDIT_WORD) {
            /*
             * Word mode: the finished syllable stops being mutable but is not
             * yet the client's. composition.h calls this the one place the
             * library holds text libhangul itself has already let go of, and
             * `settled` is where it goes; the projection then reports it as
             * preedit with preedit_settled marking how much of it is done.
             *
             * Deliberately not Composition::settle_active(): that promotes the
             * *previous* `active`, and the two are not the same string. A
             * commit can carry more than the syllable that was on display, as
             * the "han." case above shows. The commit string is the
             * authoritative account of what libhangul finished.
             */
            model->settled += committed;
        } else {
            out->commit += committed;
        }
    }

    model->active = preedit;
}

/**
 * End the composition: flush anything libhangul still holds, then hand over
 * everything this side was holding for it.
 *
 * This is the one caller of hangul_ic_flush(), and it is written the way it is
 * because of the trap in docs/libhangul-mapping.md's Flush row: flush
 * serializes the pending jamo into a *separate* flushed_string buffer and
 * clears the commit string on the way, so the return value is the only place
 * the flushed text ever appears. A caller that flushes and then reads
 * hangul_ic_get_commit_string() gets nothing. Confirmed by experiment: after
 * typing "gk" (하) and flushing, the return value is U+D558 and the commit
 * string is empty.
 *
 * It is therefore also why every path that wants both calls harvest() first
 * and this second.
 */
void HangulContextBackend::end_composition(Composition *model, Output *out)
{
    std::string flushed;
    utf8_from_ucs4_z(hangul_ic_flush(hic_.get()), &flushed);

    /*
     * PATHIME_HANGUL_PREEDIT_NONE has already put the pending syllable in the
     * client's document, so ending the composition means letting it stand, not
     * committing it a second time. The flushed text is dropped and the
     * adapter simply stops tracking what it wrote.
     *
     * That is only safe because the flushed string and the preedit string are
     * the same text, and they are: measured across all nine built-in layouts
     * and 72 key sequences, hangul_ic_flush() never returned anything other
     * than the preedit string standing at the moment of the call — including
     * for the three-set jaso combinations that have no precomposed form and
     * come back as U+1100 U+1160 U+11AB with the filler intact. Whatever
     * libhangul would hand over here is byte for byte what this adapter
     * already wrote, so committing it would duplicate the syllable rather than
     * complete it.
     */
    if (!in_document_.empty()) {
        in_document_.clear();
        model->active.clear();
        return;
    }

    model->settled += flushed;
    model->active.clear();
    out->commit += model->settled;
    model->settled.clear();
}

/**
 * Decide, before libhangul sees the key, whether the provisional syllable in
 * the client's document can still be revised — and give up cleanly if it
 * cannot.
 *
 * This exists because the recovery the public header specifies cannot be
 * performed after the fact. When the snapshot no longer covers the text this
 * adapter wrote, the rule is to "abandon the revision, discard the composition
 * state that was to be revised, and treat what is already in the document as
 * final, continuing from the next input as if starting fresh" — and "starting
 * fresh" means this key must build a *new* syllable rather than extend the
 * stranded one. By the time refresh_composition() drops the deletion request,
 * libhangul has already combined the key into the old syllable and the commit
 * has already been decided. So the question is asked here, first.
 *
 * The usual reason to answer no is not a client bug. This mode commits on
 * every keystroke, and a commit invalidates the snapshot by definition, so a
 * client must re-supply surrounding text after every single key to keep up.
 * That is what PATHIME_HANGUL_PREEDIT_NONE's documentation means by requiring
 * the surrounding-text surface "keenly".
 *
 * @return true if composition may continue; false if it was abandoned, in
 *         which case libhangul has been reset and the caller should treat the
 *         key as the start of something new.
 */
bool HangulContextBackend::prepare_revision(const SurroundingTextView &doc)
{
    if (in_document_.empty()) {
        /* Nothing provisional is outstanding, so there is nothing to revise
         * and no snapshot is needed. This is every key in the other two modes
         * and the first key of a syllable in this one. */
        return true;
    }

    const size_t stale =
        utf8_scalar_count(in_document_.c_str(), in_document_.size());
    if (doc.can_delete_before(stale)) {
        return true;
    }

    /*
     * Abandoned. hangul_ic_reset() and not hangul_ic_flush(): the syllable is
     * already in the client's document, so there is nothing to hand over and
     * flushing would produce a duplicate. Forgetting in_document_ is what
     * makes the stranded text final — the next harvest() will have no deletion
     * to issue and will simply write a new syllable after it.
     *
     * No key is refused and no error is reported, exactly as the header says.
     * The user sees the partial syllable stay where it is and a new one begin.
     */
    hangul_ic_reset(hic_.get());
    in_document_.clear();
    return false;
}

bool HangulContextBackend::process_key(const KeyEvent &key,
                                       const OptionReader &options,
                                       const SurroundingTextView &doc,
                                       Composition *model,
                                       Output *out)
{
    apply_options(options);

    const int64_t preedit_mode = options.number(PATHIME_OPT_HANGUL_PREEDIT);

    /*
     * 0. Under PATHIME_HANGUL_PREEDIT_NONE, settle whether the syllable this
     * adapter put in the client's document can still be revised — before
     * libhangul is allowed to fold this key into it. See prepare_revision().
     *
     * Unconditional rather than restricted to the branches that revise, and
     * that costs nothing: in_document_ is empty in the other two modes, so the
     * call returns true without asking anything. When it does abandon, the
     * branches below simply act on a freshly reset input context, which is
     * what "continuing as if starting fresh" means for each of them — a
     * chorded or non-ASCII key ends a composition that is already over, a
     * backspace finds nothing to remove and goes to the client, and a jamo
     * begins a new syllable.
     */
    prepare_revision(doc);

    /*
     * 1. Chorded keys are the client's, always.
     *
     * libhangul has no modifier argument, so a Control-, Alt- or Super-chord
     * reaching it would be indistinguishable from the bare key and would type
     * a jamo in the middle of the user's shortcut. Declining is the whole
     * reason modifiers cross backend.h (pathime.h's modifier documentation
     * says as much).
     */
    if (is_chorded(key)) {
        end_composition(model, out);
        return false;
    }

    /*
     * 2. Backspace is a separate entry point, not an ASCII value
     * (docs/libhangul-mapping.md mismatch 11): mapping it into the keyboard
     * table was awkward enough that libhangul gave it its own function.
     *
     * hangul_ic_process() would in fact route '\b' onward for us
     * (hangulinputcontext.c:1091-1093), but only after a table lookup on 0x08
     * that means nothing, so the direct call is both clearer and the one the
     * library documents.
     */
    if (key.keysym == PATHIME_KEY_BACKSPACE) {
        if (hangul_ic_backspace(hic_.get())) {
            harvest(preedit_mode, model, out);
            return true;
        }
        /*
         * libhangul had nothing left to remove. In word mode the settled
         * prefix is ours and libhangul has never heard of it, so the backspace
         * still has work to do — one scalar, which for Hangul is one composed
         * syllable. This is the granularity difference PATHIME_OPT_HANGUL_PREEDIT
         * warns about, and it is the same thing ibus-hangul does to its own
         * UString preedit before deciding the key was handled.
         */
        if (preedit_mode == PATHIME_HANGUL_PREEDIT_WORD && !model->settled.empty()) {
            erase_last_scalar(&model->settled);
            return true;
        }
        return false;
    }

    /*
     * 3. Anything that is not printable US-QWERTY ASCII ends the composition
     * and goes back to the client: Return, Escape, the arrows, and every
     * keysym this library has never heard of.
     *
     * A determinate rule, and the determinate choice is to commit rather than
     * discard. The cost is worth stating plainly: Escape commits the syllable
     * in progress instead of cancelling it. Cancelling is what
     * pathime_context_reset() is for, and it is a call the client can make;
     * silently destroying typed text because the user pressed an arrow key is
     * not something the client can undo. The same rule is what stops a partial
     * syllable outliving the user's interest in it — a held word must not
     * survive a Return.
     */
    const int ascii = ascii_for_key(key);
    if (ascii == kNotAscii) {
        end_composition(model, out);
        return false;
    }

    /*
     * 4. The key itself.
     *
     * Non-jamo printable ASCII is offered to libhangul rather than intercepted
     * here, and this is a decision rather than an omission. The mapping doc
     * warns that libhangul appends such a character to the commit string and
     * reports the key handled (mismatch 3), which sounds like an engine eating
     * the user's punctuation; measuring it shows what it really is. On the
     * two-set layouts — the default and the common case — the tables map
     * letters and nothing else, so every digit and every punctuation mark maps
     * to 0, is declined, and reaches the client untouched. It is the
     * three-set, ahnmatae and romaja tables that carry symbol entries, and
     * there the "swallowed" character is the layout doing its job: on
     * Sebeolsik Final the '?' position is '!' and '{' is '%'. Intercepting
     * those would hand the user the US-QWERTY character their chosen Korean
     * layout explicitly reassigns — the actual way to get punctuation wrong.
     *
     * So the verdict is libhangul's. What is not libhangul's is the word
     * boundary: a key it declines closes the held word, which with the rule
     * above makes "a space or any key hangul refuses" the boundary in word
     * mode.
     */
    const bool handled = hangul_ic_process(hic_.get(), ascii);
    harvest(preedit_mode, model, out);
    if (!handled) {
        end_composition(model, out);
    }
    return handled;
}

/**
 * hangul_ic_reset(), not hangul_ic_flush().
 *
 * The public contract for pathime_context_reset() is that it does not commit,
 * and these are the two calls that differ on exactly that: reset clears the
 * preedit, commit and flushed strings and empties the jamo buffer, discarding
 * the syllable; flush serializes that syllable and hands it back to be
 * committed (docs/libhangul-mapping.md mismatch 4). Discarding is what the
 * header asks for, so reset is the call.
 *
 * The settled prefix goes with it. In word mode `settled` holds finished
 * syllables that are still preedit as far as the client is concerned — the
 * projection reports them inside preedit, with preedit_settled marking them —
 * and preedit is precisely what a reset discards. Committing them here would
 * make pathime_context_reset() commit, in the one mode where it had something
 * to commit, which is the behaviour the header rules out. Clearing is left to
 * the caller, which clears the model regardless.
 */
void HangulContextBackend::reset(Composition *model)
{
    (void)model;
    hangul_ic_reset(hic_.get());

    /*
     * Whatever this adapter wrote into the client's document under
     * PATHIME_HANGUL_PREEDIT_NONE is left standing and simply forgotten. A
     * reset does not reach into the document to take text back — it ends the
     * composition's claim on it, and what is already there is the client's.
     */
    in_document_.clear();
}

/**
 * hangul_ic_flush(), which is the call reset() deliberately is not.
 *
 * end_composition() is the whole of it: it is what a key that libhangul
 * declines already does, so a client asking for the composition to end lands
 * exactly where a word boundary would have put it. Under
 * PATHIME_HANGUL_PREEDIT_NONE it commits nothing, because the syllable is
 * already in the document — the same reasoning end_composition() carries.
 */
void HangulContextBackend::commit(const OptionReader &options,
                                  Composition *model,
                                  Output *out)
{
    (void)options;
    end_composition(model, out);
}

/**
 * Hangul has no candidates, so there is nothing to select.
 *
 * The only candidate list libhangul could supply is hanja conversion, through
 * the separate HanjaTable/HanjaList lookup API, and hanja is out of scope for
 * this API. Nothing in this adapter ever puts anything in
 * model->candidates, so the core never has an in-range index to offer, and
 * backend.h names this case explicitly as the legitimate use of
 * PATHIME_ERROR_UNSUPPORTED.
 */
pathime_status_t HangulContextBackend::select_candidate(size_t index,
                                                        const OptionReader &options,
                                                        Composition *model,
                                                        Output *out)
{
    (void)index;
    (void)options;
    (void)model;
    (void)out;
    return PATHIME_ERROR_UNSUPPORTED;
}

/**
 * Nothing to materialize, for the reason above: Hangul composition produces no
 * alternatives at all. The eager-materialization obligation this method exists
 * to serve is satisfied vacuously — an empty list is already complete before
 * any callback runs — and model->candidates is left alone rather than cleared,
 * because backend.h's rule is that a backend producing nothing touches
 * nothing.
 */
void HangulContextBackend::materialize_candidates(size_t cap,
                                                  const OptionReader &options,
                                                  Composition *model)
{
    (void)cap;
    (void)options;
    (void)model;
}

/* ===========================================================================
 * The engine
 * ======================================================================== */

/**
 * Hangul's engine layer holds nothing.
 *
 * The expensive shared state the other two backends have — anthy's
 * dictionaries, pyzy's database — has no analogue here: the keyboard tables
 * are static const data compiled into libhangul, and composition state lives
 * entirely in the per-context HangulInputContext. The class exists so that the
 * engine/context split backend.h describes has a Korean member.
 */
class HangulEngineBackend final : public EngineBackend {
public:
    std::unique_ptr<ContextBackend> create_context(const OptionReader &options) override
    {
        /*
         * Resolve and validate before hangul_ic_new(), never after. The
         * constructor takes an id string and passes it straight to
         * hangul_ic_select_keyboard(), which stores a NULL keyboard for an id
         * it does not know and leaves the first key press to dereference it.
         * A context that cannot name a real layout is refused here instead.
         */
        const HangulKeyboard *keyboard =
            resolve_keyboard(options.number(PATHIME_OPT_HANGUL_LAYOUT));
        if (keyboard == nullptr) {
            return nullptr;
        }

        HicPtr hic(hangul_ic_new(nullptr));
        if (!hic) {
            return nullptr;
        }
        /* nullptr above means libhangul's own "2" default, which it resolves
         * through the same lookup; the keyboard we validated is then installed
         * directly, so no unvalidated id is ever passed. */
        hangul_ic_set_keyboard(hic.get(), keyboard);

        /*
         * Precomposed syllables, which is libhangul's own default and the only
         * mode this API describes: HANGUL_OUTPUT_JAMO would hand back
         * decomposed conjoining jamo instead, and no option selects it. Set
         * explicitly so that a change to the library's default cannot quietly
         * change what clients see.
         */
        hangul_ic_set_output_mode(hic.get(), HANGUL_OUTPUT_SYLLABLE);

        return std::unique_ptr<ContextBackend>(
            new HangulContextBackend(std::move(hic)));
    }
};

}  // namespace

/* ===========================================================================
 * The process-global layer
 * ======================================================================== */

/*
 * Both no-ops, and neither can fail. hangul_init() and hangul_fini() exist
 * only under ENABLE_EXTERNAL_KEYBOARDS (engines/libhangul/hangul/hangul.h:99-102),
 * which the top-level CMakeLists.txt:37 turns off, and the nine built-in
 * layouts are static tables that resolve without any initialization — so
 * hangul is the one backend with no process-global setup at all. src/init.cc
 * and src/engine.cc already say this at their own call sites; this states it
 * where the function is.
 *
 * Both directories are unused for the same reason. libhangul ships no data
 * files at all — the layouts and the character tables are compiled into the
 * library — so there is nothing under resource_dir for it to find, and the
 * only path it would ever want beneath data_dir is the user keyboard
 * directory, which is exactly what ENABLE_EXTERNAL_KEYBOARDS gates. Hangul is
 * therefore the one engine that is available wherever the library is.
 */

bool hangul_global_init(const char *data_dir, const char *resource_dir)
{
    (void)data_dir;
    (void)resource_dir;
    return true;
}

void hangul_global_shutdown()
{
}

std::unique_ptr<EngineBackend> hangul_create_engine()
{
    return std::unique_ptr<EngineBackend>(new HangulEngineBackend());
}

}  // namespace pathime
