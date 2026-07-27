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
 *     table (libhangul/hangul/hangulkeyboard.c:70, :444-460) whose contents
 *     depend on the selected layout.
 *  2. The preedit and commit strings are UCS-4, borrowed, and overwritten at
 *     the *start* of the next process/backspace call
 *     (hangulinputcontext.c:1084-1085). They are copied here immediately,
 *     which is backend.h rule 1.
 *  3. An unknown keyboard id is not reported. hangul_ic_select_keyboard()
 *     stores whatever hangul_keyboard_list_get_keyboard() returned, NULL
 *     included (hangulinputcontext.c:1470-1482, :1484 set_keyboard), and the
 *     next process() dereferences it. Every id this file uses is therefore
 *     resolved to a HangulKeyboard * first, and a NULL result is refused.
 */

#include "engines/hangul/hangul_backend.h"

#include <cstring>
#include <memory>
#include <string>

#include <hangul.h>

#include "composition.h"
#include "utf8.h"

namespace pathime {
namespace {

/* ===========================================================================
 * Layout ids
 *
 * The nine values of PATHIME_OPT_HANGUL_LAYOUT against the nine built-in
 * tables. Every id below was read out of the built-in array in
 * libhangul/hangul/hangulkeyboard.c:133-217 — the HangulKeyboard literals and
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

/**
 * The US-QWERTY shifted form of @a c, or @a c unchanged when the key has no
 * shifted form.
 *
 * Upper-casing the letters is the part everyone remembers, and on the two-set
 * layouts it is the only part that matters: their tables map letters and
 * nothing else. It is not sufficient in general. libhangul's tables are
 * indexed by the US-QWERTY character *after* shift, not by a base key plus a
 * flag, and the three-set layouts populate the symbol positions too — on
 * Sebeolsik 390 the '<' entry (that is, Shift+comma) yields '2'. Sending ','
 * there because Shift only ever meant toupper() would silently type the wrong
 * character, so the whole shift row is spelled out.
 */
int shifted_ascii(int c)
{
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    static const char kUnshifted[] = "`1234567890-=[]\\;',./";
    static const char kShifted[]   = "~!@#$%^&*()_+{}|:\"<>?";
    static_assert(sizeof(kUnshifted) == sizeof(kShifted),
                  "the two halves of the US-QWERTY shift row must line up "
                  "position for position; the lookup below indexes one with an "
                  "offset found in the other");
    if (c != '\0') {
        const char *p = std::strchr(kUnshifted, c);
        if (p != nullptr && *p != '\0') {
            return kShifted[p - kUnshifted];
        }
    }
    return c;
}

/** Returned by ascii_for_key() for a key that must never reach libhangul. */
constexpr int kNotAscii = -1;

/**
 * The single int hangul_ic_process() wants, or kNotAscii.
 *
 * Position, not character. KeyEvent::position_key() is layout_key — the
 * physical key as the keysym it would produce *unmodified* on US QWERTY —
 * falling back to the keysym when the client has no physical key to report.
 * That is exactly libhangul's input model: Hangul composition is defined by
 * where the key is, which is the entire reason layout_key exists in the API.
 *
 * Shift is then folded back in as case, because libhangul has no modifier
 * argument at all. CapsLock is undone only on the fallback path: layout_key is
 * by construction the unmodified keysym, so no lock can have reached it, while
 * a keysym carries whatever capitalization the client's layout applied. This
 * is the same correction ibus-hangul makes, for the same reason
 * (docs/libhangul-mapping.md, "CapsLock compensation").
 *
 * Anything outside printable ASCII is refused here rather than passed through.
 * hangul_keyboard_map_to_char() would answer 0 for an out-of-range key and the
 * jamo path would tolerate it, but hangul_ic_process_romaja() calls isupper()
 * on the raw argument (hangulinputcontext.c:1100-1101), and isupper() outside
 * unsigned char is undefined. The range check is a correctness requirement on
 * the romaja layout, not tidiness.
 */
int ascii_for_key(const KeyEvent &key)
{
    const uint32_t k = key.position_key();
    if (k < 0x20 || k > 0x7e) {
        return kNotAscii;
    }

    int c = static_cast<int>(k);

    if (key.layout_key == 0 && key.has(PATHIME_MOD_CAPS)) {
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        } else if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
    }

