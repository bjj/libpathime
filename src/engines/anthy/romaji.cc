/*
 * Implementation of the romaji/kana front end declared in romaji.h.
 *
 * ---------------------------------------------------------------------------
 * Where the data came from
 * ---------------------------------------------------------------------------
 *
 * The three tables below are ibus-anthy's, transcribed mechanically from
 * refs/ibus-anthy/engine/python3/tables.py rather than retyped:
 *
 *   kRomajiTable  romaji_typing_rule_static (tables.py:25), all 233 entries.
 *   kSymbolTable  symbol_rule (tables.py:263), minus the entries that cannot
 *                 be reached from here — the digits, which get their own table
 *                 because a different option governs their width; "-", which
 *                 the romaji table already claims for ー and therefore wins;
 *                 and "¥", whose key is not ASCII, the JIS ¥-vs-ろ case
 *                 TODO.md §5 records as unrepresentable.
 *   kScriptTable  hiragana_katakana_table (tables.py:594), the per-scalar
 *                 projection behind PATHIME_OPT_ANTHY_KANA_SCRIPT.
 *
 * The other two of ibus-anthy's five romaji tables are *not* here.
 * romaji_double_consonat_typing_rule (tables.py:314) and
 * romaji_correction_rule (tables.py:337) are eighteen and twenty-one rows
 * spelling out one rule each — "a consonant doubled is っ", "n before
 * anything that cannot continue it is ん" — so they are written as those
 * rules, in resolve() below. ibus-anthy itself abandoned the correction table
 * for a predicate (romaji.py:29, with the table left commented out at its two
 * call sites), which is the same conclusion.
 *
 * Lookups are linear scans. 233 four-byte comparisons per keystroke is
 * nothing next to a kana-kanji conversion, and the alternative — a sorted
 * table plus binary search — buys an invariant that a future editor can break
 * silently. If this ever shows up in a profile it is a std::unordered_map
 * built once, not a hand-maintained sort order.
 */

#include "engines/anthy/romaji.h"

#include <cstring>
#include <string>

#include "keys.h"
#include "utf8.h"

