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

#include "src/core/ext/census/census_log.h"
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

/* Fills in 'record' of size 'size'. Each byte in record is filled in with the
   same value. The value is extracted from 'record' pointer. */
static void write_record(char *record, size_t size) {
  char data = (uintptr_t)record % 255;
  memset(record, data, size);
}

/* Reads fixed size records. Returns the number of records read in
   'num_records'. */
static void read_records(size_t record_size, const char *buffer,
                         size_t buffer_size, int32_t *num_records) {
  int32_t ix;
  GPR_ASSERT(buffer_size >= record_size);
  GPR_ASSERT(buffer_size % record_size == 0);
  *num_records = buffer_size / record_size;
  for (ix = 0; ix < *num_records; ++ix) {
    size_t jx;
    const char *record = buffer + (record_size * ix);
    char data = (uintptr_t)record % 255;
    for (jx = 0; jx < record_size; ++jx) {
      GPR_ASSERT(data == record[jx]);
    }
  }
}

/* Tries to write the specified number of records. Stops when the log gets
   full. Returns the number of records written. Spins for random
   number of times, up to 'max_spin_count', between writes. */
static size_t write_records_to_log(int writer_id, int32_t record_size,
                                   int32_t num_records,
                                   int32_t max_spin_count) {
  int32_t ix;
  int counter = 0;
  for (ix = 0; ix < num_records; ++ix) {
    int32_t jx;
    int32_t spin_count = max_spin_count ? rand() % max_spin_count : 0;
    char *record;
    if (counter++ == num_records / 10) {
      printf("   Writer %d: %d out of %d written\n", writer_id, ix,
             num_records);
      counter = 0;
    }
    record = (char *)(census_log_start_write(record_size));
    if (record == NULL) {
      return ix;
    }
    write_record(record, record_size);
    census_log_end_write(record, record_size);
    for (jx = 0; jx < spin_count; ++jx) {
      GPR_ASSERT(jx >= 0);
    }
  }
  return num_records;
}

/* Performs a single read iteration. Returns the number of records read. */
static size_t perform_read_iteration(size_t record_size) {
  const void *read_buffer = NULL;
  size_t bytes_available;
  size_t records_read = 0;
  census_log_init_reader();
  while ((read_buffer = census_log_read_next(&bytes_available))) {
    int32_t num_records = 0;
    read_records(record_size, (const char *)read_buffer, bytes_available,
                 &num_records);
    records_read += num_records;
  }
  return records_read;
}

/* Asserts that the log is empty. */
static void assert_log_empty(void) {
  size_t bytes_available;
  census_log_init_reader();
  GPR_ASSERT(census_log_read_next(&bytes_available) == NULL);
}

/* Given log size and record size, computes the minimum usable space. */
static int32_t min_usable_space(size_t log_size, size_t record_size) {
  int32_t usable_space;
  int32_t num_blocks =
      GPR_MAX(log_size / CENSUS_LOG_MAX_RECORD_SIZE, gpr_cpu_num_cores());
  int32_t waste_per_block = CENSUS_LOG_MAX_RECORD_SIZE % record_size;
  /* In the worst case, all except one core-local block is full. */
  int32_t num_full_blocks = num_blocks - 1;
  usable_space = (int32_t)log_size -
                 (num_full_blocks * CENSUS_LOG_MAX_RECORD_SIZE) -
                 ((num_blocks - num_full_blocks) * waste_per_block);
  GPR_ASSERT(usable_space > 0);
  return usable_space;
}

/* Fills the log and verifies data. If 'no fragmentation' is true, records
   are sized such that CENSUS_LOG_2_MAX_RECORD_SIZE is a multiple of record
   size. If not a circular log, verifies that the number of records written
   match the number of records read. */