    if (key.has(PATHIME_MOD_SHIFT)) {
        c = shifted_ascii(c);
    }
    return c;
}

/** True for a chord the client owns: Control, Alt or Super held. */
bool is_chorded(const KeyEvent &key)
{
    return key.has(PATHIME_MOD_CONTROL) || key.has(PATHIME_MOD_ALT) ||
           key.has(PATHIME_MOD_SUPER);
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
                     Composition *model,
                     Output *out) override;

    void reset(Composition *model, Output *out) override;

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

    HicPtr hic_;
};

/**
 * Push the three per-context options and the layout into libhangul.
 *
 * Called at the top of every key, never cached, which is backend.h rule 4 and
 * is what makes the header's "a change takes effect immediately" promise true
 * for free. The cost is four field stores: hangul_ic_set_option() sets a bool
 * and hangul_ic_set_keyboard() sets a pointer and a table id
 * (hangulinputcontext.c:1484-1492). Neither touches the jamo buffer, which is
 * why PATHIME_OPT_HANGUL_LAYOUT can advertise itself composition-safe.
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

    model->settled += flushed;
    model->active.clear();
    out->commit += model->settled;
    model->settled.clear();
}

bool HangulContextBackend::process_key(const KeyEvent &key,
                                       const OptionReader &options,
                                       Composition *model,
                                       Output *out)
{
    apply_options(options);

    const int64_t preedit_mode = options.number(PATHIME_OPT_HANGUL_PREEDIT);
    /*
     * TODO(impl): PATHIME_HANGUL_PREEDIT_NONE is resolved and then treated as
     * PATHIME_HANGUL_PREEDIT_SYLLABLE by every branch below, which is *not*
     * the documented behaviour and is stated here rather than left to be
     * discovered. NONE holds nothing at all: each jamo is committed into the
     * client's document as it is struck and the syllable is built up by
     * deleting what was committed a moment ago and recommitting the fuller
     * form, so it is the only producer of Output::request_deletion() and the
     * only reason this library has a surrounding-text surface at all. Until
     * that slice lands a client that selects NONE gets a preedit it said it
     * could not display, rather than the in-document composition it asked for.
     * The engine-level requirement bits (src/engine.cc:196-219) already treat
     * NONE as needing both surrounding-text callbacks, so the plumbing it
     * needs is in place and only this side is missing.
     */

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
void HangulContextBackend::reset(Composition *model, Output *out)
{
    (void)model;
    (void)out;
    hangul_ic_reset(hic_.get());
}

/**
 * Hangul has no candidates, so there is nothing to select.
 *
 * The only candidate list libhangul could supply is hanja conversion, through
 * the separate HanjaTable/HanjaList lookup API, and hanja was cut in the API
 * review round. Nothing in this adapter ever puts anything in
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
 * only under ENABLE_EXTERNAL_KEYBOARDS (libhangul/hangul/hangul.h:99-103),
 * which the top-level CMakeLists.txt:34 turns off, and the nine built-in
 * layouts are static tables that resolve without any initialization — so
 * hangul is the one backend with no process-global setup at all. src/init.cc
 * and src/engine.cc already say this at their own call sites; this is the same
 * finding stated where the function is.
 *
 * data_dir is unused for the same reason: the only path libhangul would ever
 * want is the user keyboard directory, and that is exactly what
 * ENABLE_EXTERNAL_KEYBOARDS gates.
 */

bool hangul_global_init(const char *data_dir)
{
    (void)data_dir;
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