namespace pathime {
namespace {

/* ---------------------------------------------------------------------------
 * The tables
 * ------------------------------------------------------------------------- */

struct RomajiRow {
    const char *romaji;
    const char *kana;
};

/* Sorted by length then alphabetically, which is only for reading: nothing
 * below depends on the order. */
const RomajiRow kRomajiTable[] = {
    {"-", "ー"},
    {"a", "あ"},
    {"e", "え"},
    {"i", "い"},
    {"o", "お"},
    {"u", "う"},
    {"ba", "ば"},
    {"be", "べ"},
    {"bi", "び"},
    {"bo", "ぼ"},
    {"bu", "ぶ"},
    {"da", "だ"},
    {"de", "で"},
    {"di", "ぢ"},
    {"do", "ど"},
    {"du", "づ"},
    {"fa", "ふぁ"},
    {"fe", "ふぇ"},
    {"fi", "ふぃ"},
    {"fo", "ふぉ"},
    {"fu", "ふ"},
    {"ga", "が"},
    {"ge", "げ"},
    {"gi", "ぎ"},
    {"go", "ご"},
    {"gu", "ぐ"},
    {"ha", "は"},
    {"he", "へ"},
    {"hi", "ひ"},
    {"ho", "ほ"},
    {"hu", "ふ"},
    {"ja", "じゃ"},
    {"je", "じぇ"},
    {"ji", "じ"},
    {"jo", "じょ"},
    {"ju", "じゅ"},
    {"ka", "か"},
    {"ke", "け"},
    {"ki", "き"},
    {"ko", "こ"},
    {"ku", "く"},
    {"la", "ぁ"},
    {"le", "ぇ"},
    {"li", "ぃ"},
    {"lo", "ぉ"},
    {"lu", "ぅ"},
    {"ma", "ま"},
    {"me", "め"},
    {"mi", "み"},
    {"mo", "も"},
    {"mu", "む"},
    {"n'", "ん"},
    {"na", "な"},
    {"ne", "ね"},
    {"ni", "に"},
    {"nn", "ん"},
    {"no", "の"},
    {"nu", "ぬ"},
    {"pa", "ぱ"},
    {"pe", "ぺ"},
    {"pi", "ぴ"},
    {"po", "ぽ"},
    {"pu", "ぷ"},
    {"ra", "ら"},
    {"re", "れ"},
    {"ri", "り"},
    {"ro", "ろ"},
    {"ru", "る"},
    {"sa", "さ"},
    {"se", "せ"},
    {"si", "し"},
    {"so", "そ"},
    {"su", "す"},
    {"ta", "た"},
    {"te", "て"},
    {"ti", "ち"},
    {"to", "と"},
    {"tu", "つ"},
    {"va", "ヴぁ"},
    {"ve", "ヴぇ"},
    {"vi", "ヴぃ"},
    {"vo", "ヴぉ"},
    {"vu", "ヴ"},
    {"wa", "わ"},
    {"we", "うぇ"},
    {"wi", "うぃ"},
    {"wo", "を"},
    {"wu", "う"},
    {"xa", "ぁ"},
    {"xe", "ぇ"},
    {"xi", "ぃ"},
    {"xo", "ぉ"},
    {"xu", "ぅ"},
    {"ya", "や"},
    {"ye", "いぇ"},
    {"yi", "い"},
    {"yo", "よ"},
    {"yu", "ゆ"},
    {"za", "ざ"},
    {"ze", "ぜ"},
    {"zi", "じ"},
    {"zo", "ぞ"},
    {"zu", "ず"},
    {"bya", "びゃ"},
    {"bye", "びぇ"},
    {"byi", "びぃ"},
    {"byo", "びょ"},
    {"byu", "びゅ"},
    {"cha", "ちゃ"},
    {"che", "ちぇ"},
    {"chi", "ち"},
    {"cho", "ちょ"},
    {"chu", "ちゅ"},
    {"cya", "ちゃ"},
    {"cye", "ちぇ"},
    {"cyi", "ちぃ"},
    {"cyo", "ちょ"},
    {"cyu", "ちゅ"},
    {"dha", "でゃ"},
    {"dhe", "でぇ"},
    {"dhi", "でぃ"},
    {"dho", "でょ"},
    {"dhu", "でゅ"},
    {"dwu", "どぅ"},
    {"dya", "ぢゃ"},
    {"dye", "ぢぇ"},
    {"dyi", "ぢぃ"},
    {"dyo", "ぢょ"},
    {"dyu", "ぢゅ"},
    {"fya", "ふゃ"},
    {"fye", "ふぇ"},
    {"fyi", "ふぃ"},
    {"fyo", "ふょ"},
    {"fyu", "ふゅ"},
    {"gwa", "ぐぁ"},
    {"gya", "ぎゃ"},
    {"gye", "ぎぇ"},
    {"gyi", "ぎぃ"},
    {"gyo", "ぎょ"},
    {"gyu", "ぎゅ"},
    {"hya", "ひゃ"},
    {"hye", "ひぇ"},
    {"hyi", "ひぃ"},
    {"hyo", "ひょ"},
    {"hyu", "ひゅ"},
    {"jya", "じゃ"},
    {"jye", "じぇ"},
    {"jyi", "じぃ"},
    {"jyo", "じょ"},
    {"jyu", "じゅ"},
    {"kwa", "くぁ"},
    {"kya", "きゃ"},
    {"kye", "きぇ"},
    {"kyi", "きぃ"},
    {"kyo", "きょ"},
    {"kyu", "きゅ"},
    {"lka", "ヵ"},
    {"lke", "ヶ"},
    {"ltu", "っ"},
    {"lwa", "ゎ"},
    {"lya", "ゃ"},
    {"lye", "ぇ"},
    {"lyi", "ぃ"},
    {"lyo", "ょ"},
    {"lyu", "ゅ"},
    {"mya", "みゃ"},
    {"mye", "みぇ"},
    {"myi", "みぃ"},
    {"myo", "みょ"},
    {"myu", "みゅ"},
    {"nya", "にゃ"},
    {"nye", "にぇ"},
    {"nyi", "にぃ"},
    {"nyo", "にょ"},
    {"nyu", "にゅ"},
    {"pya", "ぴゃ"},
    {"pye", "ぴぇ"},
    {"pyi", "ぴぃ"},
    {"pyo", "ぴょ"},
    {"pyu", "ぴゅ"},
    {"rya", "りゃ"},
    {"rye", "りぇ"},
    {"ryi", "りぃ"},
    {"ryo", "りょ"},
    {"ryu", "りゅ"},
    {"sha", "しゃ"},
    {"she", "しぇ"},
    {"shi", "し"},
    {"sho", "しょ"},
    {"shu", "しゅ"},
    {"sya", "しゃ"},
    {"sye", "しぇ"},
    {"syi", "しぃ"},
    {"syo", "しょ"},
    {"syu", "しゅ"},
    {"tha", "てゃ"},
    {"the", "てぇ"},
    {"thi", "てぃ"},
    {"tho", "てょ"},
    {"thu", "てゅ"},
    {"tsa", "つぁ"},
    {"tse", "つぇ"},
    {"tsi", "つぃ"},
    {"tso", "つぉ"},
    {"tsu", "つ"},
    {"twu", "とぅ"},
    {"tya", "ちゃ"},
    {"tye", "ちぇ"},
    {"tyi", "ちぃ"},
    {"tyo", "ちょ"},
    {"tyu", "ちゅ"},
    {"wha", "うぁ"},
    {"whe", "うぇ"},
    {"whi", "うぃ"},
    {"who", "うぉ"},
    {"wye", "ゑ"},
    {"wyi", "ゐ"},
    {"xka", "ヵ"},
    {"xke", "ヶ"},
    {"xtu", "っ"},
    {"xwa", "ゎ"},
    {"xya", "ゃ"},
    {"xye", "ぇ"},
    {"xyi", "ぃ"},
    {"xyo", "ょ"},
    {"xyu", "ゅ"},
    {"zya", "じゃ"},
    {"zye", "じぇ"},
    {"zyi", "じぃ"},
    {"zyo", "じょ"},
    {"zyu", "じゅ"},
    {"ltsu", "っ"},
    {"xtsu", "っ"},
};

/** Punctuation keys, when PATHIME_OPT_PUNCTUATION_WIDTH is full width. */
struct SymbolRow {
    char        key;
    const char *full;
};

const SymbolRow kSymbolTable[] = {
    {' ', "　"},
    {'!', "！"},
    {'"', "”"},
    {'#', "＃"},
    {'$', "＄"},
    {'%', "％"},
    {'&', "＆"},
    {'\'', "’"},
    {'(', "（"},
    {')', "）"},
    {'*', "＊"},
    {'+', "＋"},
    {',', "、"},
    {'.', "。"},
    {'/', "／"},
    {':', "："},
    {';', "；"},
    {'<', "＜"},
    {'=', "＝"},
    {'>', "＞"},
    {'?', "？"},
    {'@', "＠"},
    {'[', "「"},
    {'\\', "＼"},
    {']', "」"},
    {'^', "＾"},
    {'_', "＿"},
    {'`', "‘"},
    {'{', "｛"},
    {'|', "｜"},
    {'}', "｝"},
    {'~', "～"},
};

/** Digits, when PATHIME_OPT_LATIN_WIDTH is full width. Indexed by value. */
const char *const kFullwidthDigits[10] = {
    "０", "１", "２", "３", "４", "５", "６", "７", "８", "９"
};

/** One hiragana scalar and its katakana and halfwidth-katakana forms. */
struct ScriptRow {
    const char *hiragana;
    const char *katakana;
    const char *halfwidth;
};

const ScriptRow kScriptTable[] = {
    {"あ", "ア", "ｱ"},
    {"い", "イ", "ｲ"},
    {"う", "ウ", "ｳ"},
    {"え", "エ", "ｴ"},
    {"お", "オ", "ｵ"},
    {"か", "カ", "ｶ"},
    {"き", "キ", "ｷ"},
    {"く", "ク", "ｸ"},
    {"け", "ケ", "ｹ"},
    {"こ", "コ", "ｺ"},
    {"が", "ガ", "ｶﾞ"},
    {"ぎ", "ギ", "ｷﾞ"},
    {"ぐ", "グ", "ｸﾞ"},
    {"げ", "ゲ", "ｹﾞ"},
    {"ご", "ゴ", "ｺﾞ"},
    {"さ", "サ", "ｻ"},
    {"し", "シ", "ｼ"},
    {"す", "ス", "ｽ"},
    {"せ", "セ", "ｾ"},
    {"そ", "ソ", "ｿ"},
    {"ざ", "ザ", "ｻﾞ"},
    {"じ", "ジ", "ｼﾞ"},
    {"ず", "ズ", "ｽﾞ"},
    {"ぜ", "ゼ", "ｾﾞ"},
    {"ぞ", "ゾ", "ｿﾞ"},
    {"た", "タ", "ﾀ"},
    {"ち", "チ", "ﾁ"},
    {"つ", "ツ", "ﾂ"},
    {"て", "テ", "ﾃ"},
    {"と", "ト", "ﾄ"},
    {"だ", "ダ", "ﾀﾞ"},
    {"ぢ", "ヂ", "ﾁﾞ"},
    {"づ", "ヅ", "ﾂﾞ"},
    {"で", "デ", "ﾃﾞ"},
    {"ど", "ド", "ﾄﾞ"},
    {"な", "ナ", "ﾅ"},
    {"に", "ニ", "ﾆ"},
    {"ぬ", "ヌ", "ﾇ"},
    {"ね", "ネ", "ﾈ"},
    {"の", "ノ", "ﾉ"},
    {"は", "ハ", "ﾊ"},
    {"ひ", "ヒ", "ﾋ"},
    {"ふ", "フ", "ﾌ"},
    {"へ", "ヘ", "ﾍ"},
    {"ほ", "ホ", "ﾎ"},
    {"ば", "バ", "ﾊﾞ"},
    {"び", "ビ", "ﾋﾞ"},
    {"ぶ", "ブ", "ﾌﾞ"},
    {"べ", "ベ", "ﾍﾞ"},
    {"ぼ", "ボ", "ﾎﾞ"},
    {"ぱ", "パ", "ﾊﾟ"},
    {"ぴ", "ピ", "ﾋﾟ"},
    {"ぷ", "プ", "ﾌﾟ"},
    {"ぺ", "ペ", "ﾍﾟ"},
    {"ぽ", "ポ", "ﾎﾟ"},
    {"ま", "マ", "ﾏ"},
    {"み", "ミ", "ﾐ"},
    {"む", "ム", "ﾑ"},
    {"め", "メ", "ﾒ"},
    {"も", "モ", "ﾓ"},
    {"や", "ヤ", "ﾔ"},
    {"ゆ", "ユ", "ﾕ"},
    {"よ", "ヨ", "ﾖ"},
    {"ら", "ラ", "ﾗ"},
    {"り", "リ", "ﾘ"},
    {"る", "ル", "ﾙ"},
    {"れ", "レ", "ﾚ"},
    {"ろ", "ロ", "ﾛ"},
    {"わ", "ワ", "ﾜ"},
    {"を", "ヲ", "ｦ"},
    {"ん", "ン", "ﾝ"},
    {"ぁ", "ァ", "ｧ"},
    {"ぃ", "ィ", "ｨ"},
    {"ぅ", "ゥ", "ｩ"},
    {"ぇ", "ェ", "ｪ"},
    {"ぉ", "ォ", "ｫ"},
    {"っ", "ッ", "ｯ"},
    {"ゃ", "ャ", "ｬ"},
    {"ゅ", "ュ", "ｭ"},
    {"ょ", "ョ", "ｮ"},
    {"ヵ", "ヵ", "ｶ"},
    {"ヶ", "ヶ", "ｹ"},
    {"ゎ", "ヮ", "ﾜ"},
    {"ゐ", "ヰ", "ｨ"},
    {"ゑ", "ヱ", "ｪ"},
    {"ヴ", "ヴ", "ｳﾞ"},
    {"ー", "ー", "ｰ"},
    {"、", "、", "､"},
    {"。", "。", "｡"},
    {"！", "！", "!"},
    {"”", "”", "\""},
    {"＃", "＃", "#"},
    {"＄", "＄", "$"},
    {"％", "％", "%"},
    {"＆", "＆", "&"},
    {"’", "’", "'"},
    {"（", "（", "("},
    {"）", "）", ")"},
    {"～", "～", "~"},
    {"＝", "＝", "="},
    {"＾", "＾", "^"},
    {"＼", "＼", "\\"},
    {"｜", "｜", "|"},
    {"‘", "‘", "`"},
    {"＠", "＠", "@"},
    {"゛", "゛", "ﾞ"},
    {"｛", "｛", "{"},
    {"゜", "゜", "ﾟ"},
    {"「", "「", "｢"},
    {"＋", "＋", "+"},
    {"；", "；", ";"},
    {"＊", "＊", "*"},
    {"：", "：", ":"},
    {"｝", "｝", "}"},
    {"」", "」", "｣"},
    {"＜", "＜", "<"},
    {"＞", "＞", ">"},
    {"？", "？", "?"},
    {"・", "・", "･"},
    {"／", "／", "/"},
    {"＿", "＿", "_"},
    {"￥", "￥", "¥"},
    {"０", "０", "0"},
    {"１", "１", "1"},
    {"２", "２", "2"},
    {"３", "３", "3"},
    {"４", "４", "4"},
    {"５", "５", "5"},
    {"６", "６", "6"},
    {"７", "７", "7"},
    {"８", "８", "8"},
    {"９", "９", "9"},

};

/* ---------------------------------------------------------------------------
 * Lookups
 * ------------------------------------------------------------------------- */

const char *romaji_lookup(const std::string &romaji)
{
    for (const RomajiRow &row : kRomajiTable) {
        if (romaji == row.romaji) return row.kana;
    }
    return nullptr;
}

/**
 * True if some table entry *starts* with @a romaji and is longer than it —
 * that is, if another keystroke could still resolve what is pending. This is
 * the test that makes "k" wait rather than being emitted as a stray letter,
 * and it is what ibus-anthy achieves by keeping unresolved characters in a
 * segment's enchars until something matches.
 */
bool romaji_continues(const std::string &romaji)
{
    const size_t len = romaji.size();
    for (const RomajiRow &row : kRomajiTable) {
        if (std::strlen(row.romaji) > len &&
            std::strncmp(row.romaji, romaji.c_str(), len) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * The kana a lone non-alphabetic key produces, or nullptr to insert the ASCII
 * character as itself. The two width options decide, one for digits and one
 * for everything else, which is the split the header's defaults require:
 * half-width digits with full-width punctuation.
 */
const char *symbol_lookup(char key, const RomajiSettings &settings)
{
    if (key >= '0' && key <= '9') {
        return settings.latin_width == PATHIME_WIDTH_FULL
                   ? kFullwidthDigits[key - '0']
                   : nullptr;
    }
    if (settings.punctuation_width != PATHIME_WIDTH_FULL) return nullptr;
    for (const SymbolRow &row : kSymbolTable) {
        if (row.key == key) return row.full;
    }
    return nullptr;
}

/**
 * The consonants that geminate — a doubled one is っ plus the consonant again.
 * Exactly ibus-anthy's romaji_double_consonat_typing_rule key set: "n" is
 * absent because "nn" is already ん, and "l" and "q" are absent because
 * ibus-anthy's table omits them.
 */
bool geminates(char c)
{
    return c != '\0' && std::strchr("bcdfghjkmprstvwxyz", c) != nullptr;
}

/**
 * True if @a next can still continue a pending "n" — the vowels, "y", a second
 * "n", and the apostrophe that spells ん outright. Anything else means the "n"
 * was ん all along. ibus-anthy's romaji_correction_rule_get, romaji.py:29.
 */
bool continues_n(char next)
{
    return next != '\0' && std::strchr("aiueony'", next) != nullptr;
}

char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool ascii_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* ---------------------------------------------------------------------------
 * Projections
 * ------------------------------------------------------------------------- */

/**
 * The length of the UTF-8 sequence beginning with @a lead.
 *
 * Every string this file walks was assembled from its own tables, so it is
 * known-valid and this does not diagnose; utf8.h's validating entry points are
 * for text arriving from a client, which is a different problem. A lead byte
 * that is somehow malformed answers 1, so the walk always terminates.
 */
size_t sequence_length(unsigned char lead)
{
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

/** Hiragana to the configured script, one scalar at a time. */
std::string project_script(const std::string &text, pathime_anthy_script_t script)
{
    if (script == PATHIME_ANTHY_SCRIPT_HIRAGANA) return text;

    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const size_t len = sequence_length(static_cast<unsigned char>(text[i]));
        const char *mapped = nullptr;
        for (const ScriptRow &row : kScriptTable) {
            if (text.compare(i, len, row.hiragana) == 0) {
                mapped = (script == PATHIME_ANTHY_SCRIPT_KATAKANA) ? row.katakana
                                                                   : row.halfwidth;
                break;
            }
        }
        if (mapped) {
            out += mapped;
        } else {
            out.append(text, i, len);
        }
        i += len;
    }
    return out;
}

/**
 * PATHIME_OPT_ANTHY_PERIOD_STYLE and PATHIME_OPT_ANTHY_SYMBOL_STYLE, which are
 * both per-scalar substitutions over already-composed text — ibus-anthy's
 * JaString._chk_text (jastring.py:243).
 *
 * Applied after the script projection, in that order, because that is the
 * order ibus-anthy applies them and the period table has entries for both the
 * full-width and the halfwidth-katakana forms (。and ｡) so that the pair
 * composes: 。in halfwidth katakana becomes ｡ and then ".".
 *
 * Kept out of the stored kana rather than applied when a key is struck, so
 * that changing either option re-projects what is already typed. The header's
 * rule is that an option change takes effect immediately, and this is what
 * that costs here: two comparisons per scalar on the way out.
 */
std::string project_styles(const std::string &text, const RomajiSettings &settings)
{
    const bool fullwidth_period = settings.period == PATHIME_ANTHY_PERIOD_FULLWIDTH;
    const bool corner_brackets = settings.symbol == PATHIME_ANTHY_SYMBOL_CORNER_SLASH ||
                                 settings.symbol == PATHIME_ANTHY_SYMBOL_CORNER_MIDDOT;
    const bool solidus = settings.symbol == PATHIME_ANTHY_SYMBOL_CORNER_SLASH ||
                         settings.symbol == PATHIME_ANTHY_SYMBOL_BRACKET_SLASH;
    if (!fullwidth_period && corner_brackets && solidus) return text;

    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const size_t len = sequence_length(static_cast<unsigned char>(text[i]));
        const char *mapped = nullptr;
        if (fullwidth_period) {
            if (text.compare(i, len, "。") == 0)      mapped = "．";
            else if (text.compare(i, len, "、") == 0) mapped = "，";
            else if (text.compare(i, len, "｡") == 0)  mapped = ".";
            else if (text.compare(i, len, "､") == 0)  mapped = ",";
        }
        if (!mapped && !corner_brackets) {
            if (text.compare(i, len, "「") == 0)      mapped = "［";
            else if (text.compare(i, len, "」") == 0) mapped = "］";
        }
        if (!mapped && !solidus && text.compare(i, len, "／") == 0) {
            mapped = "・";
        }
        if (mapped) {
            out += mapped;
        } else {
            out.append(text, i, len);
        }
        i += len;
    }
    return out;
}

}  // namespace

/* ---------------------------------------------------------------------------
 * Settings
 * ------------------------------------------------------------------------- */

RomajiSettings romaji_settings(const OptionReader &options)
{
    RomajiSettings settings;
    settings.method = static_cast<pathime_anthy_typing_t>(
        options.number(PATHIME_OPT_ANTHY_TYPING_METHOD));
    settings.script = static_cast<pathime_anthy_script_t>(
        options.number(PATHIME_OPT_ANTHY_KANA_SCRIPT));
    settings.period = static_cast<pathime_anthy_period_t>(
        options.number(PATHIME_OPT_ANTHY_PERIOD_STYLE));
    settings.symbol = static_cast<pathime_anthy_symbol_t>(
        options.number(PATHIME_OPT_ANTHY_SYMBOL_STYLE));
    settings.latin_with_shift = options.flag(PATHIME_OPT_ANTHY_LATIN_WITH_SHIFT);
    settings.latin_width = static_cast<pathime_width_t>(
        options.number(PATHIME_OPT_LATIN_WIDTH));
    settings.punctuation_width = static_cast<pathime_width_t>(
        options.number(PATHIME_OPT_PUNCTUATION_WIDTH));
    return settings;
}

/* ---------------------------------------------------------------------------
 * The state machine
 * ------------------------------------------------------------------------- */

/*
 * One character in, resolved as far as it can be.
 *
 * The loop exists because resolving can leave a remainder that must itself be
 * resolved: "n" followed by "k" is ん with a "k" that is now pending on its own
 * account, and a dead end has to be able to drop its leading character and try
 * again. Every branch either returns or shortens `candidate`, so it terminates.
 */
void RomajiComposer::step(char c, const RomajiSettings &settings)
{
    std::string candidate = pending_ + c;

    for (;;) {
        /* 1. A complete romaji sequence. */
        if (const char *kana = romaji_lookup(candidate)) {
            kana_ += kana;
            pending_.clear();
            return;
        }

        /* 2. A doubled consonant: っ, and the consonant starts over. This is
         *    the case that makes the front end a state machine — "kk" is two
         *    outputs from one keystroke, one of them still pending. */
        if (candidate.size() == 2 && candidate[0] == candidate[1] &&
            geminates(candidate[0])) {
            kana_ += "っ";
            pending_.assign(1, candidate[1]);
            return;
        }

        /* 3. A pending "n" that nothing can continue is ん, and the character
         *    that ended it is resolved on its own. */
        if (candidate.size() == 2 && candidate[0] == 'n' && !continues_n(candidate[1])) {
            kana_ += "ん";
            candidate.erase(0, 1);
            continue;
        }

        /* 4. Not resolvable yet, but another keystroke could. */
        if (romaji_continues(candidate)) {
            pending_ = candidate;
            return;
        }

        /* 5. A single character with no romaji future: a symbol, a digit, or
         *    a letter that spells nothing. */
        if (candidate.size() == 1) {
            if (const char *symbol = symbol_lookup(candidate[0], settings)) {
                kana_ += symbol;
            } else {
                kana_ += candidate[0];
            }
            pending_.clear();
            return;
        }

        /* 6. A dead end: the leading character can never be part of anything.
         *    Emit it as itself and re-resolve the rest.
         *
         *    ibus-anthy does something subtly different here — it keeps the
         *    unresolvable prefix pending and emits the *suffix* it managed to
         *    match, so "k," shows 、before the k (romaji.py:145-174). That is
         *    an artifact of its segment list, which can hold an unresolved
         *    segment ahead of a resolved one. A single kana run cannot, and
         *    reproducing the reordering would be worse than the honest reading:
         *    what was typed first appears first. */
        kana_ += candidate[0];
        candidate.erase(0, 1);
    }
}

bool RomajiComposer::insert(const KeyEvent &key, const RomajiSettings &settings)
{
    if (settings.method == PATHIME_ANTHY_TYPING_KANA) {
        /* TODO(impl): kana entry — striking kana directly rather than spelling
         * them, ibus-anthy's kana.KanaSegment (kana.py) with the JIS kana
         * layout and its dakuten/handakuten combining over the preceding kana.
         * It is a separate state machine over a separate table, and it needs
         * layout_key rather than keysym since a kana keyboard's legends are
         * exactly what a client's layout will not report.
         *
         * Declining every key is deliberate and is the whole behaviour of this
         * branch: PATHIME_ANTHY_TYPING_KANA composes nothing at all until that
         * is written. Falling through to the romaji machine would be worse —
         * the user would get kana, just not the ones the keycaps promised, and
         * the option would look implemented. */
        return false;
    }

    const char c = keysym_to_ascii(key.keysym);
    if (c == 0) return false;

    /* PATHIME_OPT_ANTHY_LATIN_WITH_SHIFT: a shifted letter is a letter.
     *
     * Per keystroke, where ibus-anthy latches the mode for the rest of the
     * segment being built (romaji.py:109-119) — a determinate rule in place of
     * one whose extent depends on where a segment boundary happens to fall.
     * The case is preserved as the layout produced it, which is the point of
     * the feature; CapsLock alone is not Shift and does not reach here, so
     * "SA" still composes さ, as ibus-anthy's comment at romaji.py:48 insists
     * it must. */
    if (settings.latin_with_shift && key.has(PATHIME_MOD_SHIFT) && ascii_alpha(c)) {
        kana_ += pending_;
        pending_.clear();
        kana_ += c;
        return true;
    }

    step(ascii_lower(c), settings);
    return true;
}

bool RomajiComposer::backspace()
{
    if (!pending_.empty()) {
        pending_.erase(pending_.size() - 1);
        return true;
    }
    if (kana_.empty()) return false;

    /* One kana, not one byte: positions in this library are scalar values
     * (backend.h rule 2) and utf8.h owns the conversion. */
    const size_t scalars = utf8_scalar_count(kana_.data(), kana_.size());
    const size_t cut = utf8_byte_offset(kana_.data(), kana_.size(), scalars - 1);
    if (cut == kUtf8NoPosition) {
        kana_.clear();
    } else {
        kana_.erase(cut);
    }
    return true;
}

std::string RomajiComposer::display(const RomajiSettings &settings) const
{
    /* The pending Latin is appended unprojected: it is not kana yet, and
     * showing it as the characters the user actually struck is what tells them
     * the engine is waiting for one more key. */
    return project_styles(project_script(kana_, settings.script), settings) + pending_;
}

std::string RomajiComposer::finished_kana() const
{
    /* Only a bare "n" is finished. Any longer pending run is passed through as
     * the Latin it is: "ky" could still have become きゃ or きゅ and guessing
     * which would be inventing input. */
    return pending_ == "n" ? kana_ + "ん" : kana_ + pending_;
}

std::string RomajiComposer::reading(const RomajiSettings &settings) const
{
    /* No script projection: anthy is fed hiragana whatever the display script,
     * exactly as ibus-anthy does (engine.py:1114). */
    return project_styles(finished_kana(), settings);
}

std::string RomajiComposer::commit_text(const RomajiSettings &settings) const
{
    return project_styles(project_script(finished_kana(), settings.script), settings);
}

}  // namespace pathime
