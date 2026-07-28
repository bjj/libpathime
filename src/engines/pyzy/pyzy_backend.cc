/*
 * Implementation of the Chinese adapter declared in pyzy_backend.h.
 *
 * ---------------------------------------------------------------------------
 * The shape of one mutating call
 * ---------------------------------------------------------------------------
 *
 * Every mutating method here runs the same four steps, and the order is the
 * whole of Finding 5:
 *
 *   1. observer_.clear()      — so what is read afterward describes this call
 *   2. apply_options()        — options are pulled, never cached (rule 4)
 *   3. one PyZy::InputContext call, during which pyzy fires its six granular
 *      callbacks synchronously into observer_, which only records
 *   4. harvest()              — read the flags, copy out the parts that moved
 *
 * Nothing is assembled inside a callback, because at that moment pyzy is part
 * way through a mutation and its three preedit segments, candidate list and
 * input buffer are not consistent with one another. ibus-pinyin updates IBus
 * from inside each callback and is deliberately not the model
 * (docs/pyzy-mapping.md, impedance mismatch 2).
 *
 * ---------------------------------------------------------------------------
 * The three options pyzy itself has nothing behind
 * ---------------------------------------------------------------------------
 *
 * Each is wrapper logic in ibus-pinyin rather than something pyzy supplies, so
 * each needed a decision of its own rather than a shared "not our problem".
 * Two are implemented here and one is not:
 *
 *   - PATHIME_OPT_LATIN_WIDTH and PATHIME_OPT_PUNCTUATION_WIDTH are
 *     implemented, in punctuation.*, and reached from the two places
 *     process_key() below can emit a character pyzy will not take. There was
 *     no obstruction of the kind PATHIME_OPT_LEARNING ran into — only work —
 *     and full-width punctuation is ordinary Chinese orthography, not a
 *     nicety: an engine that cannot produce ，and 。is not one you could
 *     write Chinese with.
 *   - PATHIME_OPT_PINYIN_SHOW_RAW is implemented in harvest(), by appending
 *     the raw input to the auxiliary text under double pinyin — which is the
 *     only mode ibus-pinyin implements it for, and the only one where the two
 *     texts differ.
 *   - PATHIME_OPT_LEARNING is *not*, and reports itself unsupported for both
 *     pyzy ids rather than accepting a value it would ignore: pyzy has no
 *     public way to withhold the learning commit, and its learned data is
 *     process-global while the option is per-context. See apply_options().
 */

#include "engines/pyzy/pyzy_backend.h"

#include <cstddef>
#include <memory>
#include <string>

#include <PyZy/Const.h>
#include <PyZy/DataDir.h>
#include <PyZy/Variant.h>

#include "engines/pyzy/observer.h"
#include "engines/pyzy/punctuation.h"
#include "paths.h"