static void fill_log(size_t log_size, int no_fragmentation, int circular_log) {
  int size;
  int32_t records_written;
  int32_t usable_space;
  int32_t records_read;
  if (no_fragmentation) {
    int log2size = rand() % (CENSUS_LOG_2_MAX_RECORD_SIZE + 1);
    size = (1 << log2size);
  } else {
    while (1) {
      size = 1 + (rand() % CENSUS_LOG_MAX_RECORD_SIZE);
      if (CENSUS_LOG_MAX_RECORD_SIZE % size) {
        break;
      }
    }
  }
  printf("   Fill record size: %d\n", size);
  records_written = write_records_to_log(
      0 /* writer id */, size, (log_size / size) * 2, 0 /* spin count */);
  usable_space = min_usable_space(log_size, size);
  GPR_ASSERT(records_written * size >= usable_space);
  records_read = perform_read_iteration(size);
  if (!circular_log) {
    GPR_ASSERT(records_written == records_read);
  }
  assert_log_empty();
}

/* Structure to pass args to writer_thread */
typedef struct writer_thread_args {
  /* Index of this thread in the writers vector. */
  int index;
  /* Record size. */
  size_t record_size;
  /* Number of records to write. */
  int32_t num_records;
  /* Used to signal when writer is complete */
  gpr_cv *done;
  gpr_mu *mu;
  int *count;
} writer_thread_args;

/* Writes the given number of records of random size (up to kMaxRecordSize) and
   random data to the specified log. */
static void writer_thread(void *arg) {
  writer_thread_args *args = (writer_thread_args *)arg;
  /* Maximum number of times to spin between writes. */
  static const int32_t MAX_SPIN_COUNT = 50;
  int records_written = 0;
  printf("   Writer: %d\n", args->index);
  while (records_written < args->num_records) {
    records_written += write_records_to_log(args->index, args->record_size,
                                            args->num_records - records_written,
                                            MAX_SPIN_COUNT);
    if (records_written < args->num_records) {
      /* Ran out of log space. Sleep for a bit and let the reader catch up.
         This should never happen for circular logs. */
      printf("   Writer stalled due to out-of-space: %d out of %d written\n",
             records_written, args->num_records);
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(10));
    }
  }
  /* Done. Decrement count and signal. */
  gpr_mu_lock(args->mu);
  (*args->count)--;
  gpr_cv_broadcast(args->done);
  printf("   Writer done: %d\n", args->index);
  gpr_mu_unlock(args->mu);
}

/* struct to pass args to reader_thread */
typedef struct reader_thread_args {
  /* Record size. */
  size_t record_size;
  /* Interval between read iterations. */
  int32_t read_iteration_interval_in_msec;
  /* Total number of records. */
  int32_t total_records;
  /* Signalled when reader should stop. */
  gpr_cv stop;
  int stop_flag;
  /* Used to signal when reader has finished */
  gpr_cv *done;
  gpr_mu *mu;
  int running;
} reader_thread_args;

/* Reads and verifies the specified number of records. Reader can also be
   stopped via gpr_cv_signal(&args->stop). Sleeps for 'read_interval_in_msec'
   between read iterations. */
static void reader_thread(void *arg) {
  int32_t records_read = 0;
  reader_thread_args *args = (reader_thread_args *)arg;
  int32_t num_iterations = 0;
  gpr_timespec interval;
  int counter = 0;
  printf("   Reader starting\n");
  interval = gpr_time_from_micros(
      (int64_t)args->read_iteration_interval_in_msec * 1000, GPR_TIMESPAN);
  gpr_mu_lock(args->mu);
  while (!args->stop_flag && records_read < args->total_records) {
    gpr_cv_wait(&args->stop, args->mu, interval);
    if (!args->stop_flag) {
      records_read += perform_read_iteration(args->record_size);
      GPR_ASSERT(records_read <= args->total_records);
      if (counter++ == 100000) {
        printf("   Reader: %d out of %d read\n", records_read,
               args->total_records);
        counter = 0;
      }
      ++num_iterations;
    }
  }
  /* Done */
  args->running = 0;
  gpr_cv_broadcast(args->done);
  printf("   Reader: records: %d, iterations: %d\n", records_read,
         num_iterations);
  gpr_mu_unlock(args->mu);
}

/* Creates NUM_WRITERS writers where each writer writes NUM_RECORDS_PER_WRITER
   records. Also, starts a reader that iterates over and reads blocks every
   READ_ITERATION_INTERVAL_IN_MSEC. */
