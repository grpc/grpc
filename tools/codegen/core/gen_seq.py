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

import sys

from mako.template import Template

seq_state = Template(
    """
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
  GPR_NO_UNIQUE_ADDRESS DebugLocation whence;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION SeqState(P&& p,
           ${",".join(f"F{i}&& f{i}" for i in range(0,n-1))},
           DebugLocation whence) noexcept: whence(whence)  {
    Construct(&${"prior."*(n-1)}current_promise, std::forward<P>(p));
% for i in range(0,n-1):
    Construct(&${"prior."*(n-1-i)}next_factory, std::forward<F${i}>(f${i}));
% endfor
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION ~SeqState() {
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
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION SeqState(const SeqState& other) noexcept = delete;
  SeqState& operator=(const SeqState& other) = delete;
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION SeqState(SeqState&& other) noexcept : state(other.state), whence(other.whence) {
    DCHECK(state == State::kState0);
    Construct(&${"prior."*(n-1)}current_promise,
              std::move(other.${"prior."*(n-1)}current_promise));
% for i in range(0,n-1):
    Construct(&${"prior."*(n-1-i)}next_factory,
              std::move(other.${"prior."*(n-1-i)}next_factory));
% endfor
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION SeqState& operator=(SeqState&& other) = delete;
  Json ToJson(absl::string_view type_name) const {
    Json::Object obj;
#ifndef NDEBUG
    obj["source_location"] = Json::FromString(absl::StrCat(whence.file(), ":", whence.line()));
#endif
    obj["seq_type"] = Json::FromString(std::string(type_name));
    Json::Array steps;
    steps.reserve(${n});
    Json::Object step0;
    step0["type"] = Json::FromString(std::string(TypeName<P>()));
    if (state == State::kState0) {
      step0["state"] = PromiseAsJson(${"prior."*(n-1)}current_promise);
    }
    steps.emplace_back(Json::FromObject(step0));
% for i in range(1,n):
    Json::Object step${i};
    step${i}["type"] = Json::FromString(std::string(TypeName<F${i-1}>()));
    if (state == State::kState${i}) {
      step${i}["state"] = PromiseAsJson(${"prior."*(n-1-i)}current_promise);
    }
    steps.emplace_back(Json::FromObject(step${i}));
% endfor
    obj["steps"] = Json::FromArray(steps);
    return Json::FromObject(obj);
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Poll<Result> PollOnce() {
    switch (state) {
% for i in range(0,n-1):
      case State::kState${i}: {
        GRPC_TRACE_LOG(promise_primitives, INFO).AtLocation(whence.file(), whence.line())
                << "seq[" << this << "]: begin poll step ${i+1}/${n}";
        auto result = ${"prior."*(n-1-i)}current_promise();
        PromiseResult${i}* p = result.value_if_ready();
        GRPC_TRACE_LOG(promise_primitives, INFO).AtLocation(whence.file(), whence.line())
                << "seq[" << this << "]: poll step ${i+1}/${n} gets "
                << (p != nullptr
                    ? (PromiseResultTraits${i}::IsOk(*p)
                      ? "ready"
                      : absl::StrCat("early-error:", PromiseResultTraits${i}::ErrorString(*p)).c_str())
                    : "pending");
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
      [[fallthrough]];
% endfor
      default:
      case State::kState${n-1}: {
        GRPC_TRACE_LOG(promise_primitives, INFO).AtLocation(whence.file(), whence.line())
                << "seq[" << this << "]: begin poll step ${n}/${n}";
        auto result = current_promise();
        GRPC_TRACE_LOG(promise_primitives, INFO).AtLocation(whence.file(), whence.line())
                << "seq[" << this << "]: poll step ${n}/${n} gets "
                << (result.ready()? "ready" : "pending");
        auto* p = result.value_if_ready();
        if (p == nullptr) return Pending{};
        return Result(std::move(*p));
      }
    }
  }
};"""
)

front_matter = """
#ifndef GRPC_SRC_CORE_LIB_PROMISE_DETAIL_SEQ_STATE_H
#define GRPC_SRC_CORE_LIB_PROMISE_DETAIL_SEQ_STATE_H

// This file is generated by tools/codegen/core/gen_seq.py

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/base/attributes.h"
#include "absl/strings/str_cat.h"

#include "src/core/lib/debug/trace.h"
#include "src/core/util/construct_destruct.h"
#include "src/core/util/debug_location.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/util/json/json.h"
#include "src/core/lib/promise/promise.h"

// A sequence under some traits for some set of callables P, Fs.
// P should be a promise-like object that yields a value.
// Fs... should be promise-factory-like objects that take the value from the
// previous step and yield a promise. Note that most of the machinery in
// PromiseFactory exists to make it possible for those promise-factory-like
// objects to be anything that's convenient.
// Traits defines how we move from one step to the next. Traits sets up the
// wrapping and escape handling for the sequence.
// Promises return wrapped values that the trait can inspect and unwrap before
// passing them to the next element of the sequence. The trait can
// also interpret a wrapped value as an escape value, which terminates
// evaluation of the sequence immediately yielding a result. Traits for type T
// have the members:
//  * type UnwrappedType - the type after removing wrapping from T (i.e. for
//    TrySeq, T=StatusOr<U> yields UnwrappedType=U).
//  * type WrappedType - the type after adding wrapping if it doesn't already
//    exist (i.e. for TrySeq if T is not Status/StatusOr/void, then
//    WrappedType=StatusOr<T>; if T is Status then WrappedType=Status (it's
//    already wrapped!))
//  * template <typename Next> void CallFactory(Next* next_factory, T&& value) -
//    call promise factory next_factory with the result of unwrapping value, and
//    return the resulting promise.
//  * template <typename Result, typename RunNext> Poll<Result>
//    CheckResultAndRunNext(T prior, RunNext run_next) - examine the value of
//    prior, and decide to escape or continue. If escaping, return the final
//    sequence value of type Poll<Result>. If continuing, return the value of
//    run_next(std::move(prior)).
//
// A state contains the current promise, and the promise factory to turn the
// result of the current promise into the next state's promise. We play a shell
// game such that the prior state and our current promise are kept in a union,
// and the next promise factory is kept alongside in the state struct.
// Recursively this guarantees that the next functions get initialized once, and
// destroyed once, and don't need to be moved around in between, which avoids a
// potential O(n**2) loop of next factory moves had we used a variant of states
// here. The very first state does not have a prior state, and so that state has
// a partial specialization below. The final state does not have a next state;
// that state is inlined in BasicSeq since that was simpler to type.

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
    for n in range(2, 14):
        print(seq_state.render(n=n), file=f)
    print(end_matter, file=f)
