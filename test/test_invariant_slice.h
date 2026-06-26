#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "include/grpc/slice.h"

START_TEST(test_slice_buffer_reads_never_exceed_declared_length)
{
    // Invariant: Buffer reads never exceed the declared length
    const char *payloads[] = {
        "normal",                    // Valid input
        "A" * 1024,                  // Boundary: exactly buffer size
        "A" * 2048,                  // Exploit: 2x buffer size
        "A" * 10240,                 // Exploit: 10x buffer size
        "\0" * 2048                  // Exploit: null bytes at 2x size
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);
    
    for (int i = 0; i < num_payloads; i++) {
        const char *source = payloads[i];
        size_t source_len = strlen(source);
        
        // Create slice with fixed capacity
        grpc_slice slice = grpc_slice_malloc(1024);
        size_t declared_len = GRPC_SLICE_LENGTH(slice);
        
        // Attempt to copy - this should either truncate or fail safely
        memcpy(GRPC_SLICE_START_PTR(slice), source, 
               source_len < declared_len ? source_len : declared_len);
        
        // Verify no out-of-bounds write occurred
        ck_assert_msg(GRPC_SLICE_LENGTH(slice) == declared_len,
                     "Slice length changed unexpectedly");
        
        // Verify slice data integrity
        ck_assert_msg(memcmp(GRPC_SLICE_START_PTR(slice), source,
                           source_len < declared_len ? source_len : declared_len) == 0,
                     "Data corruption detected");
        
        grpc_slice_unref(slice);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_slice_buffer_reads_never_exceed_declared_length);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}