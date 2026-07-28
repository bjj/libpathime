/*
 * src/composition.cc — the structured model and its projection to the flat
 * public value (docs/design-history.md §2, Finding 1, and §3 question 1's answer).
 *
 * The projection is short enough to look obviously correct, which is exactly
 * why it is worth testing: the one thing it can get silently wrong is
 * preedit_settled, because that is a position and the API measures every
 * position in Unicode scalar values while every buffer it carries is sized in
 * bytes. The two agree for the ASCII a test is most likely to reach for and
 * diverge for every script the library actually serves — so the cases below
 * are deliberately Hangul, kana and Han rather than Latin.
 */

#include "core_test_util.h"

#include "composition.h"
#include "utf8.h"

namespace {

/* 한 U+D55C and 글 U+AE00, three bytes each. */
const char kHan[] = "\xED\x95\x9C";
const char kGeul[] = "\xEA\xB8\x80";

/* に U+306B, ほ U+307B, ん U+3093 — three bytes each. */
const char kNi[] = "\xE3\x81\xAB";
const char kHo[] = "\xE3\x81\xBB";
const char kN[] = "\xE3\x82\x93";

/* Project a model and hand back the flat value plus its backing storage. */
struct Projected {
    std::string preedit_storage;
    pathime_composition_t flat{};

    explicit Projected(const pathime::Composition &model)
    {
        pathime::project_composition(model, &preedit_storage, &flat);
    }

