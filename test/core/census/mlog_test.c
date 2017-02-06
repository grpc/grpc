/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/ext/census/mlog.h"
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test/core/util/test_config.h"

// Change this to non-zero if you want more output.
#define VERBOSE 0

// Log size to use for all tests.
#define LOG_SIZE_IN_MB 1
#define LOG_SIZE_IN_BYTES (LOG_SIZE_IN_MB << 20)

// Fills in 'record' of size 'size'. Each byte in record is filled in with the
// same value. The value is extracted from 'record' pointer.
static void write_record(char* record, size_t size) {
  char data = (char)((uintptr_t)record % 255);
  memset(record, data, size);
}

// Reads fixed size records. Returns the number of records read in
// 'num_records'.
static void read_records(size_t record_size, const char* buffer,
                         size_t buffer_size, int* num_records) {
  GPR_ASSERT(buffer_size >= record_size);
  GPR_ASSERT(buffer_size % record_size == 0);
  *num_records = (int)(buffer_size / record_size);
  for (int i = 0; i < *num_records; ++i) {
    const char* record = buffer + (record_size * (size_t)i);
    char data = (char)((uintptr_t)record % 255);
    for (size_t j = 0; j < record_size; ++j) {
      GPR_ASSERT(data == record[j]);
    }
  }
}

// Tries to write the specified number of records. Stops when the log gets
// full. Returns the number of records written. Spins for random
// number of times, up to 'max_spin_count', between writes.
static int write_records_to_log(int writer_id, size_t record_size,
                                int num_records, int max_spin_count) {
  int counter = 0;
  for (int i = 0; i < num_records; ++i) {
    int spin_count = max_spin_count ? rand() % max_spin_count : 0;
    if (VERBOSE && (counter++ == num_records / 10)) {
      printf("   Writer %d: %d out of %d written\n", writer_id, i, num_records);
      counter = 0;
    }
    char* record = (char*)(census_log_start_write(record_size));
    if (record == NULL) {
      return i;
    }
    write_record(record, record_size);
    census_log_end_write(record, record_size);
    for (int j = 0; j < spin_count; ++j) {
      GPR_ASSERT(j >= 0);
    }
  }
  return num_records;
}

// Performs a single read iteration. Returns the number of records read.
static int perform_read_iteration(size_t record_size) {
  const void* read_buffer = NULL;
  size_t bytes_available;
  int records_read = 0;
  census_log_init_reader();
  while ((read_buffer = census_log_read_next(&bytes_available))) {
    int num_records = 0;
    read_records(record_size, (const char*)read_buffer, bytes_available,
                 &num_records);
    records_read += num_records;
  }
  return records_read;
}

// Asserts that the log is empty.
static void assert_log_empty(void) {
  census_log_init_reader();
  size_t bytes_available;
  GPR_ASSERT(census_log_read_next(&bytes_available) == NULL);
}

// Fills the log and verifies data. If 'no fragmentation' is true, records
// are sized such that CENSUS_LOG_2_MAX_RECORD_SIZE is a multiple of record
// size. If not a circular log, verifies that the number of records written
// match the number of records read.
static void fill_log(size_t log_size, int no_fragmentation, int circular_log) {
  size_t size;
  if (no_fragmentation) {
    int log2size = rand() % (CENSUS_LOG_2_MAX_RECORD_SIZE + 1);
    size = ((size_t)1 << log2size);
  } else {
    while (1) {
      size = 1 + ((size_t)rand() % CENSUS_LOG_MAX_RECORD_SIZE);
      if (CENSUS_LOG_MAX_RECORD_SIZE % size) {
        break;
      }
    }
  }
  int records_written =
      write_records_to_log(0 /* writer id */, size,
                           (int)((log_size / size) * 2), 0 /* spin count */);
  int records_read = perform_read_iteration(size);
  if (!circular_log) {
    GPR_ASSERT(records_written == records_read);
  }
  assert_log_empty();
}

