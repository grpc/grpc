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

#ifndef TEST_QPS_HISTOGRAM_H
#define TEST_QPS_HISTOGRAM_H

#include <grpc/support/histogram.h>

namespace grpc {
namespace testing {

class Histogram {
 public:
  Histogram() : impl_(gpr_histogram_create(0.01, 60e9)) {}
  ~Histogram() { gpr_histogram_destroy(impl_); }

  void Merge(Histogram* h) { gpr_histogram_merge(impl_, h->impl_); }
  void Add(double value) { gpr_histogram_add(impl_, value); }
  double Percentile(double pctile) { return gpr_histogram_percentile(impl_, pctile); }
  double Count() { return gpr_histogram_count(impl_); }
  void Swap(Histogram* other) { std::swap(impl_, other->impl_); }

 private:
  Histogram(const Histogram&);
  Histogram& operator=(const Histogram&);

  gpr_histogram* impl_;
};

}
}

#endif  /* TEST_QPS_HISTOGRAM_H */