    std::string preedit() const { return std::string(flat.preedit.bytes, flat.preedit.len); }
};

void test_empty()
{
    pathime::Composition model;
    PT_CHECK(model.empty());

    const Projected p(model);

    /*
     * pathime_str_t promises that a zero-length slice points at "" rather than
     * NULL, and that everything the library produces is NUL-terminated even
     * though len is authoritative. A client that does strlen() on a preedit is
     * doing something the header allows.
     */
    PT_CHECK(p.flat.preedit.bytes != nullptr);
    PT_CHECK(p.flat.preedit.bytes[0] == '\0');
    PT_CHECK_SIZE(p.flat.preedit.len, 0);
    PT_CHECK_SIZE(p.flat.preedit_settled, 0);

    /* The library owns this struct, so it reports the size it wrote. */
    PT_CHECK_SIZE(p.flat.struct_size, sizeof(pathime_composition_t));
}

void test_concatenation_and_settled_boundary()
{
    /*
     * The three-part shape every backend turned out to share: pyzy's
     * selectedText/conversionText/restText, anthy's segments before/at/after
     * the active one, hangul's finished syllables plus the trailing syllable.
     */
    pathime::Composition model;
    model.settled = kHan;
    model.active = kGeul;
    model.tail = kNi;

    const Projected p(model);
    PT_CHECK_STR(p.preedit(), std::string(kHan) + kGeul + kNi);

    /*
     * The case this test exists for. Three scalars, nine bytes: a
     * preedit_settled of 3 would be the byte count and is the bug; 1 is the
     * scalar count and is correct.
     */
    PT_CHECK_SIZE(p.flat.preedit.len, 9);
    PT_CHECK_SIZE(p.flat.preedit_settled, 1);

    /*
     * Two settled syllables: still scalars, not the six bytes they occupy. The
     * preedit grows to four scalars and twelve bytes, which is the pair of
     * numbers worth watching — 2 and 12 are the right answers, and any test
     * that expects them to be the same unit is the thing that is wrong.
     */
    model.settled = std::string(kHan) + kGeul;
    const Projected q(model);
    PT_CHECK_SIZE(q.flat.preedit_settled, 2);
    PT_CHECK_SIZE(q.flat.preedit.len, 12);

    /*
     * The transient state just before a commit: everything settled. The header
     * calls this a state the projection must be able to express, not a resting
     * one, and preedit_settled then equals the whole scalar count.
     */
    model.settled = std::string(kNi) + kHo + kN;
    model.active.clear();
    model.tail.clear();
    const Projected r(model);
    PT_CHECK_SIZE(r.flat.preedit_settled, 3);
    PT_CHECK_SIZE(r.flat.preedit.len, 9);
    PT_CHECK_SIZE(r.flat.preedit_settled,
                  pathime::utf8_scalar_count(r.preedit_storage.data(),
                                             r.preedit_storage.size()));
}

void test_settled_never_exceeds_preedit()
{
    /*
     * An invariant the API states and a client may rely on: preedit_settled
     * ranges from 0 to the scalar count of the preedit. It holds by
     * construction here — the settled prefix is part of what is concatenated —
     * so this checks the construction rather than a guard.
     */
    pathime::Composition model;
    for (const char *settled : {"", kHan, kNi}) {
        for (const char *active : {"", kGeul, kHo}) {
            for (const char *tail : {"", kN}) {
                model.settled = settled;
                model.active = active;
                model.tail = tail;
                const Projected p(model);
                const size_t total = pathime::utf8_scalar_count(
                    p.preedit_storage.data(), p.preedit_storage.size());
                PT_CHECK(p.flat.preedit_settled <= total);
            }
        }
    }
}

void test_settle_active()
{
    /*
     * Greedy left-to-right resolution in one operation: the active span joins
     * the settled prefix and the candidate list that described it goes away,
     * because it described a span that has stopped being active.
     */
    pathime::Composition model;
    model.settled = kHan;
    model.active = kGeul;
    model.tail = kNi;
    model.candidates = {"a", "b", "c"};
    model.cursor = 2;

    model.settle_active();

    PT_CHECK_STR(model.settled, std::string(kHan) + kGeul);
    PT_CHECK_STR(model.active, "");
    PT_CHECK_STR(model.tail, kNi);  /* the backend refills `active` from here */
    PT_CHECK(model.candidates.empty());
    PT_CHECK_SIZE(model.cursor, 0);

    /* The settled boundary moved by exactly one scalar, not by three bytes. */
    const Projected p(model);
    PT_CHECK_SIZE(p.flat.preedit_settled, 2);
}

void test_clear_and_empty()
{
    pathime::Composition model;
    model.settled = kHan;
    model.active = kGeul;
    model.tail = kNi;
    model.candidates = {"x"};
    model.cursor = 1;
    PT_CHECK(!model.empty());

    model.clear();
    PT_CHECK(model.empty());
    PT_CHECK(model.candidates.empty());
    PT_CHECK_SIZE(model.cursor, 0);

    /* empty() is about what the client would see, so a model holding only a
     * candidate list — which cannot happen, but must not lie if it did — is
     * still reported empty on the strength of having no text. */
    model.candidates = {"x"};
    PT_CHECK(model.empty());
}

void test_output()
{
    pathime::Output out;
    PT_CHECK(out.empty());

    out.commit = "\xED\x95\x9C";
    PT_CHECK(!out.empty());
    out.clear();
    PT_CHECK(out.empty());
    PT_CHECK_STR(out.commit, "");

    /*
     * A deletion is expressed against the surrounding-text snapshot with the
     * origin at the cursor that snapshot reported, and negative offsets are the
     * normal case — the engine is almost always deleting text behind the caret
     * that it committed a moment ago. This is PATHIME_HANGUL_PREEDIT_NONE's
     * whole mechanism.
     */
    out.request_deletion(-1, 1);
    PT_CHECK(!out.empty());
    PT_CHECK(out.has_deletion);
    PT_CHECK(out.delete_offset == -1);
    PT_CHECK_SIZE(out.delete_count, 1);

    /* A deletion with no commit is a complete output on its own. */
    PT_CHECK_STR(out.commit, "");

    out.clear();
    PT_CHECK(!out.has_deletion);
    PT_CHECK_SIZE(out.delete_count, 0);
    PT_CHECK(out.delete_offset == 0);
}

void test_projection_is_wholesale()
{
    /*
     * The flat value is recomputed from the model on every change and never
     * patched incrementally — which is what keeps the two from drifting. So a
     * projection into storage that already holds a longer previous value must
     * replace it, not append to or partially overwrite it.
     */
    pathime::Composition model;
    model.settled = std::string(kHan) + kGeul + kNi;

    std::string preedit_storage;
    pathime_composition_t flat{};
    pathime::project_composition(model, &preedit_storage, &flat);
    PT_CHECK_SIZE(flat.preedit.len, 9);
    PT_CHECK_SIZE(flat.preedit_settled, 3);

    model.settled = kHan;
    pathime::project_composition(model, &preedit_storage, &flat);
    PT_CHECK_STR(std::string(flat.preedit.bytes, flat.preedit.len), kHan);
    PT_CHECK_SIZE(flat.preedit.len, 3);
    PT_CHECK_SIZE(flat.preedit_settled, 1);
    /* NUL-terminated at the new length, not the old one. */
    PT_CHECK(flat.preedit.bytes[flat.preedit.len] == '\0');

    /* And back to empty. */
    model.clear();
    pathime::project_composition(model, &preedit_storage, &flat);
    PT_CHECK_SIZE(flat.preedit.len, 0);
    PT_CHECK_SIZE(flat.preedit_settled, 0);
    PT_CHECK(flat.preedit.bytes[0] == '\0');
}

}  // namespace

int main()
{
    test_empty();
    test_concatenation_and_settled_boundary();
    test_settled_never_exceeds_preedit();
    test_settle_active();
    test_clear_and_empty();
    test_output();
    test_projection_is_wholesale();
    return pt_report("core.composition");
}