// Structure to pass args to writer_thread
typedef struct writer_thread_args {
  // Index of this thread in the writers vector.
  int index;
  // Record size.
  size_t record_size;
  // Number of records to write.
  int num_records;
  // Used to signal when writer is complete
  gpr_cv* done;
  gpr_mu* mu;
  int* count;
} writer_thread_args;

// Writes the given number of records of random size (up to kMaxRecordSize) and
// random data to the specified log.
static void writer_thread(void* arg) {
  writer_thread_args* args = (writer_thread_args*)arg;
  // Maximum number of times to spin between writes.
  static const int MAX_SPIN_COUNT = 50;
  int records_written = 0;
  if (VERBOSE) {
    printf("   Writer %d starting\n", args->index);
  }
  while (records_written < args->num_records) {
    records_written += write_records_to_log(args->index, args->record_size,
                                            args->num_records - records_written,
                                            MAX_SPIN_COUNT);
    if (records_written < args->num_records) {
      // Ran out of log space. Sleep for a bit and let the reader catch up.
      // This should never happen for circular logs.
      if (VERBOSE) {
        printf(
            "   Writer %d stalled due to out-of-space: %d out of %d "
            "written\n",
            args->index, records_written, args->num_records);
      }
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(10));
    }
  }
  // Done. Decrement count and signal.
  gpr_mu_lock(args->mu);
  (*args->count)--;
  gpr_cv_signal(args->done);
  if (VERBOSE) {
    printf("   Writer %d done\n", args->index);
  }
  gpr_mu_unlock(args->mu);
}

// struct to pass args to reader_thread
typedef struct reader_thread_args {
  // Record size.
  size_t record_size;
  // Interval between read iterations.
  int read_iteration_interval_in_msec;
  // Total number of records.
  int total_records;
  // Signalled when reader should stop.
  gpr_cv stop;
  int stop_flag;
  // Used to signal when reader has finished
  gpr_cv* done;
  gpr_mu* mu;
  int running;
} reader_thread_args;

// Reads and verifies the specified number of records. Reader can also be
// stopped via gpr_cv_signal(&args->stop). Sleeps for 'read_interval_in_msec'
// between read iterations.
static void reader_thread(void* arg) {
  reader_thread_args* args = (reader_thread_args*)arg;
  if (VERBOSE) {
    printf("   Reader starting\n");
  }
  gpr_timespec interval = gpr_time_from_micros(
      args->read_iteration_interval_in_msec * 1000, GPR_TIMESPAN);
  gpr_mu_lock(args->mu);
  int records_read = 0;
  int num_iterations = 0;
  int counter = 0;
  while (!args->stop_flag && records_read < args->total_records) {
    gpr_cv_wait(&args->stop, args->mu, interval);
    if (!args->stop_flag) {
      records_read += perform_read_iteration(args->record_size);
      GPR_ASSERT(records_read <= args->total_records);
      if (VERBOSE && (counter++ == 100000)) {
        printf("   Reader: %d out of %d read\n", records_read,
               args->total_records);
        counter = 0;
      }
      ++num_iterations;
    }
  }
  // Done
  args->running = 0;
  gpr_cv_signal(args->done);
  if (VERBOSE) {
    printf("   Reader: records: %d, iterations: %d\n", records_read,
           num_iterations);
  }
  gpr_mu_unlock(args->mu);
}

