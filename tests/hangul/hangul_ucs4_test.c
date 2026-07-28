/*
 * UCS-4 handling in libhangul's pure (context-free) API.
 *
 * The point of this file on Windows is the width of a character. libhangul
 * speaks `ucschar` = uint32_t everywhere: preedit and commit buffers, the
 * syllable iterator, the jamo <-> syllable conversions. Windows' wchar_t is 16
 * bits, so any code that treats a `ucschar*` as a wide string -- upstream's own
 * test.c does, with wcscmp() -- reads a different thing on the two platforms.
 * Nothing here touches wchar_t; every comparison is element-wise over
 * uint32_t, so a divergence between Linux and Windows means a real difference
 * in the library, not in the test's idea of a character.
 *
 * The syllable-iterator and jamo_to_cjamo cases are transcribed from
 * engines/libhangul/test/test.c, which cannot run here (no Check on Windows).
 * The rest is ours.
 *
 * Kept pure ASCII deliberately -- see the note in hangul_ic_test.c.
 */
#include <stdlib.h>

#include "hangul_test_util.h"

#define COUNTOF(x) (sizeof(x) / sizeof((x)[0]))

/* The modern Hangul syllable block, U+AC00..U+D7A3. */
#define SYLLABLE_FIRST 0xAC00u
#define SYLLABLE_LAST  0xD7A3u

static void test_ucschar_width(void)
{
    /* Belt and braces: hangul_test_util.h already asserts this at compile
     * time, but a runtime check catches a header/library mismatch too -- an
     * installed hangul.h that disagrees with the DLL it is paired with. */
    HT_CHECK(sizeof(ucschar) == 4);

    /* A code point above 0xFFFF must survive as one element. No Hangul lives
     * up there, but the type has to be able to carry it, and this is exactly
     * what a 16-bit wchar_t cannot do. */
    {
        ucschar buf[2];
        buf[0] = 0x1D11Eu; /* U+1D11E MUSICAL SYMBOL G CLEF */
        buf[1] = 0;
        HT_CHECK(ucs4_len(buf) == 1);
        HT_CHECK(buf[0] == 0x1D11Eu);
    }
}

static void test_jamo_syllable_roundtrip(void)
{
    ucschar syllable;
    int mismatches = 0;

    /* Every syllable in the block must decompose and recompose to itself.
     * Counted as one check so a systematic breakage does not print 11172
     * lines. */
    ucschar first_bad = 0;

    for (syllable = SYLLABLE_FIRST; syllable <= SYLLABLE_LAST; syllable++) {
        ucschar cho = 0, jung = 0, jong = 0;
        hangul_syllable_to_jamo(syllable, &cho, &jung, &jong);
        if (hangul_jamo_to_syllable(cho, jung, jong) != syllable) {
            if (mismatches == 0)
                first_bad = syllable;
            mismatches++;
        }
    }
    ht_checks++;
    if (mismatches != 0)
        HT_FAILF("jamo round trip failed for %d syllables, first at U+%04X",
                 mismatches, (unsigned)first_bad);

    /* Spot values, so a round trip that is self-consistently wrong still
     * shows up. GAN = choseong G + jungseong A + jongseong N. */
    HT_CHECK(hangul_jamo_to_syllable(0x1100, 0x1161, 0x11AB) == 0xAC04);
    HT_CHECK(hangul_jamo_to_syllable(0x1100, 0x1161, 0) == 0xAC00);
    /* A syllable cannot be formed without a jungseong. */
    HT_CHECK(hangul_jamo_to_syllable(0x1100, 0, 0) == 0);
}

static void test_jamos_to_syllables(void)
{
    /* L V T  L V -> two precomposed syllables. This is the conversion an
     * engine performs on the way to the client, so the destination really is
     * a caller-provided ucschar array: a good place for a width mistake to
     * show up as a buffer overrun rather than a wrong answer. */
    static const ucschar jamos[] = {
        0x1100, 0x1161, 0x11AB, /* GAN */
        0x1103, 0x1161          /* DA */
    };
    ucschar out[8];
    int n;

    memset(out, 0xFF, sizeof(out));
    n = hangul_jamos_to_syllables(out, (int)COUNTOF(out),
                                  jamos, (int)COUNTOF(jamos));
    HT_CHECK(n == 2);
    if (n >= 0 && n < (int)COUNTOF(out)) {
        out[n] = 0;
        HT_CHECK_UCS("jamos_to_syllables", out, UCS(0xAC04, 0xB2E4));
    }
}

