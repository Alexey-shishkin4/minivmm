/*
 * libFuzzer entry point, implemented by every tests/fuzz/fuzz_*.c target.
 */

#ifndef MINIVMM_TESTS_FUZZ_H
#define MINIVMM_TESTS_FUZZ_H

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#endif /* MINIVMM_TESTS_FUZZ_H */
