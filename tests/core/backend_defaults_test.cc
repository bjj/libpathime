/*
 * What a backend inherits by not overriding anything — the default bodies in
 * src/backend.h, and the core's side of the contracts they express.
 *
 * These are unreachable through any other suite, and unreachable for a reason
 * that will not fix itself: all four shipped adapters override every optional
 * method, so in a build of this library the defaults are dead code. They are
 * not dead *contract*. Each one is the answer a fifth backend would inherit by
 * saying nothing, and each is documented in backend.h as the right answer for
 * some real engine — set_cursor() declining is Hangul's case, declared_number()
 * and declared_text() answering "nothing declared" is every engine except the
 * table one, prepare_string() succeeding is every engine whose string options
 * name nothing that can be missing.
 *
 * So the stubs below are not stand-ins for a real adapter. They are the
 * minimum a backend can be — the five pure virtuals and not one line more —
 * and what they demonstrate is that such a backend works, which is the only
 * way to know what the defaults are worth.
 *
 * The other half is the core's response. `ContextBackend::set_cursor()`
 * returning PATHIME_ERROR_UNSUPPORTED is not merely a status: the header
 * promises it means *rejected, composition intact*, while any other failure
 * leaves the context indeterminate. That distinction lives in candidates.cc
 * and is asserted nowhere else, because no shipped adapter returns either
 * value from that method.
 */

#include "core_test_util.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <pathime/pathime.h>

#include "backend.h"
#include "context.h"
#include "engine.h"

#define PT_CHECK_STATUS(expr, expected)                                        \
    do {                                                                       \
        const pathime_status_t pt_got_ = (expr);                               \
        const pathime_status_t pt_want_ = (expected);                          \
        pt_checks++;                                                           \
        if (pt_got_ != pt_want_)                                               \
            PT_FAILF("%s: got %s, expected %s", #expr,                         \
                     pathime_status_string(pt_got_),                           \
                     pathime_status_string(pt_want_));                         \
    } while (0)

using namespace pathime;

namespace {

/* ---------------------------------------------------------------------------
 * The minimum a backend can be
 * ------------------------------------------------------------------------- */

/**
 * Every pure virtual and nothing else. set_cursor(), options_changed(),
 * declared_number(), declared_text() and prepare_string() are deliberately
 * absent, which is the whole subject of this file.
 *
 * @a cursor_status lets one case return something other than the inherited
 * answer, to reach the core's other arm.
 */
class StubContext : public ContextBackend {
public:
    explicit StubContext(pathime_status_t cursor_status = PATHIME_ERROR_UNSUPPORTED)
        : override_cursor_(cursor_status != PATHIME_ERROR_UNSUPPORTED),
          cursor_status_(cursor_status)
    {
    }

    bool process_key(const KeyEvent &, const OptionReader &, const SurroundingTextView &,
                     Composition *, Output *) override
    {
        return false;  /* absorbs nothing */
    }

    void reset(Composition *) override {}

    void commit(const OptionReader &, Composition *, Output *) override {}

    pathime_status_t select_candidate(size_t, const OptionReader &, Composition *,
                                      Output *) override
    {
        return PATHIME_ERROR_UNSUPPORTED;
    }

    void materialize_candidates(size_t, const OptionReader &, Composition *) override {}

    /*
     * Present only so that one case can return a *non*-UNSUPPORTED failure.
     * When constructed with the default the call is forwarded to the inherited
     * implementation, so that arm still measures backend.h's own body rather
     * than a copy of it.
     */
    pathime_status_t set_cursor(size_t index, const OptionReader &options,
                                Composition *model) override
    {
        if (override_cursor_) {
            return cursor_status_;
        }
        return ContextBackend::set_cursor(index, options, model);
    }

private:
    bool override_cursor_;
    pathime_status_t cursor_status_;
};

/** The engine-level minimum: create_context() and nothing else. */
class StubEngine : public EngineBackend {
public:
    explicit StubEngine(pathime_status_t cursor_status = PATHIME_ERROR_UNSUPPORTED)
        : cursor_status_(cursor_status)
    {
    }

