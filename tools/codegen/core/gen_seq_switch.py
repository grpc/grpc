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

for n in range(1, 10):
    print "template <%s> class Seq<%s> {" % (typenames(n), fs(n))
    print " private:"
    print "  char state_ = 0;"
    print "  struct State0 {"
    print "    State0(F0&& f0, F1&& f1) : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}"
    print "    State0(State0&& other) : f(std::move(other.f)), next(std::move(other.next)) {}"
    print "    State0(const State0& other) : f(other.f), next(other.next) {}"
    print "    ~State0() = delete;"
    print "    using F = F0;"
    print "    [[no_unique_address]] F0 f;"
    print "    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;"
    print "    using Next = adaptor_detail::Factory<FResult, F1>;"
    print "    [[no_unique_address]] Next next;"
    print "  };"
    for i in range(1, n):
        print "  struct State%d {" % i
        print "    State%d(%s) : next(std::forward<F%d>(f%d)) { new (&prior) State%d(%s); }" % (i, fs_refdecls(i+1), i+1, i+1, i-1, forwards(i))
        print "    State%d(State%d&& other) : prior(std::move(other.prior)), next(std::move(other.next)) {}" % (i, i)
        print "    State%d(const State%d& other) : prior(other.prior), next(other.next) {}" % (i, i)
        print "    ~State%d() = delete;" % i
        print "    using F = typename State%d::Next::Promise;" % (i-1)
        print "    union {"
        print "      [[no_unique_address]] State%d prior;" % (i-1)
        print "      [[no_unique_address]] F f;"
        print "    };"
        print "    using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;"
        print "    using Next = adaptor_detail::Factory<FResult, F%d>;" % (i+1)
        print "    [[no_unique_address]] Next next;"
        print "  };"
    print "  using FLast = typename State%d::Next::Promise;" % (n-1)
    print "  union {"
    print "    [[no_unique_address]] State%d prior_;" % (n-1)
    print "    [[no_unique_address]] FLast f_;"
    print "  };"
    print " public:"
    print "  Seq(%s) : prior_(%s) {}" % (fs_decls(n), moves(n))
    print "  Seq& operator=(const Seq&) = delete;"
    print "  Seq(const Seq& other) {"
    print "    assert(other.state_ == 0);"
    print "    new (&prior_) State%d(other.prior_);" % (n-1)
    print "  }"
    print "  Seq(Seq&& other) {"
    print "    assert(other.state_ == 0);"
    print "    new (&prior_) State%d(std::move(other.prior_));" % (n-1)
    print "  }"
    print "  ~Seq() {"
    print "    switch (state_) {"
    for i in range(0, n):
        print "     case %d:" % i
        prior = "prior_" + ".prior" * (n - i - 1)
        print "      Destruct(&%s.f);" % (prior)
        print "      goto fin%d;" % i
    print "     case %d:" % n
    print "      Destruct(&f_);"
    print "      return;"
    print "    }"
    for i in range(0, n):
        print "  fin%d:" % i
        prior = "prior_" + ".prior" * (n - i - 1)
        print "    Destruct(&%s.next);" % prior
    print "  }"
    print "  decltype(std::declval<typename State%d::Next::Promise>()()) operator()() {" % (n-1)
    print "    switch (state_) {"
    for i in range(0, n):
        print "     case %d: {" % i
        prior = "prior_" + ".prior" * (n - i - 1)
        next_f = "prior_" + ".prior" * (n - i - 2) + ".f" if i != n - 1 else "f_"
        print "      auto r = %s.f();" % prior
        print "      auto* p = r.get_ready();"
        print "      if (p == nullptr) break;"
        print "      Destruct(&%s.f);" % prior
        print "      auto n = %s.next.Once(std::move(*p));" % prior
        print "      Destruct(&%s.next);" % prior
        print "      new (&%s) typename State%d::Next::Promise(std::move(n));" % (next_f, i)
        print "      state_ = %d;" % (i+1)
        print "     }"
    print "     case %d:" % n
    print "      return f_();"
    print "    }"
    print "    return kPending;"
    print "  }"
    print "};"
