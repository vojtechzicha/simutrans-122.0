/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef TESTS_FORK_TEST_HARNESS_H
#define TESTS_FORK_TEST_HARNESS_H


class karte_t;

/**
 * Fork: runs the test script (batch mode -forktest SCRIPT), writes SCRIPT.log and returns the number
 * of failures (errors and failed expect commands). See tests/README.md.
 */
int fork_test_run(karte_t *welt, const char *script);

#endif
