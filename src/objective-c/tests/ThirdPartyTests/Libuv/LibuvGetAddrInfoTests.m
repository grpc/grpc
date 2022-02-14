/*
 *
 * Copyright 2021 gRPC authors.
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

#import <XCTest/XCTest.h>

#import <stdlib.h>
#import <uv.h>

#define ASSERT(x) XCTAssertTrue(x)
#define ASSERT_NULL(x) XCTAssertTrue(x == NULL)
#define ASSERT_NOT_NULL(x) XCTAssertFalse(x == NULL)
#define CONCURRENT_COUNT 10

static const char* name = "localhost";

static int getaddrinfo_cbs = 0;

/* data used for running multiple calls concurrently */
static uv_getaddrinfo_t* getaddrinfo_handle;
static uv_getaddrinfo_t getaddrinfo_handles[CONCURRENT_COUNT];
static int callback_counts[CONCURRENT_COUNT];
static int fail_cb_called;

static void getaddrinfo_fail_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
  ASSERT(fail_cb_called == 0);
  ASSERT(status < 0);
  ASSERT_NULL(res);
  uv_freeaddrinfo(res); /* Should not crash. */
  fail_cb_called++;
}

static void getaddrinfo_basic_cb(uv_getaddrinfo_t* handle, int status, struct addrinfo* res) {
  ASSERT(handle == getaddrinfo_handle);
  getaddrinfo_cbs++;
  free(handle);
  uv_freeaddrinfo(res);
}

static void getaddrinfo_cuncurrent_cb(uv_getaddrinfo_t* handle, int status, struct addrinfo* res) {
  int i;
  int* data = (int*)handle->data;

  for (i = 0; i < CONCURRENT_COUNT; i++) {
    if (&getaddrinfo_handles[i] == handle) {
      ASSERT(i == *data);
      callback_counts[i]++;
      break;
    }
  }
  ASSERT(i < CONCURRENT_COUNT);
  free(data);
  uv_freeaddrinfo(res);
  getaddrinfo_cbs++;
}

@interface LibuvGetAddrInfoTests : XCTestCase

@end

@implementation LibuvGetAddrInfoTests

- (void)testGetAddrInfoFail {
  uv_getaddrinfo_t req;

  ASSERT(UV_EINVAL ==
         uv_getaddrinfo(uv_default_loop(), &req, (uv_getaddrinfo_cb)abort, NULL, NULL, NULL));

  /* Use a FQDN by ending in a period */
  ASSERT(0 == uv_getaddrinfo(uv_default_loop(), &req, getaddrinfo_fail_cb, "example.invalid.", NULL,
                             NULL));
  ASSERT(0 == uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT(fail_cb_called == 1);
}

- (void)testGetAddrInfoFailSync {
  uv_getaddrinfo_t req;

  /* Use a FQDN by ending in a period */
  ASSERT(0 > uv_getaddrinfo(uv_default_loop(), &req, NULL, "example.invalid.", NULL, NULL));
  uv_freeaddrinfo(req.addrinfo);
}

- (void)testGetAddrInfoBasic {
  int r;
  getaddrinfo_handle = (uv_getaddrinfo_t*)malloc(sizeof(uv_getaddrinfo_t));

  r = uv_getaddrinfo(uv_default_loop(), getaddrinfo_handle, &getaddrinfo_basic_cb, name, NULL,
                     NULL);
  ASSERT(r == 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(getaddrinfo_cbs == 1);
}

- (void)testGetAddrInfoBasicSync {
  uv_getaddrinfo_t req;

  ASSERT(0 == uv_getaddrinfo(uv_default_loop(), &req, NULL, name, NULL, NULL));
  uv_freeaddrinfo(req.addrinfo);
}

- (void)testGetAddrInfoConcurrent {
  int i, r;
  int* data;

  for (i = 0; i < CONCURRENT_COUNT; i++) {
    callback_counts[i] = 0;

    data = (int*)malloc(sizeof(int));
    ASSERT_NOT_NULL(data);
    *data = i;
    getaddrinfo_handles[i].data = data;

    r = uv_getaddrinfo(uv_default_loop(), &getaddrinfo_handles[i], &getaddrinfo_cuncurrent_cb, name,
                       NULL, NULL);
    ASSERT(r == 0);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  for (i = 0; i < CONCURRENT_COUNT; i++) {
    ASSERT(callback_counts[i] == 1);
  }
}

@end