namespace pathime {
namespace {

/* ---------------------------------------------------------------------------
 * Process-global state
 *
 * One bool. pyzy's own global layer is a singleton Database plus a
 * SpecialPhraseTable (Finding 3), reached only through the two static members
 * of InputContext; all this records is whether we have called them, so that a
 * shutdown without an init, or a second init, is a no-op rather than an
 * unbalanced Database::finalize().
 * ------------------------------------------------------------------------ */

bool g_pyzy_ready = false;

/* ---------------------------------------------------------------------------
 * Option translation
 *
 * Both flag sets are written out pair by pair rather than derived from the
 * constant shift that happens to relate them today. The public bits are ABI
 * and cannot move; pyzy's are a vendored header we do not control, and a
 * silent renumbering there would turn a shift into wrong conversions with
 * nothing to catch it.
 * ------------------------------------------------------------------------ */

struct FlagPair {
    uint32_t ours;
    unsigned int theirs;
};

const FlagPair kFuzzyBits[] = {
    { PATHIME_PINYIN_FUZZY_C_CH,   PINYIN_FUZZY_C_CH },
    { PATHIME_PINYIN_FUZZY_CH_C,   PINYIN_FUZZY_CH_C },
    { PATHIME_PINYIN_FUZZY_Z_ZH,   PINYIN_FUZZY_Z_ZH },
    { PATHIME_PINYIN_FUZZY_ZH_Z,   PINYIN_FUZZY_ZH_Z },
    { PATHIME_PINYIN_FUZZY_S_SH,   PINYIN_FUZZY_S_SH },
    { PATHIME_PINYIN_FUZZY_SH_S,   PINYIN_FUZZY_SH_S },
    { PATHIME_PINYIN_FUZZY_L_N,    PINYIN_FUZZY_L_N },
    { PATHIME_PINYIN_FUZZY_N_L,    PINYIN_FUZZY_N_L },
    { PATHIME_PINYIN_FUZZY_F_H,    PINYIN_FUZZY_F_H },
    { PATHIME_PINYIN_FUZZY_H_F,    PINYIN_FUZZY_H_F },
    { PATHIME_PINYIN_FUZZY_L_R,    PINYIN_FUZZY_L_R },
    { PATHIME_PINYIN_FUZZY_R_L,    PINYIN_FUZZY_R_L },
    { PATHIME_PINYIN_FUZZY_K_G,    PINYIN_FUZZY_K_G },
    { PATHIME_PINYIN_FUZZY_G_K,    PINYIN_FUZZY_G_K },
    { PATHIME_PINYIN_FUZZY_AN_ANG, PINYIN_FUZZY_AN_ANG },
    { PATHIME_PINYIN_FUZZY_ANG_AN, PINYIN_FUZZY_ANG_AN },
    { PATHIME_PINYIN_FUZZY_EN_ENG, PINYIN_FUZZY_EN_ENG },
    { PATHIME_PINYIN_FUZZY_ENG_EN, PINYIN_FUZZY_ENG_EN },
    { PATHIME_PINYIN_FUZZY_IN_ING, PINYIN_FUZZY_IN_ING },
    { PATHIME_PINYIN_FUZZY_ING_IN, PINYIN_FUZZY_ING_IN }
};

/*
 * pyzy also defines PINYIN_FUZZY_IAN_IANG, _IANG_IAN, _UAN_UANG and _UANG_UAN
 * as aliases of the AN/ANG pair (Const.h:70-73), so those four rules ride on
 * PATHIME_PINYIN_FUZZY_AN_ANG and _ANG_AN and need no bits of their own. That
 * is a real narrowing — a client cannot tolerate "ian for iang" without also
 * tolerating "an for ang" — and it is pyzy's, not ours.
 */

const FlagPair kCorrectionBits[] = {
    { PATHIME_PINYIN_CORRECT_GN_NG,  PINYIN_CORRECT_GN_TO_NG },
    { PATHIME_PINYIN_CORRECT_MG_NG,  PINYIN_CORRECT_MG_TO_NG },
    { PATHIME_PINYIN_CORRECT_IOU_IU, PINYIN_CORRECT_IOU_TO_IU },
    { PATHIME_PINYIN_CORRECT_UEI_UI, PINYIN_CORRECT_UEI_TO_UI },
    { PATHIME_PINYIN_CORRECT_UEN_UN, PINYIN_CORRECT_UEN_TO_UN },
    { PATHIME_PINYIN_CORRECT_UE_VE,  PINYIN_CORRECT_UE_TO_VE },
    { PATHIME_PINYIN_CORRECT_V_U,    PINYIN_CORRECT_V_TO_U },
    { PATHIME_PINYIN_CORRECT_ON_ONG, PINYIN_CORRECT_ON_TO_ONG }
};

template <size_t N>
unsigned int translate_flags(int64_t ours, const FlagPair (&table)[N])
{
    unsigned int theirs = 0;
    for (size_t i = 0; i < N; ++i) {
        if ((static_cast<uint64_t>(ours) & table[i].ours) != 0) {
            theirs |= table[i].theirs;
        }
    }
    return theirs;
}

/**
 * The PROPERTY_CONVERSION_OPTION bitmask, assembled from the three options
 * that feed it.
 *
 * PATHIME_OPT_PINYIN_FUZZY and _CORRECTION are read for bopomofo contexts too.
 * The option table currently scopes both to Pinyin, so for a bopomofo context
 * they resolve to their tier-4 default of every bit — which is also pyzy's own
 * default (InputContext.h:181-182), so reading them unconditionally is both
 * correct today and already right on the day the fuzzy row is widened. The
 * trace behind that is in pyzy_backend.h's header comment: fuzzy rules really
 * are reachable from bopomofo, correction rules really are not.
 */
unsigned int conversion_option(const OptionReader &options)
{
    unsigned int mask = 0;
    if (options.flag(PATHIME_OPT_INCOMPLETE_INPUT)) {
        mask |= PINYIN_INCOMPLETE_PINYIN;
    }
    mask |= translate_flags(options.number(PATHIME_OPT_PINYIN_FUZZY), kFuzzyBits);
    mask |= translate_flags(options.number(PATHIME_OPT_PINYIN_CORRECTION), kCorrectionBits);
    return mask;
}

/**
 * PATHIME_OPT_CHINESE_VARIANT as pyzy's one simplified-or-traditional bool.
 *
 * options.cc narrows valid_values for these two engines to the two exclusive
 * values, so nothing else can arrive; the default arm exists because a
 * defaulted switch over an enum is the only form that stays warning-free on
 * every compiler, and simplified is the library default it falls back to.
 */
bool wants_simplified(const OptionReader &options)
{
    switch (options.number(PATHIME_OPT_CHINESE_VARIANT)) {
    case PATHIME_CHINESE_TRADITIONAL_ONLY:
        return false;
    case PATHIME_CHINESE_SIMPLIFIED_ONLY:
    default:
        return true;
    }
}

/** PATHIME_OPT_BOPOMOFO_LAYOUT as a pyzy BOPOMOFO_KEYBOARD_* value. */
unsigned int bopomofo_schema(const OptionReader &options)
{
    switch (options.number(PATHIME_OPT_BOPOMOFO_LAYOUT)) {
    case PATHIME_BOPOMOFO_LAYOUT_CHING_YEAH:
        return BOPOMOFO_KEYBOARD_CHING_YEAH;
    case PATHIME_BOPOMOFO_LAYOUT_ETEN:
        /* pyzy spells it ETAN (Const.h:92); the layout is Eten. */
        return BOPOMOFO_KEYBOARD_ETAN;
    case PATHIME_BOPOMOFO_LAYOUT_IBM:
        return BOPOMOFO_KEYBOARD_IBM;
    case PATHIME_BOPOMOFO_LAYOUT_STANDARD:
    default:
        return BOPOMOFO_KEYBOARD_STANDARD;
    }
}

/**
 * The double-pinyin half of PATHIME_OPT_PINYIN_SCHEME as a pyzy
 * DOUBLE_PINYIN_KEYBOARD_* value. Meaningless under
 * PATHIME_PINYIN_SCHEME_FULL, which selects a different InputType instead.
 */
unsigned int double_pinyin_schema(const OptionReader &options)
{
    switch (options.number(PATHIME_OPT_PINYIN_SCHEME)) {
    case PATHIME_PINYIN_SCHEME_DOUBLE_ZRM:
        return DOUBLE_PINYIN_KEYBOARD_ZRM;
    case PATHIME_PINYIN_SCHEME_DOUBLE_ABC:
        return DOUBLE_PINYIN_KEYBOARD_ABC;
    case PATHIME_PINYIN_SCHEME_DOUBLE_ZGPY:
        return DOUBLE_PINYIN_KEYBOARD_ZGPY;
    case PATHIME_PINYIN_SCHEME_DOUBLE_PYJJ:
        return DOUBLE_PINYIN_KEYBOARD_PYJJ;
    case PATHIME_PINYIN_SCHEME_DOUBLE_XHE:
        return DOUBLE_PINYIN_KEYBOARD_XHE;
    case PATHIME_PINYIN_SCHEME_DOUBLE_MSPY:
    default:
        return DOUBLE_PINYIN_KEYBOARD_MSPY;
    }
}

/**
 * True for the characters pyzy's parsers accept: [a-z] and the apostrophe
 * (FullPinyinContext.cc:41-50). Bopomofo accepts a wider set of ASCII —
 * digits and punctuation are symbol and tone keys on its layouts
 * (BopomofoContext::keyvalToBopomofo) — so the test is per input type, and
 * for bopomofo it is "any printable ASCII", with pyzy itself refusing the rest
 * by returning false from insert().
 *
 * Printable keysyms are the character's Unicode scalar value, which for ASCII
 * is the character, so the keysym is the char pyzy wants with no table in
 * between. That is the whole of Finding 6 for this backend.
 */
bool insertable(uint32_t keysym, PyZy::InputContext::InputType type, bool composing)
{
    if (type == PyZy::InputContext::BOPOMOFO) {
        /* Space is excluded deliberately: it is the convert key below, and no
         * bopomofo layout binds it. */
        return keysym > 0x0020 && keysym < 0x007f;
    }
    if (keysym == '\'') {
        /*
         * The apostrophe is pyzy's syllable separator, but only *between*
         * syllables. With nothing typed it is an opening quotation mark, and
         * this is the guard that lets it reach the emit path and become one —
         * ibus-pinyin's, arrived at from the other side: processPunct returns
         * FALSE on empty input before it ever gets to the apostrophe case
         * (PYPinyinEditor.cc:82-88).
         *
         * Without the guard pyzy takes it, accepts it, and renders nothing:
         * the key would produce an invisible composition and no text at all.
         */
        return composing;
    }
    return keysym >= 'a' && keysym <= 'z';
}

}  // namespace

/* ===========================================================================
 * PyzyContext
 * ======================================================================== */

PyzyContext::PyzyContext(pathime_engine_id_t id, const OptionReader &options)
    : type_(PyZy::InputContext::FULL_PINYIN), id_(id)
{
    recreate(wanted_type(options));
    apply_options(options);
}

PyzyContext::~PyzyContext()
{
    delete context_;
}

PyZy::InputContext::InputType PyzyContext::wanted_type(const OptionReader &options) const
{
    if (id_ == PATHIME_ENGINE_BOPOMOFO) {
        /* PATHIME_OPT_PINYIN_SCHEME is a Pinyin option and would resolve to
         * its default here; the engine id already decided this one. */
        return PyZy::InputContext::BOPOMOFO;
    }
    return (options.number(PATHIME_OPT_PINYIN_SCHEME) == PATHIME_PINYIN_SCHEME_FULL)
               ? PyZy::InputContext::FULL_PINYIN
               : PyZy::InputContext::DOUBLE_PINYIN;
}

void PyzyContext::recreate(PyZy::InputContext::InputType type)
{
    delete context_;
    context_ = PyZy::InputContext::create(type, &observer_);
    type_ = type;

    /*
     * A fresh context is empty, and that emptiness has to reach the model. Say
     * it in the same language a mutation would, by marking the flags harvest()
     * reads — so the next harvest copies the (empty) segments over whatever
     * the previous context had left behind, and drops its candidate list.
     */
    observer_.input_text = true;
    observer_.cursor = true;
    observer_.preedit = true;
    observer_.auxiliary = true;
    observer_.candidates = true;

    /* Same reasoning one level up: an unclosed quote belongs to the run of
     * text that opened it, and this is a new one. */
    punctuation_.clear();
}

void PyzyContext::apply_options(const OptionReader &options)
{
    /*
     * The phonetic type first, because changing it means a different object.
     * pyzy fixes InputType at create() (InputContext.cc:67-81), which is why
     * PATHIME_OPT_PINYIN_SCHEME is documented as resetting the composition;
     * rebuilding here is what makes that documented behaviour real, since
     * ContextBackend has no "replace me" hook and the core cannot know that
     * one vendor's context is cheap to throw away.
     */
    const PyZy::InputContext::InputType type = wanted_type(options);
    if (type != type_) {
        recreate(type);
    }
    if (context_ == nullptr) {
        return;
    }

    context_->setProperty(PyZy::InputContext::PROPERTY_CONVERSION_OPTION,
                          PyZy::Variant::fromUnsignedInt(conversion_option(options)));
    context_->setProperty(PyZy::InputContext::PROPERTY_MODE_SIMP,
                          PyZy::Variant::fromBool(wants_simplified(options)));
    context_->setProperty(PyZy::InputContext::PROPERTY_SPECIAL_PHRASE,
                          PyZy::Variant::fromBool(options.flag(PATHIME_OPT_SPECIAL_PHRASES)));

    /*
     * The two schema properties are the ones PhoneticContext::setProperty
     * refuses (docs/pyzy-mapping.md, "Negotiation"): each is honoured only in
     * the subclass it belongs to and returns false everywhere else. Sending
     * only the one this context can honour keeps a false return meaning
     * something rather than being the normal case — verified by probe: on a
     * FULL_PINYIN context both schema setters return false, on a BOPOMOFO
     * context PROPERTY_BOPOMOFO_SCHEMA returns true.
     */
    if (type_ == PyZy::InputContext::BOPOMOFO) {
        context_->setProperty(PyZy::InputContext::PROPERTY_BOPOMOFO_SCHEMA,
                              PyZy::Variant::fromUnsignedInt(bopomofo_schema(options)));
    } else if (type_ == PyZy::InputContext::DOUBLE_PINYIN) {
        context_->setProperty(PyZy::InputContext::PROPERTY_DOUBLE_PINYIN_SCHEMA,
                              PyZy::Variant::fromUnsignedInt(double_pinyin_schema(options)));
    }

    /*
     * PATHIME_OPT_LEARNING is deliberately not consulted here, and there is
     * nothing missing: src/options.cc excludes both pyzy ids from that option's
     * engine set, so a client trying to set it gets PATHIME_ERROR_UNSUPPORTED
     * and never reaches this function with an expectation to disappoint.
     *
     * The reason it cannot be implemented: pyzy learns unconditionally on its
     * commit path — PhraseEditor::commit() is reached from inside
     * selectCandidate() and commit(), neither of which takes a flag — and the
     * public header exposes no switch, only resetCandidate() to unlearn one
     * entry after the fact. The user database is process-global besides
     * (Finding 3), so even a hack could not be per-context, which this option
     * is. Reporting unsupported is the decision; see the descriptor comment in
     * src/options.cc for the alternative that was turned down.
     */
}

void PyzyContext::harvest(const OptionReader &options, Composition *model, Output *out)
{
    if (context_ == nullptr) {
        return;
    }

    /*
     * Each assignment is a copy out of a reference into a pyzy-internal buffer
     * (PhoneticContext.h:74-97), valid only until the next mutating call —
     * backend.h rule 1, and the reason nothing here is aliased.
     *
     * The three-part preedit maps onto the model one-for-one, which is not a
     * coincidence: composition.h was designed around it. No conversion is
     * needed in either direction, and in particular cursor() is not consulted.
     * It is a byte offset into the raw ASCII input (Finding 4) — a position in
     * a different string from anything the API carries — and this slice has
     * nothing to say about it, so it is read nowhere.
     */
    if (observer_.preedit) {
        model->settled = context_->selectedText();
        model->active = context_->conversionText();
        model->tail = context_->restText();
    }

    /*
     * The auxiliary text is passed through as pyzy renders it, including the
     * '|' it puts at the cursor: "ni hao|" for pinyin, "ㄋㄧˇ,ㄏㄠˇ|" for
     * bopomofo (PinyinContext.cc:160-208). That is supplemental text a client
     * shows beside the preedit and never commits, which is exactly what a
     * rendering of segmentation and cursor state is — so unlike the same
     * marker appearing in the preedit, it belongs.
     *
     * PATHIME_OPT_PINYIN_SHOW_RAW appends the keys as typed, in the brackets
     * ibus-pinyin uses (PYDoublePinyinEditor.cc:38-63) minus the run of spaces
     * it pads them with, which is layout and belongs to the client.
     *
     * Only under double pinyin, which is the whole of ibus-pinyin's own
     * implementation — its config key is literally "DoublePinyinShowRaw" and
     * nothing but DoublePinyinEditor reads it. Under full pinyin the raw keys
     * and the decoded syllables are the same characters, so the option would
     * append a duplicate of what is already there; the header says it is
     * meaningless in that mode, and this is what makes that true rather than
     * merely stated. Bopomofo is out for the same reason it is out of the
     * option's engine set in src/options.cc: its auxiliary text is zhuyin and
     * its raw keys are Latin, but nobody typing zhuyin is checking a spelling
     * against a scheme they are still learning.
     */
    if (observer_.auxiliary) {
        model->auxiliary = context_->auxiliaryText();
        if (type_ == PyZy::InputContext::DOUBLE_PINYIN &&
            options.flag(PATHIME_OPT_PINYIN_SHOW_RAW) &&
            !context_->inputText().empty()) {
            model->auxiliary += " [ ";
            model->auxiliary += context_->inputText();
            model->auxiliary += " ]";
        }
    }

    /*
     * A regenerated list makes everything already materialized stale. It is
     * dropped rather than appended to, which is what lets
     * materialize_candidates() resume from candidates.size() and still honour
     * "never reorder what is already there". The hover goes back to 0 with it,
     * because a new span is being described (composition.h, "The candidate
     * cursor") — which is also what pyzy does to its own focused index
     * (PhoneticContext.cc:92-96).
     */
    if (observer_.candidates) {
        model->candidates.clear();
        model->cursor = 0;
    }

    if (observer_.committed) {
        out->commit += observer_.commit_text;
        punctuation_.note_commit(observer_.commit_text);
    }

    observer_.clear();
}

bool PyzyContext::process_key(const KeyEvent &key,
                              const OptionReader &options,
                              const SurroundingTextView &doc,
                              Composition *model,
                              Output *out)
{
    /* Unused: pyzy holds its composition in the preedit, so it never revises
     * text already in the client's document. See backend.h. */
    (void)doc;

    if (context_ == nullptr) {
        return false;
    }

    /*
     * Chorded keys are the client's shortcuts, declined before anything else.
     * pyzy has no modifier concept at all, so a Control- or Alt- combination
     * could only be turned into a plain character, and swallowing Ctrl+A in a
     * text field is exactly the bug that produces. Shift needs no test: it
     * shows up as an uppercase keysym, which insertable() already rejects for
     * pinyin. CapsLock and NumLock are latched state rather than chords and
     * are ignored, as ibus-pinyin ignores them (cmshm_filter).
     */
    if (key.has(PATHIME_MOD_CONTROL) || key.has(PATHIME_MOD_ALT) ||
        key.has(PATHIME_MOD_SUPER)) {
        return false;
    }

    observer_.clear();
    apply_options(options);
    if (context_ == nullptr) {
        return false;
    }

    bool handled = false;

    if (insertable(key.keysym, type_, !context_->inputText().empty())) {
        /*
         * insert() returns false only for a character this input type cannot
         * take; a full buffer returns true and silently drops the character
         * (docs/pyzy-mapping.md, "Handled / Unhandled"). Both of those are the
         * right answer for us as they stand: a rejected character was never
         * ours, and a dropped one was — and a rejection falls through to the
         * emit path below, which is where the character it could not take
         * becomes text of ours instead.
         */
        handled = context_->insert(static_cast<char>(key.keysym));
    }

    if (!handled && !context_->inputText().empty()) {
        switch (key.keysym) {
        case PATHIME_KEY_SPACE:
            /*
             * The convert key. Selecting the hovered candidate is the pinyin
             * convention and what ibus-pinyin does (PYPhoneticEditor.cc:74-79);
             * with no candidate to select it falls through to a commit, which
             * is how a spelling nothing matches still reaches the document.
             */
            if (model->cursor < model->candidates.size()) {
                context_->selectCandidate(model->cursor);
            } else {
                context_->commit(PyZy::InputContext::TYPE_CONVERTED);
            }
            handled = true;
            break;

        case PATHIME_KEY_RETURN:
            /*
             * TYPE_CONVERTED commits the selected prefix plus the *raw* input
             * for whatever is not yet selected — verified by probe: with
             * "nihao" typed and nothing selected it commits "nihao", not the
             * 你好 the preedit is showing. That is ibus-pinyin's Return
             * behaviour too (PYPhoneticEditor.cc:98-101) and the convention:
             * Return means "give me what I typed", Space means "convert it".
             */
            context_->commit(PyZy::InputContext::TYPE_CONVERTED);
            handled = true;
            break;

        case PATHIME_KEY_ESCAPE:
            /*
             * Discards without committing, which is pyzy's reset() semantics
             * and the API's (pathime_context_reset). The core clears the model
             * only on its own reset path, so harvest() below is what empties
             * it here — the flags pyzy fires from inside reset() are exactly
             * the ones that do it.
             */
            context_->reset();
            handled = true;
            break;

        case PATHIME_KEY_BACKSPACE:
            /*
             * Plain character deletion, deliberately not ibus-pinyin's
             * "unselect the last candidate first" (PYPhoneticEditor.cc:103-107).
             * composition.h defines `settled` as text the engine will not
             * revisit, and unselectCandidates() revisits it. Backspace over a
             * settled span is a real feature, but it is one the composition
             * model would have to grow a concept for, not one to smuggle in.
             */
            context_->removeCharBefore();
            handled = true;
            break;

        case PATHIME_KEY_DELETE:
            context_->removeCharAfter();
            handled = true;
            break;

        /*
         * The cursor keys move within the *raw input* — pyzy's cursor, the
         * byte offset of Finding 4 — not within the output and not within the
         * candidate list. Up, Down, Page Up and Page Down are absent on
         * purpose: they navigate the candidate list, which docs/CONCEPTS.md
         * puts on the client's side, and the API has no operation for moving
         * the hover anyway. A client scrolls its own list and calls
         * pathime_context_select_candidate().
         *
         * The four cursor keys are **declined**, and the reason is the
         * composition model rather than pyzy. A Composition has no cursor
         * inside a span (composition.h): the API carries a settled boundary
         * and nothing else, so there is no position for these keys to move and
         * nothing the client could be told about the result. The anthy adapter
         * declines them for exactly the same reason.
         *
         * Routing them to pyzy was tried first and is what made the reason
         * concrete. While its cursor sits anywhere but the end of the input,
         * pyzy stops putting converted Chinese in conversionText() and puts
         * the spelled-out syllables with a literal '|' at the cursor there
         * instead (PinyinContext.cc:129-142, the `m_text.size () != m_cursor`
         * arm) — typing "nihao" then Left really does make the preedit read
         * "ni h|a". That marker is pyzy rendering its own cursor into a string
         * the API promises is plain content text, and the API has nowhere to
         * say otherwise. Filtering the marker out would hide the state rather
         * than express it; declining the keys means the state never arises.
         *
         * The cost, stated plainly: a user cannot go back and repair the
         * middle of a long pinyin run without backspacing to it. The
         * phone-keyboard target has no key that would move that cursor anyway,
         * and if a future model grows a within-span cursor this is one edit.
         */
        case PATHIME_KEY_LEFT:
        case PATHIME_KEY_RIGHT:
        case PATHIME_KEY_HOME:
        case PATHIME_KEY_END:
            break;

        default:
            /*
             * Not a composition key. If it is printable it is the emit path's
             * below; if it is not — Tab, a function key, Up and Down, which
             * navigate a candidate list docs/CONCEPTS.md puts on the client's
             * side — it is the client's.
             *
             * Never swallowed, which is where this parts company with
             * ibus-pinyin: its editor returns TRUE for every key while
             * composing (PYPhoneticEditor.cc:161-162), so with its auto-commit
             * option off a comma typed mid-composition simply vanishes.
             */
            handled = false;
            break;
        }
    }

    /*
     * The emit path: a printable key pyzy would not take becomes text of ours,
     * shaped by PATHIME_OPT_LATIN_WIDTH and PATHIME_OPT_PUNCTUATION_WIDTH.
     * This is ibus-pinyin's FallbackEditor (punctuation.h says where and why),
     * and it is the whole of what those two options mean for these engines.
     *
     * Two consequences worth being explicit about, because both are visible to
     * a client:
     *
     *   - It runs *after* the switch, so a key the switch claimed never
     *     reaches it. Space is the case that matters: while composing it is
     *     the convert key, and only with nothing composing does it become an
     *     ordinary space (or U+3000 at full width). ibus-pinyin arrives at the
     *     same split through processSpace returning FALSE on empty input
     *     (PYPhoneticEditor.cc:68-69).
     *   - It makes these engines *handle* every printable key, including the
     *     ones they merely pass through unchanged at half width. That is
     *     correct for an IME the user has switched into — ibus-pinyin's
     *     FallbackEditor does the same — and it is the reason the width
     *     options can be honoured at all: a key the client inserts itself is
     *     one we never saw.
     */
    std::string emitted;
    if (!handled && emittable(key.keysym)) {
        finish_composition(*model);
        emitted = emit_text(static_cast<char>(key.keysym), width_settings(options),
                            &punctuation_);
        handled = true;
    }

    harvest(options, model, out);

    /*
     * After the harvest, so that a composition ended just above lands in
     * out->commit first and the punctuation follows the text it punctuates.
     * The same order is why note_commit() is called here rather than inside
     * emit_text(): harvest() has just recorded pyzy's commit, and ours is the
     * later of the two.
     */
    out->commit += emitted;
    punctuation_.note_commit(emitted);
    return handled;
}

void PyzyContext::finish_composition(const Composition &model)
{
    if (context_->inputText().empty()) {
        return;
    }

    /*
     * Select then commit, which is ibus-pinyin's auto-commit path verbatim
     * (PYPinyinEditor.cc:117-122). The two steps are not redundant:
     * selectCandidate() settles the hovered candidate and moves on to the next
     * phonetic segment rather than finishing, so the commit is what disposes
     * of whatever is left — as raw input, which is the same TYPE_CONVERTED
     * bargain Return makes.
     *
     * Selecting the hover rather than committing outright is the point. The
     * client has been shown 你好 for "nihao" and has gone on to type the comma
     * after it; taking the conversion the user was looking at is what makes
     * that read as one sentence rather than as an abandoned composition.
     */
    if (model.cursor < model.candidates.size()) {
        context_->selectCandidate(model.cursor);
    }
    context_->commit(PyZy::InputContext::TYPE_CONVERTED);

    /*
     * Recorded here and not left to harvest(), because the character about to
     * be emitted reads this flag and harvest() does not run until afterwards.
     * This is what makes "1好." end in 。while "1.5" keeps its decimal point:
     * the 好 committed a moment ago is what the period is following, not the
     * digit before it.
     */
    punctuation_.note_commit(observer_.commit_text);
}

void PyzyContext::reset(Composition *model, Output *out)
{
    /*
     * @a model and @a out are untouched on purpose. pyzy's reset() discards
     * without committing (docs/pyzy-mapping.md, "Reset"), which is exactly what
     * the API asks for, so there is no text that must not be lost and nothing
     * to put in @a out; and backend.h says the caller clears @a model
     * afterward regardless. Harvesting here would be work whose result is
     * about to be overwritten.
     */
    (void)model;
    (void)out;

    /*
     * Cleared even when there is no pyzy context to reset, because this state
     * is ours and not pyzy's. A reset is the API's "start again" — the client
     * has changed document, or lost focus and come back to a different one —
     * and carrying a half-open quotation mark across that is exactly the bug
     * ibus-pinyin's FallbackEditor::reset() avoids.
     */
    punctuation_.clear();

    if (context_ == nullptr) {
        return;
    }

    observer_.clear();
    context_->reset();
    observer_.clear();
}

void PyzyContext::options_changed(const OptionReader &options,
                                  Composition *model,
                                  Output *out)
{
    /*
     * The one adapter that needs this hook, and the reason backend.h has it.
     *
     * pyzy's options are *properties pushed into a live InputContext*, not
     * values consulted at the point of use, and setting one regenerates the
     * candidate list through the observer. So pulling them at the top of the
     * next mutating call is too late: the header promises a change takes
     * effect immediately, and between two keystrokes there is no next call.
     * Without this, setting PATHIME_OPT_CHINESE_VARIANT mid-composition left
     * the traditional candidates on screen until the user typed again.
     *
     * apply_options() may also rebuild the whole context when the phonetic
     * type changed — but PATHIME_OPT_PINYIN_SCHEME is declared
     * resets_composition, so the core resets instead of coming here, and that
     * path is not reached for it.
     */
    if (context_ == nullptr) {
        return;
    }

    observer_.clear();
    apply_options(options);

    /*
     * Recompute the auxiliary text even though pyzy's own has not moved. It is
     * the same problem the candidate drop below solves, one line earlier:
     * PATHIME_OPT_PINYIN_SHOW_RAW is *ours*, so pyzy fires no auxiliaryChanged
     * for it and harvest() would leave the old string in place until the next
     * keystroke — which for an option toggled mid-composition may never come.
     */
    observer_.auxiliary = true;
    harvest(options, model, out);

    /*
     * Drop the materialized list even when pyzy did not tell us to.
     *
     * setProperty() stores the new value but does not regenerate anything —
     * no candidatesChanged fires, so harvest() above leaves the list alone —
     * and yet the entries that come back from getCandidate() afterwards really
     * are different, because the simplified/traditional choice is applied as
     * each candidate is produced. Measured: without this, setting
     * PATHIME_OPT_CHINESE_VARIANT mid-composition left the traditional 離 at
     * index 5 until the user typed one more key, at which point the real
     * candidatesChanged arrived and it became 离.
     *
     * So the list is dropped here and the core's pump refills it. Positions
     * may move, which is allowed: the caller forces composition_changed for an
     * option set, so the client is told to re-read before it can act on a
     * stale index. Refetching a list that did not need it is the cost, and it
     * is bounded by the cap and paid only when a client changes an option
     * mid-composition.
     */
    model->candidates.clear();
    model->cursor = 0;
}

pathime_status_t PyzyContext::select_candidate(size_t index,
                                               const OptionReader &options,
                                               Composition *model,
                                               Output *out)
{
    if (context_ == nullptr) {
        return PATHIME_ERROR_BACKEND;
    }

    observer_.clear();
    apply_options(options);
    if (context_ == nullptr) {
        return PATHIME_ERROR_BACKEND;
    }

    /*
     * selectCandidate() settles the chosen text into selectedText() and
     * regenerates the list for the next phonetic segment; when the input is
     * exhausted it commits instead, firing commitText from inside this call
     * (docs/pyzy-mapping.md, impedance mismatch 4 and 8). Both outcomes reach
     * the model through the same harvest, which is the point of doing it that
     * way. Note the consequence for callers: after this returns, the same index
     * means a different candidate.
     *
     * The index is already known to be in range against the list this adapter
     * materialized, so a false return means pyzy's list moved out from under
     * that — a backend disagreement rather than a caller error.
     */
    const bool ok = context_->selectCandidate(index);
    harvest(options, model, out);
    return ok ? PATHIME_OK : PATHIME_ERROR_BACKEND;
}

void PyzyContext::materialize_candidates(size_t cap,
                                         const OptionReader &options,
                                         Composition *model)
{
    /*
     * Nothing to pull: the one option that governs this pump is
     * PATHIME_OPT_MAX_CANDIDATES, and the core has already resolved it into
     * @a cap.
     */
    (void)options;

    if (context_ == nullptr) {
        return;
    }

    /*
     * Resuming from the current size is what makes raising the cap cheap and
     * safe — already-materialized entries keep their positions, which is the
     * promise PATHIME_OPT_MAX_CANDIDATES makes to a client scrolling a growing
     * list. harvest() is the only thing that ever shortens this list, and it
     * does so wholesale when pyzy says the list was regenerated.
     *
     * hasCandidate(i) is the lazy, *mutating* query (PhoneticContext.cc:231-250)
     * that this whole method exists for. It fires no Observer callbacks —
     * verified by probe, where pumping ten candidates produced none — so it is
     * safe to run outside the clear/apply/mutate/harvest sequence above.
     * Running out before the cap is the normal case: "nihao" prepares twelve.
     */
    for (size_t i = model->candidates.size(); i < cap; ++i) {
        if (!context_->hasCandidate(i)) {
            break;
        }

        PyZy::Candidate candidate;
        if (!context_->getCandidate(i, candidate)) {
            break;
        }

        /*
         * Text only. Candidate::type (NORMAL_PHRASE, USER_PHRASE,
         * SPECIAL_PHRASE) is metadata the plain-text rule excludes
         * (docs/pyzy-mapping.md, impedance mismatch 10); ibus-pinyin uses it to
         * colour its lookup table, which is presentation.
         */
        model->candidates.push_back(candidate.text);
    }
}

/* ===========================================================================
 * PyzyEngine
 * ======================================================================== */

std::unique_ptr<ContextBackend> PyzyEngine::create_context(const OptionReader &options)
{
    std::unique_ptr<PyzyContext> ctx(new PyzyContext(id_, options));
    if (!ctx->valid()) {
        /* InputContext::create() warns and returns NULL only for an InputType
         * it does not know (InputContext.cc:76-78), which wanted_type() cannot
         * produce — so this is a defensive path, not an expected one. */
        return nullptr;
    }
    return ctx;
}

/* ===========================================================================
 * The process-global layer
 * ======================================================================== */

bool pyzy_global_init(const char *data_dir, const char *resource_dir)
{
    if (g_pyzy_ready) {
        return true;
    }
    if (data_dir == nullptr || data_dir[0] == '\0') {
        /*
         * Checked here rather than trusted, because PyZy::InputContext::init()
         * answers an empty directory with g_error() (InputContext.cc:48-53),
         * which aborts the process. init.cc never passes one; this is the
         * assertion that says so out loud.
         */
        return false;
    }

    /*
     * Where pyzy reads its shipped data: main.db, and the phrases.txt that
     * backs PATHIME_OPT_SPECIAL_PHRASES when the client has not put one of its
     * own under data_dir. pyzy_set_data_dir() has to be called before
     * InputContext::init(), which is where the database is opened.
     */
    const std::string pyzy_resources = path_join(resource_dir, "pyzy");
    pyzy_set_data_dir(pyzy_resources.c_str());

    /*
     * Whether the database is there has to be asked in *front* of
     * PyZy::InputContext::init() rather than behind it. Behind it there is
     * nothing left to ask: init() returns void, and beneath it Database::init()
     * constructs the singleton whether or not open() found anything
     * (Database.cc:202-208, 729-734), so a broken installation and a working
     * one are indistinguishable — pyzy says so with a g_warning and nothing
     * more.
     *
     * The tempting stronger check — convert a syllable, see whether a candidate
     * comes back — is what this deliberately is not. With no database open m_db
     * is NULL and the query path dereferences it, so the probe meant to detect
     * the broken installation is the thing that crashes on it.
     *
     * Returning false here leaves g_pyzy_ready false, so pathime_has_engine()
     * reports both PATHIME_ENGINE_PINYIN and PATHIME_ENGINE_BOPOMOFO
     * unavailable, which is exactly what the header documents for an engine
     * "whose runtime prerequisites, such as its dictionaries, are unavailable".
     * It also leaves PyZy::InputContext::init() uncalled, which is what keeps
     * pyzy_global_shutdown()'s finalize() balanced.
     */
    if (!is_regular_file(path_join(pyzy_resources, "main.db"))) {
        return false;
    }

    /*
     * Both directories are rooted at data_dir, which is the whole point of
     * pathime_init_params_t::data_dir: pyzy would otherwise take the cache
     * directory from XDG_CACHE_HOME and the config directory from
     * XDG_CONFIG_HOME (InputContext.cc:36-43), and a client asking for a second
     * profile would get the same files. They are kept distinct beneath one
     * "pyzy" directory even though pyzy permits them to be the same path,
     * because they hold different kinds of thing: cache/ is the learned-phrase
     * database pyzy writes, config/ is where a client may drop its own
     * phrases.txt for PATHIME_OPT_SPECIAL_PHRASES. Neither is created here —
     * pyzy calls g_mkdir_with_parents itself when it first saves
     * (Database.cc:392-394).
     */
    const std::string root(data_dir);
    PyZy::InputContext::init(path_join(path_join(root, "pyzy"), "cache"),
                             path_join(path_join(root, "pyzy"), "config"));

    /*
     * True, and by now it has been earned rather than assumed: the database
     * check ran in front of init(), because after it there is nothing left to
     * ask.
     */
    g_pyzy_ready = true;
    return true;
}

void pyzy_global_shutdown()
{
    if (!g_pyzy_ready) {
        return;
    }

    /*
     * This is where the user-database save gets driven (TODO.md §5), and the
     * entry point is the one the public header already has: finalize().
     *
     * The chain is finalize() -> Database::finalize() (Database.cc:736-740),
     * which resets the singleton and so runs ~Database (Database.cc:211-219) —
     * and that destructor calls saveUserDB() whenever a save is outstanding.
     * "Outstanding" is m_timeout_id != 0, which Database::modified() sets from
     * g_timeout_add_seconds after every learning write (Database.cc:460-470).
     * The timeout itself never fires, exactly as TODO.md §5 says, because
     * nothing runs a GMainLoop; but g_timeout_add_seconds still returns a
     * non-zero source id without one, so the flag the destructor tests is set
     * regardless and the save happens here.
     *
     * So there is no separate save call to find, and none is missing: the
     * requirement is that finalize() is *reached*, which is what makes
     * pathime_shutdown() the operation a client must not skip. A process that
     * exits without it loses the session's learning — pyzy's own timer would
     * not have saved it either.
     */
    PyZy::InputContext::finalize();
    g_pyzy_ready = false;
}

std::unique_ptr<EngineBackend> pyzy_create_engine(pathime_engine_id_t id)
{
    if (!g_pyzy_ready) {
        return nullptr;
    }
    if (id != PATHIME_ENGINE_PINYIN && id != PATHIME_ENGINE_BOPOMOFO) {
        return nullptr;
    }
    return std::unique_ptr<EngineBackend>(new PyzyEngine(id));
}

}  // namespace pathime
