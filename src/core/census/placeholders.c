/*
 *
 * Copyright 2016, Google Inc.
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

const census_aggregation *census_view_aggregrations(const census_view *view) {
  (void)view;
  abort();
}

census_view *census_view_create(uint32_t metric_id, const census_context *tags,
                                const census_aggregation *aggregations,
                                size_t naggregations) {
  (void)metric_id;
  (void)tags;
  (void)aggregations;
  (void)naggregations;
  abort();
}

const census_context *census_view_tags(const census_view *view) {
  (void)view;
  abort();
}

void census_view_delete(census_view *view) {
  (void)view;
  abort();
}

const census_view_data *census_view_get_data(const census_view *view) {
  (void)view;
  abort();
}

size_t census_view_metric(const census_view *view) {
  (void)view;
  abort();
}

size_t census_view_naggregations(const census_view *view) {
  (void)view;
  abort();
}

void census_view_reset(census_view *view) {
  (void)view;
  abort();
}
