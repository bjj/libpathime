#include "text.h"

#include <cstdint>

namespace demo {
namespace {

/* Length in bytes of the UTF-8 sequence starting at s[i], never past the end.
 * A byte that starts nothing valid answers 1, so a malformed string still
 * advances and no loop here can fail to terminate. */
std::size_t sequence_length(const std::string &s, std::size_t i)
{
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t len = 1;
    if ((c & 0x80u) == 0x00u)      len = 1;
    else if ((c & 0xE0u) == 0xC0u) len = 2;
    else if ((c & 0xF0u) == 0xE0u) len = 3;
    else if ((c & 0xF8u) == 0xF0u) len = 4;
    if (i + len > s.size()) len = 1;
    return len;
}

/* The scalar at s[i], given its sequence length. Malformed input answers
 * U+FFFD, which is width 1 and prints as itself — good enough for a display
 * helper, and it keeps every caller from having to have an error path. */
std::uint32_t scalar_at(const std::string &s, std::size_t i, std::size_t len)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s.data()) + i;
    switch (len) {
    case 1: return p[0] < 0x80u ? p[0] : 0xFFFDu;
    case 2: return ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    case 3: return ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    case 4: return ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) |
                   ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    default: return 0xFFFDu;
    }
}

bool in_range(std::uint32_t c, std::uint32_t lo, std::uint32_t hi)
{
    return c >= lo && c <= hi;
}

/* Columns one scalar occupies. See display_width()'s note on how approximate
 * this is; the ranges are the ones the four engines can actually produce, plus
 * the emoji block because a terminal demo invites one. */
std::size_t scalar_width(std::uint32_t c)
{
    if (c == 0) return 0;
    if (c < 0x20u || (c >= 0x7Fu && c < 0xA0u)) return 0;  /* control */

    if (in_range(c, 0x0300u, 0x036Fu) ||   /* combining diacriticals */
        in_range(c, 0x1AB0u, 0x1AFFu) ||
        in_range(c, 0x20D0u, 0x20FFu) ||
        in_range(c, 0x3099u, 0x309Au) ||   /* combining dakuten/handakuten */
        in_range(c, 0xFE00u, 0xFE0Fu))     /* variation selectors */
        return 0;

    if (in_range(c, 0x1100u, 0x115Fu) ||   /* Hangul jamo, initial */
        in_range(c, 0x2E80u, 0x303Eu) ||   /* CJK radicals … kana punctuation */
        in_range(c, 0x3041u, 0x33FFu) ||   /* kana, bopomofo, CJK compatibility */
        in_range(c, 0x3400u, 0x4DBFu) ||   /* CJK extension A */
        in_range(c, 0x4E00u, 0x9FFFu) ||   /* CJK unified ideographs */
        in_range(c, 0xA000u, 0xA4CFu) ||   /* Yi */
        in_range(c, 0xA960u, 0xA97Fu) ||   /* Hangul jamo extended A */
        in_range(c, 0xAC00u, 0xD7A3u) ||   /* Hangul syllables */
        in_range(c, 0xF900u, 0xFAFFu) ||   /* CJK compatibility ideographs */
        in_range(c, 0xFE10u, 0xFE19u) ||
        in_range(c, 0xFE30u, 0xFE6Fu) ||   /* CJK compatibility forms */
        in_range(c, 0xFF00u, 0xFF60u) ||   /* full-width forms */
        in_range(c, 0xFFE0u, 0xFFE6u) ||
        in_range(c, 0x1F300u, 0x1F64Fu) ||
        in_range(c, 0x1F900u, 0x1F9FFu) ||
        in_range(c, 0x20000u, 0x3FFFDu))   /* CJK extension B and beyond */
        return 2;

    return 1;
}

/* Byte length of the escape sequence starting at s[i], or 0 if none does.
 * Handles the two forms this program emits: CSI (ESC [ … final byte) and the
 * plain two-byte ESC form. */
std::size_t escape_length(const std::string &s, std::size_t i)
{
    if (s[i] != '\x1b' || i + 1 >= s.size()) return 0;
    if (s[i + 1] != '[') return 2;
    std::size_t j = i + 2;
    while (j < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[j]);
        if (c >= 0x40u && c <= 0x7Eu) return j - i + 1;
        j++;
    }
    return s.size() - i;
}

}  // namespace

std::size_t scalar_count(const std::string &s)
{
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); i += sequence_length(s, i)) n++;
    return n;
}

std::size_t byte_offset_of_scalar(const std::string &s, std::size_t n)
{
    std::size_t i = 0;
    for (std::size_t seen = 0; seen < n && i < s.size(); seen++)
        i += sequence_length(s, i);
    return i;
}

std::size_t display_width(const std::string &s)
{
    std::size_t w = 0;
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t len = sequence_length(s, i);
        w += scalar_width(scalar_at(s, i, len));
        i += len;
    }
    return w;
}

std::size_t tail_start_byte(const std::string &s, std::size_t columns)
{
    /* Total width first, then advance the start until what remains fits.
     * Forward is the only direction UTF-8 is cheap to walk, and two forward
     * passes over one line of text is not worth avoiding. */
    std::size_t total = display_width(s);
    std::size_t start = 0;
    while (total > columns && start < s.size()) {
        const std::size_t len = sequence_length(s, start);
        total -= scalar_width(scalar_at(s, start, len));
        start += len;
    }
    return start;
}

std::string clip_to_width(const std::string &styled, std::size_t columns)
{
    std::string out;
    std::size_t used = 0;
    bool clipped = false;
    for (std::size_t i = 0; i < styled.size();) {
        const std::size_t esc = escape_length(styled, i);
        if (esc != 0) {
            out.append(styled, i, esc);
            i += esc;
            continue;
        }
        const std::size_t len = sequence_length(styled, i);
        const std::size_t w = scalar_width(scalar_at(styled, i, len));
        if (used + w > columns) { clipped = true; break; }
        out.append(styled, i, len);
        used += w;
        i += len;
    }
    if (clipped) out += "\x1b[0m";
    return out;
}

std::string pad_to_width(const std::string &s, std::size_t columns)
{
    const std::size_t w = display_width(s);
    if (w >= columns) return s;
    return s + std::string(columns - w, ' ');
}

void erase_scalars(std::string *s, std::size_t start, std::size_t count)
{
    const std::size_t from = byte_offset_of_scalar(*s, start);
    const std::size_t to = byte_offset_of_scalar(*s, start + count);
    s->erase(from, to - from);
}

std::string utf8_encode(std::uint32_t scalar)
{
    std::string out;
    if (scalar < 0x80u) {
        out += static_cast<char>(scalar);
    } else if (scalar < 0x800u) {
        out += static_cast<char>(0xC0u | (scalar >> 6));
        out += static_cast<char>(0x80u | (scalar & 0x3Fu));
    } else if (scalar < 0x10000u) {
        out += static_cast<char>(0xE0u | (scalar >> 12));
        out += static_cast<char>(0x80u | ((scalar >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (scalar & 0x3Fu));
    } else {
        out += static_cast<char>(0xF0u | (scalar >> 18));
        out += static_cast<char>(0x80u | ((scalar >> 12) & 0x3Fu));
        out += static_cast<char>(0x80u | ((scalar >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (scalar & 0x3Fu));
    }
    return out;
}

std::string escape_for_log(const std::string &text)
{
    std::string out;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '\n')      out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20u) out += '?';
        else                out += ch;
    }
    return out;
}

}  // namespace demo