    std::unique_ptr<ContextBackend> create_context(const OptionReader &) override
    {
        return std::unique_ptr<ContextBackend>(new StubContext(cursor_status_));
    }

private:
    pathime_status_t cursor_status_;
};

/**
 * The reader an adapter is handed. Nothing below consults it — the defaults
 * under test all ignore their options argument — but it has to be a real
 * object, because a reference bound to nothing is undefined behaviour whether
 * or not anyone reads through it.
 */
class StubOptions : public OptionReader {
public:
    int64_t number(pathime_option_t) const override { return 0; }
    const char *text(pathime_option_t) const override { return ""; }
};

void on_commit_text(void *, pathime_str_t) {}

void on_composition_changed(void *user_data, const pathime_composition_t *)
{
    if (user_data != nullptr) {
        ++*static_cast<int *>(user_data);
    }
}

/**
 * A context wired as pathime_context_create() would wire it, with a stub
 * backend behind it and a candidate list to move a cursor around in.
 *
 * The candidates are placed directly rather than materialized, because
 * materialize_candidates() is one of the things the stub does not do — and
 * the subject here is the cursor, not how the list was filled.
 */
void wire(pathime_context &ctx, pathime_engine &engine, int *changes,
          pathime_status_t cursor_status = PATHIME_ERROR_UNSUPPORTED)
{
    ctx.engine = &engine;
    ctx.user_data = changes;
    ctx.commit_text = on_commit_text;
    ctx.delete_surrounding_text = nullptr;
    ctx.composition_changed = on_composition_changed;
    ctx.backend.reset(new StubContext(cursor_status));
    engine.contexts.push_back(&ctx);

    ctx.model.active = "ab";
    ctx.model.candidates.push_back("first");
    ctx.model.candidates.push_back("second");
    ctx.model.candidates.push_back("third");
    ctx.model.cursor = 0;

    refresh_composition(&ctx, false);
    if (changes != nullptr) {
        *changes = 0;
    }
}

/* ---------------------------------------------------------------------------
 * The defaults themselves
 * ------------------------------------------------------------------------- */

/*
 * Called directly, because the point is what the inherited body returns and
 * routing through the core would only prove the core forwards. The engine-level
 * three have no route from the public API for a non-table engine anyway:
 * options.cc consults tier 3 only where a table can declare something.
 */
void test_inherited_defaults()
{
    StubEngine engine;

    /*
     * Tier 3, declined. "Every implementation must answer false for an empty
     * table_file rather than reaching for a default table" — and a backend that
     * declares nothing answers false for every table_file, which is what makes
     * saying so once here cheaper than three adapters each declining.
     */
    int64_t number = 12345;
    PT_CHECK(!engine.declared_number("", PATHIME_OPT_TABLE_AUTO_COMMIT, &number));
    PT_CHECK(!engine.declared_number("some-table.db", PATHIME_OPT_TABLE_AUTO_COMMIT,
                                     &number));
    /* Declining leaves the out-parameter alone. */
    PT_CHECK(number == 12345);

    /* The string counterpart: nullptr is "declares none", distinct from "". */
    PT_CHECK(engine.declared_text("", PATHIME_OPT_TABLE_SINGLE_WILDCARD) == nullptr);
    PT_CHECK(engine.declared_text("some-table.db", PATHIME_OPT_TABLE_FILE) == nullptr);

    /*
     * A string option implies no work, so there is none to fail. Returning OK
     * is what lets a setter for an option this backend has never heard of
     * proceed to the store.
     */
    PT_CHECK_STATUS(engine.prepare_string(PATHIME_OPT_TABLE_FILE, "anything"),
                    PATHIME_OK);
    PT_CHECK_STATUS(engine.prepare_string(PATHIME_OPT_TABLE_FILE, ""), PATHIME_OK);

    /*
     * The context-level default: a backend with no candidates to hover
     * declines rather than reporting a success that would leave the cursor and
     * the preedit disagreeing.
     */
    const StubOptions options;
    StubContext probe;
    Composition model;
    PT_CHECK_STATUS(probe.set_cursor(0, options, &model), PATHIME_ERROR_UNSUPPORTED);

    /* options_changed() is the other inherited no-op: it must touch nothing. */
    Output out;
    model.active = "unchanged";
    probe.options_changed(options, &model, &out);
    PT_CHECK_STR(model.active, "unchanged");

    /* create_context() is the one thing a backend must supply, and it works. */
    PT_CHECK(engine.create_context(options) != nullptr);
}

/* ---------------------------------------------------------------------------
 * The core's side of the UNSUPPORTED contract
 * ------------------------------------------------------------------------- */

/*
 * "UNSUPPORTED is a rejection: the backend declined to hover and changed
 * nothing, so the composition is intact." The observable consequences are
 * three, and all three are what a client would notice: the status, the cursor
 * back where it started, and a context that is still usable.
 */
void test_unsupported_cursor_is_a_clean_rejection()
{
    pathime_engine engine;
    StubEngine backend;
    engine.id = PATHIME_ENGINE_HANGUL;
    engine.backend.reset(new StubEngine());

    pathime_context ctx;
    int changes = 0;
    wire(ctx, engine, &changes);

    /* Move somewhere other than 0 first, so a rollback is visible. */
    ctx.model.cursor = 2;
    refresh_composition(&ctx, false);
    changes = 0;

    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(&ctx, 1), PATHIME_ERROR_UNSUPPORTED);

