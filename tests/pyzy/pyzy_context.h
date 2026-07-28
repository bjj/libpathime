/* Shared scaffolding for the PyZy::InputContext conversion tests.
 *
 * Note the `override` on every Observer method. pyzy's own test
 * (src/tests/basic.cc) declares them taking `const InputContext *` where the
 * interface says `InputContext *`, so none of them override anything, the
 * class stays abstract, and the file has not compiled since the signature
 * changed — see the comment in tests/pyzy/CMakeLists.txt. `override` turns
 * exactly that mistake into a compile error instead of a puzzling one about
 * instantiating an abstract class, which is worth the keystrokes here.
 */
#ifndef LIBPATHIME_TESTS_PYZY_CONTEXT_H
#define LIBPATHIME_TESTS_PYZY_CONTEXT_H

#include <PyZy/DataDir.h>
#include <PyZy/InputContext.h>

#include <string>

namespace pyzy_test {

/* Records what the engine commits, and counts the other notifications so a
 * test can assert that a mutation actually produced callbacks. */
class Recorder : public PyZy::InputContext::Observer {
public:
    Recorder () : m_notifications (0) { }

    void commitText (PyZy::InputContext *, const std::string &text) override
    { m_committed += text; }

    void inputTextChanged     (PyZy::InputContext *) override { ++m_notifications; }
    void cursorChanged        (PyZy::InputContext *) override { ++m_notifications; }
    void preeditTextChanged   (PyZy::InputContext *) override { ++m_notifications; }
    void auxiliaryTextChanged (PyZy::InputContext *) override { ++m_notifications; }
    void candidatesChanged    (PyZy::InputContext *) override { ++m_notifications; }

    const std::string & committed () const { return m_committed; }
    int notifications () const { return m_notifications; }

    void clear () { m_committed.clear (); m_notifications = 0; }

private:
    std::string m_committed;
    int         m_notifications;
};

inline void insertKeys (PyZy::InputContext *context, const char *keys)
{
    for (const char *p = keys; *p != '\0'; ++p)
        context->insert (*p);
}

/* Directories for the user cache (input history) and user config (an override
 * phrases.txt). Absolute, beneath the directory this test owns — the tests must
 * not touch the real user profile — so nothing depends on where they are run
 * from. */
inline std::string userCacheDir  () { return std::string (PYZY_TEST_HOME) + "/user-cache"; }
inline std::string userConfigDir () { return std::string (PYZY_TEST_HOME) + "/user-config"; }

#ifdef PYZY_TEST_DATA_DIR
/* Where the shipped data is: the main.db and phrases.txt this build staged.
 * Call before PyZy::InputContext::init(), which is where the database is
 * opened. Only defined for the tests that were given a database. */
inline void useStagedDatabase () { pyzy_set_data_dir (PYZY_TEST_DATA_DIR); }
#endif

}  // namespace pyzy_test

#endif  /* LIBPATHIME_TESTS_PYZY_CONTEXT_H */
