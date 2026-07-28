/*
 * Implementation of the encoding-boundary helpers declared in utf8.h.
 *
 * Everything here is hand-rolled rather than delegated, and the three obvious
 * candidates were all rejected for the same reason — they answer a different
 * question. <codecvt> is deprecated and its error reporting cannot distinguish
 * an overlong form from a truncated one; iconv is a build dependency the
 * Windows port would have to carry for a hundred lines of table-free
 * arithmetic; and the C library's mbrtoc32 is locale-dependent, which is
 * exactly what an API that is UTF-8 on every platform must not be.
 *
 * The decoder is deliberately one loop shared by both validators. The
 * strictness rules are the interesting part, and having them in one place is
 * what stops the boundaries disagreeing: before this file was filled in there
 * were two near-identical scanners, one in context.cc and one in options.cc.
 */

#include "utf8.h"

namespace pathime {

namespace {

/*
 * The lowest scalar value each sequence length may legally encode, indexed by
 * that length, so entries 0 and 1 are unused padding. A decoded value below
 * its entry is an overlong form — the classic way to smuggle a NUL or an ASCII
 * delimiter past a naive check — and is rejected rather than normalized.
 */
constexpr uint32_t kLowestLegal[5] = {0u, 0u, 0x80u, 0x800u, 0x10000u};

constexpr uint32_t kMaxScalar = 0x10FFFFu;
constexpr uint32_t kSurrogateFirst = 0xD800u;
constexpr uint32_t kSurrogateLast = 0xDFFFu;

/** True if @a value is a Unicode scalar value this API can carry. */
bool is_scalar_value(uint32_t value)
{
    return value != 0u && value <= kMaxScalar &&
           !(value >= kSurrogateFirst && value <= kSurrogateLast);
}

/**
 * Decode one sequence starting at @a bytes[i], with @a len bounding the
 * buffer. On success advances @a i past the sequence and writes the scalar.
 */
bool decode_one(const char *bytes, size_t len, size_t &i, uint32_t &out_scalar)
{
    const unsigned char lead = static_cast<unsigned char>(bytes[i]);
    size_t seq_len;
    uint32_t scalar;

    if (lead == 0x00u) {
        /* U+0000 is not representable in this API, so a NUL byte inside a
         * slice is malformed input rather than an early terminator. */
        return false;
    } else if (lead < 0x80u) {
        seq_len = 1;
        scalar = lead;
    } else if ((lead & 0xE0u) == 0xC0u) {
        seq_len = 2;
        scalar = lead & 0x1Fu;
    } else if ((lead & 0xF0u) == 0xE0u) {
        seq_len = 3;
        scalar = lead & 0x0Fu;
    } else if ((lead & 0xF8u) == 0xF0u) {
        seq_len = 4;
        scalar = lead & 0x07u;
    } else {
        /* A stray continuation byte, or a five-byte lead from the obsolete
         * pre-2003 encoding. Neither can begin a sequence. */
        return false;
    }

    if (len - i < seq_len) {
        return false;  /* Truncated: the buffer ends mid-sequence. */
    }

    for (size_t k = 1; k < seq_len; ++k) {
        const unsigned char cont = static_cast<unsigned char>(bytes[i + k]);
        if ((cont & 0xC0u) != 0x80u) {
            return false;
        }
        scalar = (scalar << 6) | (cont & 0x3Fu);
    }

    if (scalar < kLowestLegal[seq_len] || !is_scalar_value(scalar)) {
        return false;
    }

    i += seq_len;
    out_scalar = scalar;
    return true;
}

}  // namespace

bool utf8_validate(const char *bytes, size_t len, size_t *out_scalars)
{
    if (bytes == nullptr) {
        /*
         * A null pointer describes no text at all, which is coherent only when
         * the length agrees. pathime_str_t promises bytes points at "" rather
         * than nullptr for an empty slice, but a caller that has not read that
         * far gets a determinate answer here instead of a fault.
         */
        if (len != 0) {
            return false;
        }
        if (out_scalars != nullptr) {
            *out_scalars = 0;
        }
        return true;
    }

    size_t scalars = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t scalar;
        if (!decode_one(bytes, len, i, scalar)) {
            return false;
        }
        ++scalars;
    }

