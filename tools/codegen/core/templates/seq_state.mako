<%doc>
Copyright 2025 gRPC authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
</%doc>

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
using Types = SeqStateTypes<Traits, P, ${",".join(f"F{i}" for i in range(0,n-1))}>;
% for i in range(0,n):
using Promise${i} = typename Types::Promise${i};
using PromiseResult${i} = typename Types::PromiseResult${i};
using PromiseResultTraits${i} = typename Types::PromiseResultTraits${i};
% endfor
% for i in range(0,n-1):
using NextFactory${i} = typename Types::NextFactory${i};
% endfor
using Result = typename Types::Result;
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
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION SeqState(const SeqState& other) noexcept : state(other.state), whence(other.whence) {
    DCHECK(state == State::kState0);
    Construct(&${"prior."*(n-1)}current_promise,
            other.${"prior."*(n-1)}current_promise);
% for i in range(0,n-1):
    Construct(&${"prior."*(n-1-i)}next_factory,
              other.${"prior."*(n-1-i)}next_factory);
% endfor
  }
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
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Poll<Result> operator()() {
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
};
