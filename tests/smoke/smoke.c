/* SPDX-License-Identifier: MIT
 * Smoke test: load dragdoll.dll, check version, init/shutdown twice, check error paths. */
#include <dragdoll/dragdoll.h>

#include <stdio.h>

static int gFailures = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            printf("FAILED: %s (line %d) last error: '%s'\n", #cond, __LINE__, dragdoll_last_error_message());         \
            ++gFailures;                                                                                               \
        }                                                                                                              \
    } while (0)

int main(void)
{
    printf("%s\n", dragdoll_get_build_info());

    CHECK(dragdoll_get_version() == DD_API_VERSION);
    CHECK(sizeof(void*) == 4 || sizeof(void*) == 8);

    /* Wrong major version must be rejected. */
    CHECK(dragdoll_init(DD_MAKE_VERSION(DD_API_VERSION_MAJOR + 1, 0, 0)) == DD_ERROR_VERSION_MISMATCH);

    /* Two full init/shutdown cycles (hosts may reload). */
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        CHECK(dragdoll_init(DD_API_VERSION) == DD_OK);
        CHECK(dragdoll_init(DD_API_VERSION) == DD_ERROR_ALREADY_INITIALIZED);
        dragdoll_shutdown();
    }
    dragdoll_shutdown(); /* Safe when not initialised. */

    if (gFailures == 0)
    {
        printf("smoke: OK\n");
        return 0;
    }
    printf("smoke: %d failure(s)\n", gFailures);
    return 1;
}