    if (out_scalars != nullptr) {
        *out_scalars = scalars;
    }
    return true;
}

bool utf8_validate_z(const char *text, size_t *out_scalars)
{
    if (text == nullptr) {
        return false;
    }

    /*
     * Measured first rather than scanned in place. decode_one() needs a bound
     * to detect truncation, and the terminator is that bound: a NUL inside a
     * multi-byte sequence then reads as the buffer ending mid-sequence, which
     * is the right diagnosis and never a read past the terminator.
     */
    size_t len = 0;
    while (text[len] != '\0') {
        ++len;
    }
    return utf8_validate(text, len, out_scalars);
}

size_t utf8_scalar_count(const char *bytes, size_t len)
{
    /* Continuation bytes are the only ones that do not begin a scalar, so
     * counting the rest needs no decoding. Valid input is a precondition. */
    size_t scalars = 0;
    for (size_t i = 0; i < len; ++i) {
        if ((static_cast<unsigned char>(bytes[i]) & 0xC0u) != 0x80u) {
            ++scalars;
        }
    }
    return scalars;
}

size_t utf8_byte_offset(const char *bytes, size_t len, size_t scalar_index)
{
    size_t seen = 0;
    for (size_t i = 0; i < len; ++i) {
        if ((static_cast<unsigned char>(bytes[i]) & 0xC0u) != 0x80u) {
            if (seen == scalar_index) {
                return i;
            }
            ++seen;
        }
    }

    /* One past the last scalar is a legal position — a cursor sitting after
     * the final character — and answers the end of the buffer. */
    return (seen == scalar_index) ? len : kUtf8NoPosition;
}

size_t utf8_scalar_index(const char *bytes, size_t len, size_t byte_offset)
{
    if (byte_offset > len) {
        return kUtf8NoPosition;
    }
    if (byte_offset < len &&
        (static_cast<unsigned char>(bytes[byte_offset]) & 0xC0u) == 0x80u) {
        /* Inside a multi-byte sequence: there is no scalar position here, and
         * rounding to a neighbour would silently move a cursor. */
        return kUtf8NoPosition;
    }
    return utf8_scalar_count(bytes, byte_offset);
}

bool utf8_next_scalar(const char *bytes, size_t len, size_t *offset, uint32_t *out_scalar)
{
    size_t i = *offset;
    if (bytes == nullptr || i >= len) {
        return false;
    }

    uint32_t scalar = 0;
    if (!decode_one(bytes, len, i, scalar)) {
        return false;
    }

    *offset = i;
    *out_scalar = scalar;
    return true;
}

bool utf8_append_scalar(std::string &out, uint32_t scalar)
{
    if (!is_scalar_value(scalar)) {
        return false;
    }

    if (scalar < 0x80u) {
        out.push_back(static_cast<char>(scalar));
    } else if (scalar < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (scalar >> 6)));
        out.push_back(static_cast<char>(0x80u | (scalar & 0x3Fu)));
    } else if (scalar < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (scalar >> 12)));
        out.push_back(static_cast<char>(0x80u | ((scalar >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (scalar & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (scalar >> 18)));
        out.push_back(static_cast<char>(0x80u | ((scalar >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((scalar >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (scalar & 0x3Fu)));
    }
    return true;
}

bool utf8_from_ucs4(const uint32_t *units, size_t len, std::string *out)
{
    out->clear();
    if (units == nullptr) {
        return len == 0;
    }

    for (size_t i = 0; i < len; ++i) {
        if (!utf8_append_scalar(*out, units[i])) {
            /* All or nothing: a partially converted string handed onward would
             * be indistinguishable from a legitimately shorter one. */
            out->clear();
            return false;
        }
    }
    return true;
}

bool utf8_from_ucs4_z(const uint32_t *units, std::string *out)
{
    out->clear();
    if (units == nullptr) {
        /* libhangul returns an empty preedit for a context with nothing in it,
         * and a null pointer is a legitimate spelling of that. */
        return true;
    }

    for (size_t i = 0; units[i] != 0u; ++i) {
        if (!utf8_append_scalar(*out, units[i])) {
            out->clear();
            return false;
        }
    }
    return true;
}

}  // namespace pathime
