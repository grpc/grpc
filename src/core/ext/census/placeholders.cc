/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/census.h>

#include <grpc/support/log.h>

/* Placeholders for the pending APIs */

int census_get_trace_record(census_trace_record *trace_record) {
  (void)trace_record;
  abort();
}

void census_record_values(census_context *context, census_value *values,
                          size_t nvalues) {
  (void)context;
  (void)values;
  (void)nvalues;
  abort();
}

void census_set_rpc_client_peer(census_context *context, const char *peer) {
  (void)context;
  (void)peer;
  abort();
}

void census_trace_scan_end() { abort(); }

int census_trace_scan_start(int consume) {
  (void)consume;
  abort();
}
