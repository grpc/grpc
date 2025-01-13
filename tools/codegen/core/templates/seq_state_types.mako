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
<%def name="decl(promise_name, i, n)">
using Promise${i} = ${promise_name};
% if i < n-1:
using NextFactory${i} = OncePromiseFactory<typename Promise${i}::Result, F${i}>;
${decl(f"typename NextFactory{i}::Promise", i+1, n)}
% endif
</%def>

template <template<typename> class Traits, typename P, ${",".join(f"typename F{i}" for i in range(0,n-1))}>
struct SeqStateTypes<Traits, P, ${",".join(f"F{i}" for i in range(0,n-1))}> {
<% name="PromiseLike<P>" %>
% for i in range(0,n-1):
using Promise${i} = ${name};
using PromiseResult${i} = typename Promise${i}::Result;
using PromiseResultTraits${i} = Traits<PromiseResult${i}>;
using NextFactory${i} = OncePromiseFactory<typename PromiseResultTraits${i}::UnwrappedType, F${i}>;
<% name=f"typename NextFactory{i}::Promise" %>
% endfor
using Promise${n-1} = ${name};
using PromiseResult${n-1} = typename Promise${n-1}::Result;
using PromiseResultTraits${n-1} = Traits<PromiseResult${n-1}>;
using Result = typename PromiseResultTraits${n-1}::WrappedType;
};
