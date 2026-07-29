/*
 * Implementation of the width and punctuation layer declared in punctuation.h.
 *
 * Both tables are transcribed from refs/ibus-pinyin/src/PYFallbackEditor.cc:
 * kSimplified from processPunctForSimplifiedChinese (line 27) and
 * kTraditional from processPunctForTraditionalChinese (line 97). The keys
 * ibus-pinyin comments *out* of both — @ # % & * - = + | / — are absent here
 * too: they have no Chinese form, and leaving them to the width conversion is
 * the same answer by a different route.
 */

#include "punctuation.h"

#include "utf8.h"

namespace pathime {
namespace {

/** One ASCII key and the Chinese punctuation it stands for. */
struct PunctRow {
    char        key;
    const char *text;
};

/*
 * Simplified Chinese. Three rows are absent because they are decided at
 * emit_text() rather than here: the two quotes alternate and the period looks
 * behind it.
 */
const PunctRow kSimplified[] = {
    {'`',  "·"},
    {'~',  "～"},
    {'!',  "！"},
    {'$',  "￥"},
    {'^',  "……"},
    {'(',  "（"},
    {')',  "）"},
    {'_',  "——"},
    {'[',  "【"},
    {']',  "】"},
    {'{',  "『"},
    {'}',  "』"},
    {'\\', "、"},
    {';',  "；"},
    {':',  "："},
    {',',  "，"},
    {'<',  "《"},
    {'>',  "》"},
    {'?',  "？"},
};

/*
 * Traditional Chinese. It differs in four places and the differences are the
 * reason there are two tables rather than one: the corner brackets 「」 rather
 * than the black lenticular 【】, the `<` and `>` keys standing for the comma
 * and full stop rather than the double angle brackets, and no row at all for
 * the backtick, which therefore falls through to the width conversion.
 */
const PunctRow kTraditional[] = {
    {'~',  "～"},
    {'!',  "！"},
    {'$',  "￥"},
    {'^',  "……"},
    {'(',  "（"},
    {')',  "）"},
    {'_',  "——"},
    {'[',  "「"},
    {']',  "」"},
    {'{',  "『"},
    {'}',  "』"},
    {'\\', "、"},
    {';',  "；"},
    {':',  "："},
    {',',  "，"},
    {'<',  "，"},
    {'>',  "。"},
    {'?',  "？"},
};

/** The row for @a c in the table @a settings selects, or nullptr. */
const char *punct_lookup(char c, const WidthSettings &settings)
{
    const PunctRow *table = settings.simplified ? kSimplified : kTraditional;
    const size_t count = settings.simplified
                             ? sizeof(kSimplified) / sizeof(kSimplified[0])
                             : sizeof(kTraditional) / sizeof(kTraditional[0]);
    for (size_t i = 0; i < count; ++i) {
        if (table[i].key == c) {
            return table[i].text;
        }
    }
    return nullptr;
}

/**
 * The full-width form of a printable ASCII character.
 *
 * Two rows of ibus-pinyin's HalfFullConverter table are all that a printable
 * ASCII input can reach (PYHalfFullConverter.cc:28-29): space maps to the
 * ideographic space, and the other 94 characters map to the Halfwidth and
 * Fullwidth Forms block in order. The rest of that table converts halfwidth
 * katakana and jamo back to full width, which nothing here produces — so this
 * is the whole of it for us, expressed as the arithmetic it is.
 */
std::string to_fullwidth(char c)
{
    std::string out;
    const uint32_t scalar = static_cast<uint32_t>(static_cast<unsigned char>(c));
    if (scalar == 0x20) {
        utf8_append_scalar(out, 0x3000);
    } else {
        utf8_append_scalar(out, 0xFF01 + (scalar - 0x21));
    }
    return out;
}

/** Letters, digits and space — what PATHIME_OPT_LATIN_WIDTH governs. */
bool is_latin(char c)
{
    return c == ' ' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z');
}

}  // namespace

WidthSettings width_settings(const OptionReader &options)
{
    WidthSettings settings;
    settings.latin = static_cast<pathime_width_t>(
        options.number(PATHIME_OPT_LATIN_WIDTH));
    settings.punctuation = static_cast<pathime_width_t>(
        options.number(PATHIME_OPT_PUNCTUATION_WIDTH));
    /*
     * By preference rather than by exclusion, because the table engine accepts
     * all five variant values where pyzy accepts two. WidthSettings::simplified
     * says why ANY lands on the simplified table.
     */
    const int64_t variant = options.number(PATHIME_OPT_CHINESE_VARIANT);
    settings.simplified = variant != PATHIME_CHINESE_TRADITIONAL_ONLY &&
                          variant != PATHIME_CHINESE_TRADITIONAL_FIRST;
    return settings;
}

bool emittable(uint32_t keysym)
{
    return keysym >= 0x0020 && keysym <= 0x007e;
}

std::string emit_text(char c, const WidthSettings &settings, PunctuationState *state)
{
    if (is_latin(c)) {
        return settings.latin == PATHIME_WIDTH_FULL ? to_fullwidth(c)
                                                    : std::string(1, c);
    }

    if (settings.punctuation != PATHIME_WIDTH_FULL) {
        /*
         * Half-width punctuation is the ASCII character itself, and the quote
         * state is deliberately left alone: nothing alternated, so a quote
         * typed later at full width still opens.
         */
        return std::string(1, c);
    }

    switch (c) {
    case '\'': {
        const char *text = state->quote_open ? "‘" : "’";
        state->quote_open = !state->quote_open;
        return text;
    }
    case '"': {
        const char *text = state->double_quote_open ? "“" : "”";
        state->double_quote_open = !state->double_quote_open;
        return text;
    }
    case '.':
        /*
         * The one look-behind rule: a full stop directly after a digit is a
         * decimal point, so "1.5" survives instead of becoming "1。5"
         * (PYFallbackEditor.cc:80-85).
         */
        return state->prev_was_digit ? std::string(1, c) : "。";
    default:
        break;
    }

    const char *substitution = punct_lookup(c, settings);
    if (substitution != nullptr) {
        return substitution;
    }

    /*
     * No Chinese form for this key. It still gets the full-width treatment,
     * which is where this parts company with ibus-pinyin — it falls back to
     * the *Latin* width flag here (PYFallbackEditor.cc:198), so its `@` stays
     * ASCII while its `!` does not. The header says punctuation width is "the
     * same choice for punctuation", and the anthy front end already reads it
     * that way, so one option governing one class of character wins over
     * fidelity to a split the reference never explains.
     */
    return to_fullwidth(c);
}

namespace {

/**
 * Set @a next_is_open from the nearest of @a open / @a close in @a text, and
 * report whether either was there at all.
 *
 * Searched from the end, and the *first* one found from that direction wins:
 * the most recent mark is the one whose partner the next quote must be. Both
 * are looked for in one pass rather than two so that "which came last" is
 * answered without comparing two positions.
 */
bool last_quote_decides(std::string_view text, const char *open, const char *close,
                        bool *next_is_open)
{
    const size_t open_at = text.rfind(open);
    const size_t close_at = text.rfind(close);

    if (open_at == std::string_view::npos && close_at == std::string_view::npos) {
        return false;
    }
    /* An opening mark most recently means the next one closes it, and the
     * other way round. npos compares below every real position, which is the
     * right answer when only one kind is present. */
    const bool open_is_later =
        close_at == std::string_view::npos ||
        (open_at != std::string_view::npos && open_at > close_at);
    *next_is_open = !open_is_later;
    return true;
}

}  // namespace

void observe_document(std::string_view before_cursor, PunctuationState *state)
{
    if (before_cursor.empty()) {
        /* Nothing visible: every tracked value stands. This is the no-snapshot
         * case and the caret-at-the-start case, which are the same answer. */
        return;
    }

    /*
     * The digit look-behind, which needs exactly one scalar. Walking back over
     * continuation bytes is what makes this correct for a document ending in a
     * multi-byte character: 好 must not be mistaken for a digit because its
     * last byte happens to fall in the ASCII digit range — it cannot, since
     * continuation bytes are all >= 0x80, but the walk is what makes that a
     * property of the code rather than of the encoding table.
     */
    size_t last = before_cursor.size() - 1;
    while (last > 0 &&
           (static_cast<unsigned char>(before_cursor[last]) & 0xC0u) == 0x80u) {
        last--;
    }
    const bool one_byte = last == before_cursor.size() - 1;
    const char c = before_cursor[last];
    state->prev_was_digit = one_byte && c >= '0' && c <= '9';

    /*
     * The two alternations, each answered only if its own kind of mark is
     * visible. They are independent: a document may show a “ and no ‘, and
     * learning about one says nothing about the other.
     */
    last_quote_decides(before_cursor, "‘", "’", &state->quote_open);
    last_quote_decides(before_cursor, "“", "”", &state->double_quote_open);
}

}  // namespace pathime