    /* Rolled back, not left at the requested index. */
    PT_CHECK_SIZE(ctx.model.cursor, 2);

    /* Intact: a rejection is not a failure the client must reset out of. */
    PT_CHECK(!ctx.indeterminate);

    /* The composition the client can see still describes the same preedit. */
    PT_CHECK_STR(std::string(ctx.composition.preedit.bytes, ctx.composition.preedit.len),
                 "ab");
    PT_CHECK_SIZE(ctx.composition.candidate_count, 3);
    PT_CHECK_SIZE(ctx.composition.candidate_cursor, 2);

    /* Nothing changed, so nothing was announced. */
    PT_CHECK(changes == 0);

    (void)backend;
}

/*
 * The other arm, and the reason the first is written as a distinction rather
 * than as "set_cursor may fail": any failure that is *not* UNSUPPORTED got
 * partway, so the context is indeterminate until reset.
 */
void test_other_failures_leave_the_context_indeterminate()
{
    pathime_engine engine;
    engine.id = PATHIME_ENGINE_HANGUL;
    engine.backend.reset(new StubEngine(PATHIME_ERROR_BACKEND));

    pathime_context ctx;
    int changes = 0;
    wire(ctx, engine, &changes, PATHIME_ERROR_BACKEND);

    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(&ctx, 1), PATHIME_ERROR_BACKEND);

    /* The cursor is still rolled back — that part is common to every failure. */
    PT_CHECK_SIZE(ctx.model.cursor, 0);

    /* But the context is now indeterminate, which UNSUPPORTED never causes. */
    PT_CHECK(ctx.indeterminate);
}

/*
 * Out of range is the core's own rejection and never reaches the backend, which
 * is what lets ContextBackend::set_cursor() take its index on trust. Worth
 * pinning here because the stub would happily accept any index at all.
 */
void test_out_of_range_never_reaches_the_backend()
{
    pathime_engine engine;
    engine.id = PATHIME_ENGINE_HANGUL;
    engine.backend.reset(new StubEngine());

    pathime_context ctx;
    int changes = 0;
    wire(ctx, engine, &changes);

    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(&ctx, 3), PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(&ctx, 99), PATHIME_ERROR_INVALID_ARGUMENT);

    /* INVALID_ARGUMENT is a rejection too: nothing moved, nothing spoiled. */
    PT_CHECK_SIZE(ctx.model.cursor, 0);
    PT_CHECK(!ctx.indeterminate);
}

}  // namespace

int main(void)
{
    /* The public entry points below refuse to run before initialization. */
    if (pathime_init(NULL) != PATHIME_OK) {
        PT_FAILF("%s", "pathime_init failed");
        return pt_report("core.backend_defaults");
    }

    test_inherited_defaults();
    test_unsupported_cursor_is_a_clean_rejection();
    test_other_failures_leave_the_context_indeterminate();
    test_out_of_range_never_reaches_the_backend();

    pathime_shutdown();
    return pt_report("core.backend_defaults");
}
