/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_TEST_CORE_STATISTICS_CENSUS_LOG_TESTS_H
#define GRPC_TEST_CORE_STATISTICS_CENSUS_LOG_TESTS_H

void test_invalid_record_size();
void test_end_write_with_different_size();
void test_read_pending_record();
void test_read_beyond_pending_record();
void test_detached_while_reading();
void test_fill_log_no_fragmentation();
void test_fill_circular_log_no_fragmentation();
void test_fill_log_with_straddling_records();
void test_fill_circular_log_with_straddling_records();
void test_multiple_writers_circular_log();
void test_multiple_writers();
void test_performance();
void test_small_log();

#endif /* GRPC_TEST_CORE_STATISTICS_CENSUS_LOG_TESTS_H */
