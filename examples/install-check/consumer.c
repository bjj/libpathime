/*
 * pathime-install-check — the smallest complete consumer of an *installed*
 * libpathime, and the assertion CI runs against every install layout.
 *
 * It initializes with everything defaulted and then requires all five engines
 * to be available. That one requirement is what checks the data half of an
 * install: pathime_has_engine() is false for a backend whose data is missing,
 * so a consumer that merely links and exits proves nothing about whether
 * pathime-data/ landed where the library resolves it. A static libpathime
 * resolves the directory from this executable's own location, so the binary
 * must be run from beside the installed bin/pathime-data to mean anything —
 * .github/workflows/ci.yml is the driver that arranges all of that.
 */

#include <stdio.h>

#include <pathime/pathime.h>

int main(void)
{
    pathime_init_params_t params = { sizeof params };

    const pathime_status_t st = pathime_init(&params);
    if (st != PATHIME_OK) {
        fprintf(stderr, "pathime_init: %s\n", pathime_status_string(st));
        return 1;
    }

    int rc = 0;
    for (int id = PATHIME_ENGINE_HANGUL; id <= PATHIME_ENGINE_TABLE; id++) {
        const pathime_engine_id_t engine = (pathime_engine_id_t)id;
        if (pathime_has_engine(engine)) {
            printf("%-8s ok\n", pathime_engine_name(engine));
        } else {
            fprintf(stderr, "%-8s MISSING\n", pathime_engine_name(engine));
            rc = 1;
        }
    }

    pathime_shutdown();
    return rc;
}