// Creates NUM_WRITERS writers where each writer writes NUM_RECORDS_PER_WRITER
// records. Also, starts a reader that iterates over and reads blocks every
// READ_ITERATION_INTERVAL_IN_MSEC.
// Number of writers.
#define NUM_WRITERS 5
static void multiple_writers_single_reader(int circular_log) {
  // Sleep interval between read iterations.
  static const int READ_ITERATION_INTERVAL_IN_MSEC = 10;
  // Maximum record size.
  static const size_t MAX_RECORD_SIZE = 20;
  // Number of records written by each writer. This is sized such that we
  // will write through the entire log ~10 times.
  const int NUM_RECORDS_PER_WRITER =
      (int)((10 * census_log_remaining_space()) / (MAX_RECORD_SIZE / 2)) /
      NUM_WRITERS;
  size_t record_size = ((size_t)rand() % MAX_RECORD_SIZE) + 1;
  // Create and start writers.
  writer_thread_args writers[NUM_WRITERS];
  int writers_count = NUM_WRITERS;
  gpr_cv writers_done;
  gpr_mu writers_mu;  // protects writers_done and writers_count
  gpr_cv_init(&writers_done);
  gpr_mu_init(&writers_mu);
  gpr_thd_id id;
  for (int i = 0; i < NUM_WRITERS; ++i) {
    writers[i].index = i;
    writers[i].record_size = record_size;
    writers[i].num_records = NUM_RECORDS_PER_WRITER;
    writers[i].done = &writers_done;
    writers[i].count = &writers_count;
    writers[i].mu = &writers_mu;
    gpr_thd_new(&id, &writer_thread, &writers[i], NULL);
  }
  // Start reader.
  gpr_cv reader_done;
  gpr_mu reader_mu;  // protects reader_done and reader.running
  reader_thread_args reader;
  reader.record_size = record_size;
  reader.read_iteration_interval_in_msec = READ_ITERATION_INTERVAL_IN_MSEC;
  reader.total_records = NUM_WRITERS * NUM_RECORDS_PER_WRITER;
  reader.stop_flag = 0;
  gpr_cv_init(&reader.stop);
  gpr_cv_init(&reader_done);
  reader.done = &reader_done;
  gpr_mu_init(&reader_mu);
  reader.mu = &reader_mu;
  reader.running = 1;
  gpr_thd_new(&id, &reader_thread, &reader, NULL);
  // Wait for writers to finish.
  gpr_mu_lock(&writers_mu);
  while (writers_count != 0) {
    gpr_cv_wait(&writers_done, &writers_mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&writers_mu);
  gpr_mu_destroy(&writers_mu);
  gpr_cv_destroy(&writers_done);
  gpr_mu_lock(&reader_mu);
  if (circular_log) {
    // Stop reader.
    reader.stop_flag = 1;
    gpr_cv_signal(&reader.stop);
  }
  // wait for reader to finish
  while (reader.running) {
    gpr_cv_wait(&reader_done, &reader_mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  if (circular_log) {
    // Assert that there were no out-of-space errors.
    GPR_ASSERT(0 == census_log_out_of_space_count());
  }
  gpr_mu_unlock(&reader_mu);
  gpr_mu_destroy(&reader_mu);
  gpr_cv_destroy(&reader_done);
  if (VERBOSE) {
    printf("   Reader: finished\n");
  }
}

static void setup_test(int circular_log) {
  census_log_initialize(LOG_SIZE_IN_MB, circular_log);
  //  GPR_ASSERT(census_log_remaining_space() == LOG_SIZE_IN_BYTES);
}

// Attempts to create a record of invalid size (size >
// CENSUS_LOG_MAX_RECORD_SIZE).
void test_invalid_record_size(void) {
  static const size_t INVALID_SIZE = CENSUS_LOG_MAX_RECORD_SIZE + 1;
  static const size_t VALID_SIZE = 1;
  printf("Starting test: invalid record size\n");
  setup_test(0);
  void* record = census_log_start_write(INVALID_SIZE);
  GPR_ASSERT(record == NULL);
  // Now try writing a valid record.
  record = census_log_start_write(VALID_SIZE);
  GPR_ASSERT(record != NULL);
  census_log_end_write(record, VALID_SIZE);
  // Verifies that available space went down by one block. In theory, this
  // check can fail if the thread is context switched to a new CPU during the
  // start_write execution (multiple blocks get allocated), but this has not
  // been observed in practice.
  //  GPR_ASSERT(LOG_SIZE_IN_BYTES - CENSUS_LOG_MAX_RECORD_SIZE ==
  //             census_log_remaining_space());
  census_log_shutdown();
}

// Tests end_write() with a different size than what was specified in
// start_write().
void test_end_write_with_different_size(void) {
  static const size_t START_WRITE_SIZE = 10;
  static const size_t END_WRITE_SIZE = 7;
  printf("Starting test: end write with different size\n");
  setup_test(0);
  void* record_written = census_log_start_write(START_WRITE_SIZE);
  GPR_ASSERT(record_written != NULL);
  census_log_end_write(record_written, END_WRITE_SIZE);
  census_log_init_reader();
  size_t bytes_available;
  const void* record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(record_written == record_read);
  GPR_ASSERT(END_WRITE_SIZE == bytes_available);
  assert_log_empty();
  census_log_shutdown();
}

// Verifies that pending records are not available via read_next().
void test_read_pending_record(void) {
  static const size_t PR_RECORD_SIZE = 1024;
  printf("Starting test: read pending record\n");
  setup_test(0);
  // Start a write.
  void* record_written = census_log_start_write(PR_RECORD_SIZE);
  GPR_ASSERT(record_written != NULL);
  // As write is pending, read should fail.
  census_log_init_reader();
  size_t bytes_available;
  const void* record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(record_read == NULL);
  // A read followed by end_write() should succeed.
  census_log_end_write(record_written, PR_RECORD_SIZE);
  census_log_init_reader();
  record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(record_written == record_read);
  GPR_ASSERT(PR_RECORD_SIZE == bytes_available);
  assert_log_empty();
  census_log_shutdown();
}

// Tries reading beyond pending write.
void test_read_beyond_pending_record(void) {
  printf("Starting test: read beyond pending record\n");
  setup_test(0);
  // Start a write.
  const size_t incomplete_record_size = 10;
  void* incomplete_record = census_log_start_write(incomplete_record_size);
  GPR_ASSERT(incomplete_record != NULL);
  const size_t complete_record_size = 20;
  void* complete_record = census_log_start_write(complete_record_size);
  GPR_ASSERT(complete_record != NULL);
  GPR_ASSERT(complete_record != incomplete_record);
  census_log_end_write(complete_record, complete_record_size);
  // Now iterate over blocks to read completed records.
  census_log_init_reader();
  size_t bytes_available;
  const void* record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(complete_record == record_read);
  GPR_ASSERT(complete_record_size == bytes_available);
  // Complete first record.
  census_log_end_write(incomplete_record, incomplete_record_size);
  // Have read past the incomplete record, so read_next() should return NULL.
  // NB: this test also assumes our thread did not get switched to a different
  // CPU between the two start_write calls
  record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(record_read == NULL);
  // Reset reader to get the newly completed record.
  census_log_init_reader();
  record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(incomplete_record == record_read);
  GPR_ASSERT(incomplete_record_size == bytes_available);
  assert_log_empty();
  census_log_shutdown();
}

// Tests scenario where block being read is detached from a core and put on the
// dirty list.
void test_detached_while_reading(void) {
  printf("Starting test: detached while reading\n");
  setup_test(0);
  // Start a write.
  static const size_t DWR_RECORD_SIZE = 10;
  void* record_written = census_log_start_write(DWR_RECORD_SIZE);
  GPR_ASSERT(record_written != NULL);
  census_log_end_write(record_written, DWR_RECORD_SIZE);
  // Read this record.
  census_log_init_reader();
  size_t bytes_available;
  const void* record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(record_read != NULL);
  GPR_ASSERT(DWR_RECORD_SIZE == bytes_available);
  // Now fill the log. This will move the block being read from core-local
  // array to the dirty list.
  while ((record_written = census_log_start_write(DWR_RECORD_SIZE))) {
    census_log_end_write(record_written, DWR_RECORD_SIZE);
  }

  // In this iteration, read_next() should only traverse blocks in the
  // core-local array. Therefore, we expect at most gpr_cpu_num_cores() more
  // blocks. As log is full, if read_next() is traversing the dirty list, we
  // will get more than gpr_cpu_num_cores() blocks.
  int block_read = 0;
  while ((record_read = census_log_read_next(&bytes_available))) {
    ++block_read;
    GPR_ASSERT(block_read <= (int)gpr_cpu_num_cores());
  }
  census_log_shutdown();
}

// Fills non-circular log with records sized such that size is a multiple of
// CENSUS_LOG_MAX_RECORD_SIZE (no per-block fragmentation).
void test_fill_log_no_fragmentation(void) {
  printf("Starting test: fill log no fragmentation\n");
  const int circular = 0;
  setup_test(circular);
  fill_log(LOG_SIZE_IN_BYTES, 1 /* no fragmentation */, circular);
  census_log_shutdown();
}

// Fills circular log with records sized such that size is a multiple of
// CENSUS_LOG_MAX_RECORD_SIZE (no per-block fragmentation).
void test_fill_circular_log_no_fragmentation(void) {
  printf("Starting test: fill circular log no fragmentation\n");
  const int circular = 1;
  setup_test(circular);
  fill_log(LOG_SIZE_IN_BYTES, 1 /* no fragmentation */, circular);
  census_log_shutdown();
}

// Fills non-circular log with records that may straddle end of a block.
void test_fill_log_with_straddling_records(void) {
  printf("Starting test: fill log with straddling records\n");
  const int circular = 0;
  setup_test(circular);
  fill_log(LOG_SIZE_IN_BYTES, 0 /* block straddling records */, circular);
  census_log_shutdown();
}

// Fills circular log with records that may straddle end of a block.
void test_fill_circular_log_with_straddling_records(void) {
  printf("Starting test: fill circular log with straddling records\n");
  const int circular = 1;
  setup_test(circular);
  fill_log(LOG_SIZE_IN_BYTES, 0 /* block straddling records */, circular);
  census_log_shutdown();
}

// Tests scenario where multiple writers and a single reader are using a log
// that is configured to discard old records.
void test_multiple_writers_circular_log(void) {
  printf("Starting test: multiple writers circular log\n");
  const int circular = 1;
  setup_test(circular);
  multiple_writers_single_reader(circular);
  census_log_shutdown();
}

// Tests scenario where multiple writers and a single reader are using a log
// that is configured to discard old records.
void test_multiple_writers(void) {
  printf("Starting test: multiple writers\n");
  const int circular = 0;
  setup_test(circular);
  multiple_writers_single_reader(circular);
  census_log_shutdown();
}

// Repeat the straddling records and multiple writers tests with a small log.
void test_small_log(void) {
  printf("Starting test: small log\n");
  const int circular = 0;
  census_log_initialize(0, circular);
  size_t log_size = census_log_remaining_space();
  GPR_ASSERT(log_size > 0);
  fill_log(log_size, 0, circular);
  census_log_shutdown();
  census_log_initialize(0, circular);
  multiple_writers_single_reader(circular);
  census_log_shutdown();
}

void test_performance(void) {
  for (size_t write_size = 1; write_size < CENSUS_LOG_MAX_RECORD_SIZE;
       write_size *= 2) {
    setup_test(0);
    gpr_timespec start_time = gpr_now(GPR_CLOCK_REALTIME);
    int nrecords = 0;
    while (1) {
      void* record = census_log_start_write(write_size);
      if (record == NULL) {
        break;
      }
      census_log_end_write(record, write_size);
      nrecords++;
    }
    gpr_timespec write_time =
        gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), start_time);
    double write_time_micro =
        (double)write_time.tv_sec * 1000000 + (double)write_time.tv_nsec / 1000;
    census_log_shutdown();
    printf(
        "Wrote %d %d byte records in %.3g microseconds: %g records/us "
        "(%g ns/record), %g gigabytes/s\n",
        nrecords, (int)write_size, write_time_micro,
        nrecords / write_time_micro, 1000 * write_time_micro / nrecords,
        (double)((int)write_size * nrecords) / write_time_micro / 1000);
  }
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  gpr_time_init();
  srand((unsigned)gpr_now(GPR_CLOCK_REALTIME).tv_nsec);
  test_invalid_record_size();
  test_end_write_with_different_size();
  test_read_pending_record();
  test_read_beyond_pending_record();
  test_detached_while_reading();
  test_fill_log_no_fragmentation();
  test_fill_circular_log_no_fragmentation();
  test_fill_log_with_straddling_records();
  test_fill_circular_log_with_straddling_records();
  test_small_log();
  test_multiple_writers();
  test_multiple_writers_circular_log();
  test_performance();
  return 0;
}
