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

template <template<typename> class Traits, typename P, ${",".join(f"typename F{i}" for i in range(0,n-1))}>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto SeqMap(P&& p, ${",".join(f"F{i}&& f{i}" for i in range(0,n-1))}) {
using Types = SeqStateTypes<Traits, P, ${",".join(f"F{i}" for i in range(0,n-1))}>;
% for i in range(0,n-1):
using PromiseResult${i} = typename Types::PromiseResult${i};
% endfor
% for i in range(0,n):
using PromiseResultTraits${i} = typename Types::PromiseResultTraits${i};
% endfor
% for i in range(0,n-1):
using NextFactory${i} = typename Types::NextFactory${i};
% endfor
using Result = typename PromiseResultTraits${n-1}::WrappedType;
    return ::grpc_core::Map(
        std::forward<P>(p),
        [${",".join(f"f{i} = NextFactory{i}(std::forward<F{i}>(f{i}))" for i in range(0,n-1))}]
        (PromiseResult0 r0) mutable {
% for i in range(0, n-2):
            if (!PromiseResultTraits${i}::IsOk(r${i})) {
                return PromiseResultTraits${i}::template ReturnValue<Result>(std::move(r${i}));
            }
            PromiseResult${i+1} r${i+1} =
                PromiseResultTraits${i}::CallFactoryThenPromise(&f${i}, std::move(r${i}));
% endfor
            if (!PromiseResultTraits${n-2}::IsOk(r${n-2})) {
                return PromiseResultTraits${n-2}::template ReturnValue<Result>(std::move(r${n-2}));
            }
            return Result(PromiseResultTraits${n-2}::CallFactoryThenPromise(&f${n-2}, std::move(r${n-2})));
        }
    );
}