/* Number of writers. */
#define NUM_WRITERS 5
static void multiple_writers_single_reader(int circular_log) {
  /* Sleep interval between read iterations. */
  static const int32_t READ_ITERATION_INTERVAL_IN_MSEC = 10;
  /* Number of records written by each writer. */
  static const int32_t NUM_RECORDS_PER_WRITER = 10 * 1024 * 1024;
  /* Maximum record size. */
  static const size_t MAX_RECORD_SIZE = 10;
  int ix;
  gpr_thd_id id;
  gpr_cv writers_done;
  int writers_count = NUM_WRITERS;
  gpr_mu writers_mu; /* protects writers_done and writers_count */
  writer_thread_args writers[NUM_WRITERS];
  gpr_cv reader_done;
  gpr_mu reader_mu; /* protects reader_done and reader.running */
  reader_thread_args reader;
  int32_t record_size = 1 + rand() % MAX_RECORD_SIZE;
  printf("   Record size: %d\n", record_size);
  /* Create and start writers. */
  gpr_cv_init(&writers_done);
  gpr_mu_init(&writers_mu);
  for (ix = 0; ix < NUM_WRITERS; ++ix) {
    writers[ix].index = ix;
    writers[ix].record_size = record_size;
    writers[ix].num_records = NUM_RECORDS_PER_WRITER;
    writers[ix].done = &writers_done;
    writers[ix].count = &writers_count;
    writers[ix].mu = &writers_mu;
    gpr_thd_new(&id, &writer_thread, &writers[ix], NULL);
  }
  /* Start reader. */
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
  /* Wait for writers to finish. */
  gpr_mu_lock(&writers_mu);
  while (writers_count != 0) {
    gpr_cv_wait(&writers_done, &writers_mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&writers_mu);
  gpr_mu_destroy(&writers_mu);
  gpr_cv_destroy(&writers_done);
  gpr_mu_lock(&reader_mu);
  if (circular_log) {
    /* Stop reader. */
    reader.stop_flag = 1;
    gpr_cv_signal(&reader.stop);
  }
  /* wait for reader to finish */
  while (reader.running) {
    gpr_cv_wait(&reader_done, &reader_mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  if (circular_log) {
    /* Assert that there were no out-of-space errors. */
    GPR_ASSERT(0 == census_log_out_of_space_count());
  }
  gpr_mu_unlock(&reader_mu);
  gpr_mu_destroy(&reader_mu);
  gpr_cv_destroy(&reader_done);
  printf("   Reader: finished\n");
}

/* Log sizes to use for all tests. */
#define LOG_SIZE_IN_MB 1
#define LOG_SIZE_IN_BYTES (LOG_SIZE_IN_MB << 20)

static void setup_test(int circular_log) {
  census_log_initialize(LOG_SIZE_IN_MB, circular_log);
  GPR_ASSERT(census_log_remaining_space() == LOG_SIZE_IN_BYTES);
}

/* Attempts to create a record of invalid size (size >
   CENSUS_LOG_MAX_RECORD_SIZE). */
void test_invalid_record_size(void) {
  static const size_t INVALID_SIZE = CENSUS_LOG_MAX_RECORD_SIZE + 1;
  static const size_t VALID_SIZE = 1;
  void *record;
  printf("Starting test: invalid record size\n");
  setup_test(0);
  record = census_log_start_write(INVALID_SIZE);
  GPR_ASSERT(record == NULL);
  /* Now try writing a valid record. */
  record = census_log_start_write(VALID_SIZE);
  GPR_ASSERT(record != NULL);
  census_log_end_write(record, VALID_SIZE);
  /* Verifies that available space went down by one block. In theory, this
     check can fail if the thread is context switched to a new CPU during the
     start_write execution (multiple blocks get allocated), but this has not
     been observed in practice. */
  GPR_ASSERT(LOG_SIZE_IN_BYTES - CENSUS_LOG_MAX_RECORD_SIZE ==
             census_log_remaining_space());
  census_log_shutdown();
}

/* Tests end_write() with a different size than what was specified in
   start_write(). */
void test_end_write_with_different_size(void) {
  static const size_t START_WRITE_SIZE = 10;
  static const size_t END_WRITE_SIZE = 7;
  void *record_written;
  const void *record_read;
  size_t bytes_available;
  printf("Starting test: end write with different size\n");
  setup_test(0);
  record_written = census_log_start_write(START_WRITE_SIZE);
  GPR_ASSERT(record_written != NULL);
  census_log_end_write(record_written, END_WRITE_SIZE);
  census_log_init_reader();
  record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(record_written == record_read);
  GPR_ASSERT(END_WRITE_SIZE == bytes_available);
  assert_log_empty();
  census_log_shutdown();
}

/* Verifies that pending records are not available via read_next(). */
void test_read_pending_record(void) {
  static const size_t PR_RECORD_SIZE = 1024;
  size_t bytes_available;
  const void *record_read;
  void *record_written;
  printf("Starting test: read pending record\n");
  setup_test(0);
  /* Start a write. */
  record_written = census_log_start_write(PR_RECORD_SIZE);
  GPR_ASSERT(record_written != NULL);
  /* As write is pending, read should fail. */
  census_log_init_reader();
  record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(record_read == NULL);
  /* A read followed by end_write() should succeed. */
  census_log_end_write(record_written, PR_RECORD_SIZE);
  census_log_init_reader();
  record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(record_written == record_read);
  GPR_ASSERT(PR_RECORD_SIZE == bytes_available);
  assert_log_empty();
  census_log_shutdown();
}

/* Tries reading beyond pending write. */
void test_read_beyond_pending_record(void) {
  /* Start a write. */
  uint32_t incomplete_record_size = 10;
  uint32_t complete_record_size = 20;
  size_t bytes_available;
  void *complete_record;
  const void *record_read;
  void *incomplete_record;
  printf("Starting test: read beyond pending record\n");
  setup_test(0);
  incomplete_record = census_log_start_write(incomplete_record_size);
  GPR_ASSERT(incomplete_record != NULL);
  complete_record = census_log_start_write(complete_record_size);
  GPR_ASSERT(complete_record != NULL);
  GPR_ASSERT(complete_record != incomplete_record);
  census_log_end_write(complete_record, complete_record_size);
  /* Now iterate over blocks to read completed records. */
  census_log_init_reader();
  record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(complete_record == record_read);
  GPR_ASSERT(complete_record_size == bytes_available);
  /* Complete first record. */
  census_log_end_write(incomplete_record, incomplete_record_size);
  /* Have read past the incomplete record, so read_next() should return NULL. */
  /* NB: this test also assumes our thread did not get switched to a different
     CPU between the two start_write calls */
  record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(record_read == NULL);
  /* Reset reader to get the newly completed record. */
  census_log_init_reader();
  record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(incomplete_record == record_read);
  GPR_ASSERT(incomplete_record_size == bytes_available);
  assert_log_empty();
  census_log_shutdown();
}

/* Tests scenario where block being read is detached from a core and put on the
   dirty list. */
void test_detached_while_reading(void) {
  static const size_t DWR_RECORD_SIZE = 10;
  size_t bytes_available;
  const void *record_read;
  void *record_written;
  uint32_t block_read = 0;
  printf("Starting test: detached while reading\n");
  setup_test(0);
  /* Start a write. */
  record_written = census_log_start_write(DWR_RECORD_SIZE);
  GPR_ASSERT(record_written != NULL);
  census_log_end_write(record_written, DWR_RECORD_SIZE);
  /* Read this record. */
  census_log_init_reader();
  record_read = census_log_read_next(&bytes_available);
  GPR_ASSERT(record_read != NULL);
  GPR_ASSERT(DWR_RECORD_SIZE == bytes_available);
  /* Now fill the log. This will move the block being read from core-local
     array to the dirty list. */
  while ((record_written = census_log_start_write(DWR_RECORD_SIZE))) {
    census_log_end_write(record_written, DWR_RECORD_SIZE);
  }

  /* In this iteration, read_next() should only traverse blocks in the
     core-local array. Therefore, we expect at most gpr_cpu_num_cores() more
     blocks. As log is full, if read_next() is traversing the dirty list, we
     will get more than gpr_cpu_num_cores() blocks. */
  while ((record_read = census_log_read_next(&bytes_available))) {
    ++block_read;
    GPR_ASSERT(block_read <= gpr_cpu_num_cores());
  }
  census_log_shutdown();
}

/* Fills non-circular log with records sized such that size is a multiple of
   CENSUS_LOG_MAX_RECORD_SIZE (no per-block fragmentation). */
void test_fill_log_no_fragmentation(void) {
  const int circular = 0;
  printf("Starting test: fill log no fragmentation\n");
  setup_test(circular);
  fill_log(LOG_SIZE_IN_BYTES, 1 /* no fragmentation */, circular);
  census_log_shutdown();
}

/* Fills circular log with records sized such that size is a multiple of
   CENSUS_LOG_MAX_RECORD_SIZE (no per-block fragmentation). */
void test_fill_circular_log_no_fragmentation(void) {
  const int circular = 1;
  printf("Starting test: fill circular log no fragmentation\n");
  setup_test(circular);
  fill_log(LOG_SIZE_IN_BYTES, 1 /* no fragmentation */, circular);
  census_log_shutdown();
}

/* Fills non-circular log with records that may straddle end of a block. */
void test_fill_log_with_straddling_records(void) {
  const int circular = 0;
  printf("Starting test: fill log with straddling records\n");
  setup_test(circular);
  fill_log(LOG_SIZE_IN_BYTES, 0 /* block straddling records */, circular);
  census_log_shutdown();
}

/* Fills circular log with records that may straddle end of a block. */
void test_fill_circular_log_with_straddling_records(void) {
  const int circular = 1;
  printf("Starting test: fill circular log with straddling records\n");
  setup_test(circular);
  fill_log(LOG_SIZE_IN_BYTES, 0 /* block straddling records */, circular);
  census_log_shutdown();
}

/* Tests scenario where multiple writers and a single reader are using a log
   that is configured to discard old records. */
void test_multiple_writers_circular_log(void) {
  const int circular = 1;
  printf("Starting test: multiple writers circular log\n");
  setup_test(circular);
  multiple_writers_single_reader(circular);
  census_log_shutdown();
}

/* Tests scenario where multiple writers and a single reader are using a log
   that is configured to discard old records. */
void test_multiple_writers(void) {
  const int circular = 0;
  printf("Starting test: multiple writers\n");
  setup_test(circular);
  multiple_writers_single_reader(circular);
  census_log_shutdown();
}

/* Repeat the straddling records and multiple writers tests with a small log. */
void test_small_log(void) {
  size_t log_size;
  const int circular = 0;
  printf("Starting test: small log\n");
  census_log_initialize(0, circular);
  log_size = census_log_remaining_space();
  GPR_ASSERT(log_size > 0);
  fill_log(log_size, 0, circular);
  census_log_shutdown();
  census_log_initialize(0, circular);
  multiple_writers_single_reader(circular);
  census_log_shutdown();
}

void test_performance(void) {
  int write_size = 1;
  for (; write_size < CENSUS_LOG_MAX_RECORD_SIZE; write_size *= 2) {
    gpr_timespec write_time;
    gpr_timespec start_time;
    double write_time_micro = 0.0;
    int nrecords = 0;
    setup_test(0);
    start_time = gpr_now(GPR_CLOCK_REALTIME);
    while (1) {
      void *record = census_log_start_write(write_size);
      if (record == NULL) {
        break;
      }
      census_log_end_write(record, write_size);
      nrecords++;
    }
    write_time = gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), start_time);
    write_time_micro = write_time.tv_sec * 1000000 + write_time.tv_nsec / 1000;
    census_log_shutdown();
    printf(
        "Wrote %d %d byte records in %.3g microseconds: %g records/us "
        "(%g ns/record), %g gigabytes/s\n",
        nrecords, write_size, write_time_micro, nrecords / write_time_micro,
        1000 * write_time_micro / nrecords,
        (write_size * nrecords) / write_time_micro / 1000);
  }
}
