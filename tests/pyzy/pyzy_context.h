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

/* Directories for the user cache (input history) and user config
 * (an override phrases.txt). Relative, so they land inside the per-test working
 * directory that CMake sets — the tests must not touch the real user profile.
 *
 * The *system* database is found separately: PyZy::Database only searches
 * PKGDATADIR, which points into an install prefix that does not exist in a
 * build tree, and then "main.db" relative to the working directory. The build
 * stages android.db there under that name. Same for phrases.txt, which
 * SpecialPhraseTable looks for in the working directory first. */
inline const char * userCacheDir  () { return "user-cache"; }
inline const char * userConfigDir () { return "user-config"; }

}  // namespace pyzy_test

#endif  /* LIBPATHIME_TESTS_PYZY_CONTEXT_H */
