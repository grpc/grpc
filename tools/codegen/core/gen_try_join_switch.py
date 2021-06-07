#!/usr/bin/env python2.7

# Copyright 2021 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

def fs(n):
    return ", ".join("F%d" % i for i in range(0, n+1))

def rs(n):
    return ", ".join("R%d" % i for i in range(0, n+1))

def fs_decls(n):
    return ", ".join("F%d f%d" % (i,i) for i in range(0, n+1))

def fs_refdecls(n):
    return ", ".join("F%d&& f%d" % (i,i) for i in range(0, n+1))

def moves(n):
    return ", ".join("std::move(f%d)" % i for i in range(0, n+1))

def forwards(n):
    return ", ".join("std::forward<F%d>(f%d)" % (i,i) for i in range(0, n+1))

def typenames(n):
    return ", ".join("typename F%d" % i for i in range(0, n+1))

for n in range(0, 32):
    if n < 8:
        state_size = 8
    elif n < 16:
        state_size = 16
    elif n < 32:
        state_size = 32
    else:
        state_size = 64
    print "template <%s> class TryJoin<%s> {" % (typenames(n), fs(n))
    print " private:"
    print "  [[no_unique_address]] uint%d_t state_ = 0;" % state_size
    for i in range(0, n+1):
        print "  using R%d = decltype(IntoResult(std::declval<F%d>()().get_ready()));" % (i,i)
        print "  union { [[no_unique_address]] F%d pending%d_; [[no_unique_address]] R%d ready%d_; };" % (i,i,i,i)
    print " public:"
    print "  TryJoin(%s) : %s {}" % (fs_decls(n), ", ".join("pending%d_(std::move(f%d))" % (i,i) for i in range(0, n+1)))
    print "  TryJoin& operator=(const TryJoin&) = delete;"
    print "  TryJoin(const TryJoin& other) {"
    print "    assert(other.state_ == 0);"
    for i in range(0, n+1):
        print "    Construct(&pending%d_, std::move(other.pending%d_));" % (i,i)
    print "  }"
    print "  TryJoin(TryJoin&& other) {"
    print "    assert(other.state_ == 0);"
    for i in range(0, n+1):
        print "    Construct(&pending%d_, std::move(other.pending%d_));" % (i,i)
    print "  }"
    print "  ~TryJoin() {"
    for i in range(0, n+1):
        print "    if (state_ & %d) Destruct(&ready%d_); else Destruct(&pending%d_);" % (1<<i,i,i)
    print "  }"
    print "  using Result = absl::StatusOr<std::tuple<%s>>;" % rs(n)
    print "  Poll<Result> operator()() {"
    for i in range(0, n+1):
        print "    if ((state_ & %d) == 0) {" % (1<<i)
        print "      auto r = pending%d_();" % i
        print "      if (auto* p = r.get_ready()) {"
        print "        if (p->ok()) { state_ |= %d; Destruct(&pending%d_); Construct(&ready%d_, IntoResult(p)); }" % (1<<i,i,i)
        print "        else { return ready(Result(IntoStatus(p))); }"
        print "      }"
        print "    }"
    print "    if (state_ != %d) return kPending;" % sum(1<<i for i in range(0,n+1))
    print "    return ready(Result(absl::in_place, %s));" % ", ".join("std::move(ready%d_)" % i for i in range(0,n+1))
    print "  }"
    print "};"