static void test_classification(void)
{
    HT_CHECK(hangul_is_choseong(0x1100));
    HT_CHECK(!hangul_is_choseong(0x1161));
    HT_CHECK(hangul_is_jungseong(0x1161));
    HT_CHECK(hangul_is_jongseong(0x11AB));
    HT_CHECK(hangul_is_syllable(0xAC00));
    HT_CHECK(hangul_is_syllable(SYLLABLE_LAST));
    HT_CHECK(!hangul_is_syllable(SYLLABLE_LAST + 1));
    HT_CHECK(hangul_is_jamo(0x1100));
    HT_CHECK(hangul_is_cjamo(0x3131));
    HT_CHECK(!hangul_is_cjamo(0x1100));
    HT_CHECK(!hangul_is_syllable(0x1D11Eu)); /* non-BMP must not be mistaken */
}

/* Transcribed from engines/libhangul/test/test.c: test_hangul_jamo_to_cjamo. */
static void test_jamo_to_cjamo(void)
{
    HT_CHECK(hangul_jamo_to_cjamo(0x11F2) == 0x3183);
    HT_CHECK(hangul_jamo_to_cjamo(0xA971) == 0x316F);
    HT_CHECK(hangul_jamo_to_cjamo(0xD7F9) == 0x3149);
}

/* Transcribed from engines/libhangul/test/test.c: test_syllable_iterator.
 * The iterator walks a raw ucschar array by pointer arithmetic, so every
 * expectation below is an element offset -- the one thing that changes if the
 * element width is ever wrong. */
static void test_syllable_iterator(void)
{
    static const ucschar str[] = {
        /* L L V V T T */
        0x1107, 0x1107, 0x116E, 0x1166, 0x11AF, 0x11A8,         /*  0 */
        /* L V T */
        0x1108, 0x1170, 0x11B0,                                 /*  6 */
        /* L L V V T T M */
        0x1107, 0x1107, 0x116E, 0x1166, 0x11AF, 0x11A8, 0x302E, /*  9 */
        /* L V T M */
        0x1108, 0x1170, 0x11B0, 0x302F,                         /* 16 */
        /* Lf V */
        0x115F, 0x1161,                                         /* 20 */
        /* L Vf */
        0x110C, 0x1160,                                         /* 22 */
        /* L LVT T */
        0x1107, 0xBC14, 0x11A8,                                 /* 24 */
        /* L LV T */
        0x1100, 0xAC00, 0x11A8,                                 /* 27 */
        /* LVT */
        0xC00D,                                                 /* 30 */
        /* other */
        'a',                                                    /* 31 */
        0                                                       /* 32 */
    };
    static const int forward[] = { 6, 9, 16, 20, 22, 24, 27, 30, 31, 32 };
    static const int backward[] = { 31, 30, 27, 24, 22, 20, 16, 9, 6, 0 };

    const ucschar* begin = str;
    const ucschar* end = str + COUNTOF(str) - 1;
    const ucschar* s;
    size_t i;

    s = str;
    for (i = 0; i < COUNTOF(forward); i++) {
        s = hangul_syllable_iterator_next(s, end);
        ht_checks++;
        if (s - str != forward[i]) {
            HT_FAILF("syllable_iterator_next step %u: at %ld, expected %d",
                     (unsigned)i, (long)(s - str), forward[i]);
            break;
        }
    }

    s = end;
    for (i = 0; i < COUNTOF(backward); i++) {
        s = hangul_syllable_iterator_prev(s, begin);
        ht_checks++;
        if (s - str != backward[i]) {
            HT_FAILF("syllable_iterator_prev step %u: at %ld, expected %d",
                     (unsigned)i, (long)(s - str), backward[i]);
            break;
        }
    }

    /* hangul_syllable_len measures the same runs the iterator steps over. */
    HT_CHECK(hangul_syllable_len(str, (int)COUNTOF(str) - 1) == 6);
    HT_CHECK(hangul_syllable_len(str + 6, (int)COUNTOF(str) - 7) == 3);
}

int main(void)
{
    test_ucschar_width();
    test_jamo_syllable_roundtrip();
    test_jamos_to_syllables();
    test_classification();
    test_jamo_to_cjamo();
    test_syllable_iterator();

    return ht_report("hangul.ucs4");
}
