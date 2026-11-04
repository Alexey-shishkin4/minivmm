/*
 * Minimal TAP (Test Anything Protocol) helpers for unit tests; meson runs the
 * tests with protocol: 'tap'.
 *
 *   int main(void) {
 *       tap_plan(2);
 *       tap_ok(1 + 1 == 2, "addition");
 *       tap_ok(gpa_to_hva(...) == NULL, "out-of-range GPA rejected");
 *       return tap_done();
 *   }
 */

#ifndef MINIVMM_TESTS_TAP_H
#define MINIVMM_TESTS_TAP_H

#include <stdbool.h>
#include <stdio.h>

static unsigned tap_count_;
static unsigned tap_failed_;

static inline void tap_plan(unsigned n)
{
    printf("1..%u\n", n);
}

static inline bool tap_ok_(bool cond, const char *name, const char *expr,
                           const char *file, int line)
{
    tap_count_++;
    printf("%sok %u - %s\n", cond ? "" : "not ", tap_count_, name);
    if (!cond) {
        tap_failed_++;
        printf("#   failed: %s\n#   at %s:%d\n", expr, file, line);
    }
    return cond;
}

#define tap_ok(cond, name) tap_ok_((cond), (name), #cond, __FILE__, __LINE__)

static inline int tap_done(void)
{
    return tap_failed_ ? 1 : 0;
}

#endif /* MINIVMM_TESTS_TAP_H */
