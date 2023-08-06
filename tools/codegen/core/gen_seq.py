#!/usr/bin/env python3

# Copyright 2023 gRPC authors.
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

from mako.template import Template
import sys

seq_state = Template("""
<%def name="decl(promise_name, i, n)">
using Promise${i} = ${promise_name};
% if i < n-1:
using NextFactory${i} = OncePromiseFactory<typename Promise${i}::Result, F${i}>;
${decl(f"typename NextFactory{i}::Promise", i+1, n)}
% endif
</%def>

<%def name="state(i, n)">
% if i == 0:
Promise0 current_promise;
NextFactory0 next_factory;
% elif i == n-1:
union {
    struct { ${state(i-1, n)} } prior;
    Promise${i} current_promise;
};
% else:
union {
    struct { ${state(i-1, n)} } prior;
    P${i} current_promise;
};
NextFactory${i} next_factory;
% endif
</%def>

template <template<typename> class Traits, typename P, ${",".join(f"typename F{i}" for i in range(0,n-1))}>
struct SeqState<Traits, P, ${",".join(f"F{i}" for i in range(0,n-1))}> {
<% name="PromiseLike<P>" %>
% for i in range(0,n-1):
using Promise${i} = ${name};
using PromiseResult${i} = typename Promise${i}::Result;
using PromiseResultTraits${i} = Traits<PromiseResult${i}>;
using NextFactory${i} = OncePromiseFactory<typename PromiseResultTraits${i}::UnwrappedType, F${i}>;
<% name=f"typename NextFactory{i}::Promise" %>\\
% endfor
using Promise${n-1} = ${name};
using PromiseResult${n-1} = typename Promise${n-1}::Result;
using PromiseResultTraits${n-1} = Traits<PromiseResult${n-1}>;
using Result = typename PromiseResultTraits${n-1}::WrappedType;
% if n == 1:
Promise0 current_promise;
% else:
%  for i in range(0,n-1):
struct Running${i} {
%   if i != 0:
union {
  GPR_NO_UNIQUE_ADDRESS Running${i-1} prior;
%   endif
  GPR_NO_UNIQUE_ADDRESS Promise${i} current_promise;
%   if i != 0:
};
%   endif
GPR_NO_UNIQUE_ADDRESS NextFactory${i} next_factory;
};
%  endfor
union {
    GPR_NO_UNIQUE_ADDRESS Running${n-2} prior;
    GPR_NO_UNIQUE_ADDRESS Promise${n-1} current_promise;
};
% endif
  enum class State : uint8_t { ${",".join(f"kState{i}" for i in range(0,n))} };
  GPR_NO_UNIQUE_ADDRESS State state = State::kState0;

  SeqState(P&& p, ${",".join(f"F{i}&& f{i}" for i in range(0,n-1))}) noexcept {
    Construct(&${"prior."*(n-1)}current_promise, std::forward<P>(p));
% for i in range(0,n-1):
    Construct(&${"prior."*(n-1-i)}next_factory, std::forward<F${i}>(f${i}));
% endfor
  }
  ~SeqState() {
    switch (state) {
% for i in range(0,n-1):
     case State::kState${i}:
      Destruct(&${"prior."*(n-1-i)}current_promise);
      goto tail${i};
% endfor
     case State::kState${n-1}:
      Destruct(&current_promise);
      return;
    }
% for i in range(0,n-1):
tail${i}:
    Destruct(&${"prior."*(n-1-i)}next_factory);
% endfor
  }
  SeqState(const SeqState& other) noexcept : state(other.state) {
    GPR_ASSERT(state == State::kState0);
    Construct(&${"prior."*(n-1-i)}current_promise,
            other.${"prior."*(n-1-i)}current_promise);
% for i in range(0,n-1):
    Construct(&${"prior."*(n-1-i)}next_factory,
              other.${"prior."*(n-1-i)}next_factory);
% endfor
  }
  SeqState& operator=(const SeqState& other) = delete;
  SeqState(SeqState&& other) noexcept : state(other.state) {
    switch (state) {
% for i in range(0,n-1):
     case State::kState${i}:
      Construct(&${"prior."*(n-1-i)}current_promise,
                std::move(other.${"prior."*(n-1-i)}current_promise));
      goto tail${i};
% endfor
     case State::kState${n-1}:
      Construct(&current_promise, std::move(other.current_promise));
      return;
    }
% for i in range(0,n-1):
tail${i}:
    Construct(&${"prior."*(n-1-i)}next_factory,
              std::move(other.${"prior."*(n-1-i)}next_factory));
% endfor
  }
  SeqState& operator=(SeqState&& other) = delete;
  Poll<Result> PollOnce() {
    switch (state) {
% for i in range(0,n-1):
      case State::kState${i}: {
        auto result = ${"prior."*(n-1-i)}current_promise();
        PromiseResult${i}* p = result.value_if_ready();
        if (p == nullptr) return Pending{};
        if (!PromiseResultTraits${i}::IsOk(*p)) {
            return PromiseResultTraits${i}::template ReturnValue<Result>(std::move(*p));
        }
        Destruct(&${"prior."*(n-1-i)}current_promise);
        auto next_promise = PromiseResultTraits${i}::CallFactory(&${"prior."*(n-1-i)}next_factory, std::move(*p));
        Destruct(&${"prior."*(n-1-i)}next_factory);
        Construct(&${"prior."*(n-2-i)}current_promise, std::move(next_promise));
        state = State::kState${i+1};
      }
      ABSL_FALLTHROUGH_INTENDED;
% endfor
      default:
      case State::kState${n-1}: {
        auto result = current_promise();
        auto* p = result.value_if_ready();
        if (p == nullptr) return Pending{};
        return Result(std::move(*p));
      }
    }
  }
};""")

front_matter = """
#ifndef GRPC_SRC_CORE_LIB_PROMISE_DETAIL_SEQ_STATE_H
#define GRPC_SRC_CORE_LIB_PROMISE_DETAIL_SEQ_STATE_H

// This file is generated by tools/codegen/core/gen_seq.py

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <utility>

#include "absl/base/attributes.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {
namespace promise_detail {
template <template<typename> class Traits, typename P, typename... Fs>
struct SeqState;
"""

end_matter = """
}  // namespace promise_detail
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_DETAIL_SEQ_STATE_H
"""

# utility: print a big comment block into a set of files
def put_banner(files, banner):
    for f in files:
        for line in banner:
            print("// %s" % line, file=f)
        print("", file=f)

with open(sys.argv[0]) as my_source:
    copyright = []
    for line in my_source:
        if line[0] != "#":
            break
    for line in my_source:
        if line[0] == "#":
            copyright.append(line)
            break
    for line in my_source:
        if line[0] != "#":
            break
        copyright.append(line)

copyright = [line[2:].rstrip() for line in copyright]

with open("src/core/lib/promise/detail/seq_state.h", "w") as f:
    put_banner([f], copyright)
    print(front_matter, file=f)
    for n in range(2, 10):
        print(seq_state.render(n=n), file=f)
    print(end_matter, file=f)