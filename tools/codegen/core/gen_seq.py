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

from mako.lookup import TemplateLookup
from mako.template import Template

files = {
  "test/core/promise/opt_seq_test.cc": ("opt_seq_test.mako", {"n":6}),
  "test/core/promise/opt_try_seq_test.cc": ("opt_try_seq_test.mako", {"n":6}),
  "src/core/lib/promise/detail/seq_state.h": ("seq_state.mako", {"max_steps":14}),
}

def gen_opt_seq_state(n, f):
    typename_fs = ", ".join(f"typename F{i}" for i in range(n - 1))
    args_fs = ", ".join(f"F{i}&& f{i}" for i in range(n - 1))
    fs = ", ".join(f"F{i}" for i in range(n - 1))
    fwd_fs = ", ".join(f"std::forward<F{i}>(f{i})" for i in range(n - 1))
    frrefs = ", ".join(f"F{i}&&" for i in range(n - 1))

    print(
        f"template <template <typename> class Traits, uint32_t kInstantBits, typename P, typename... Rs, {typename_fs}>",
        file=f,
    )
    print(
        f"GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto FoldMiddle(P&& p, std::tuple<Rs&&...>&& resolved, {args_fs}, DebugLocation whence) {{",
        file=f,
    )
    print(f"  static_assert((kInstantBits & {bin(1<<(n-2))}) == 0);", file=f)
    first = True
    for i in range(n - 2, 0, -1):
        mask = ((1 << i) - 1) << (n - 2 - i)
        not_mask = (1 << (n - 2)) - 1 - mask
        print(f"// i={i} mask={bin(mask)} not_mask={bin(not_mask)}", file=f)
        print(
            f"  {'else' if not first else ''} if constexpr ((kInstantBits & {bin(mask)}) == {bin(mask)}) {{",
            file=f,
        )
        first = False
        print(
            f"    using Arg = typename SeqStateTypes<Traits, P, Rs...>::LastPromiseResult;",
            file=f,
        )
        map = f"SeqFactoryMap<Traits, Arg>({','.join(f'std::forward<F{j}>(f{j})' for j in range(i+1))})"
        rest = [f"std::forward<F{j}>(f{j})" for j in range(i + 1, n - 1)] + [
            "whence"
        ]
        print(
            f"    static_assert(std::is_same_v<typename SeqStateTypes<Traits, P, Rs..., decltype({map})>::LastPromiseResult, typename SeqStateTypes<Traits, P, Rs..., {','.join(f'F{j}' for j in range(i+1))}>::LastPromiseResult>);",
            file=f,
        )
        print(
            f"    static_assert(std::is_same_v<typename SeqStateTypes<Traits, P, Rs..., decltype({map})>::Result, typename SeqStateTypes<Traits, P, Rs..., {','.join(f'F{j}' for j in range(i+1))}>::Result>);",
            file=f,
        )
        print(
            f"    return FoldMiddle<Traits, (kInstantBits & {bin(not_mask)})>(",
            file=f,
        )
        print(f"        std::forward<P>(p),", file=f)
        print(
            f"        std::tuple_cat(std::forward<std::tuple<Rs&&...>>(resolved), std::tuple<decltype({map})&&>({map})),",
            file=f,
        )
        print(f"        {','.join(rest)});", file=f)
        print("  }", file=f)
    if not first:
        print("  else {", file=f)
    print(f"    return FoldMiddle<Traits, 0>(", file=f)
    print(f"        std::forward<P>(p),", file=f)
    print(
        f"        std::tuple_cat(std::forward<std::tuple<Rs&&...>>(resolved), std::tuple<{frrefs}>({fwd_fs})), whence);",
        file=f,
    )
    if not first:
        print("  }", file=f)
    print("}", file=f)

    print(
        f"template <template <typename> class Traits, uint32_t kInstantBits, typename P, {typename_fs}>",
        file=f,
    )
    print(
        f"GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto FoldSeqStateImpl(P&& p, {args_fs}, DebugLocation whence) {{",
        file=f,
    )
    print(f"  using Types = SeqStateTypes<Traits, P, {fs}>;", file=f)
    masks = set()
    for i in range(n - 1, 0, -1):
        mask = (1 << i) - 1
        masks.add(mask)
        print(f"// i={i} mask={bin(mask)}", file=f)
        print(
            f"  {'else' if i != n-1 else ''} if constexpr ((kInstantBits & {bin(mask)}) == {bin(mask)}) {{",
            file=f,
        )
        if i == n - 1:
            left = "std::forward<P>(p)"
        else:
            left = f"FoldSeqStateImpl<Traits, (kInstantBits >> {i})>(std::forward<P>(p), {','.join(f'std::forward<F{j}>(f{j})' for j in range(0, n-1-i))}, whence)"
        print(
            f"    return WithResult<typename Types::Result>(SeqMap<Traits>({left}, {','.join(f'std::forward<F{j}>(f{j})' for j in range(n-1-i, n-1))}));",
            file=f,
        )
        print("  }", file=f)
    for i in range(n - 1, 0, -1):
        mask = ((1 << i) - 1) << (n - 1 - i)
        if mask in masks:
            continue
        masks.add(mask)
        not_mask = (1 << (n - 1)) - 1 - mask
        print(f"// i={i} mask={bin(mask)} not_mask={bin(not_mask)}", file=f)
        print(
            f"  else if constexpr ((kInstantBits & {bin(mask)}) == {bin(mask)}) {{",
            file=f,
        )
        map = f"SeqMap<Traits>(std::forward<P>(p), {','.join(f'std::forward<F{j}>(f{j})' for j in range(i))})"
        print(
            f"static_assert(std::is_same_v<typename PromiseLike<decltype({map})>::Result, typename SeqStateTypes<Traits, P, {','.join(f'F{j}' for j in range(i))}>::Result>);",
            file=f,
        )
        if not_mask:
            rest = f"{','.join(f'std::forward<F{j}>(f{j})' for j in range(i, n-1))}"
            print(
                f"    return WithResult<typename Types::Result>(FoldSeqStateImpl<Traits, (kInstantBits & {bin(not_mask)})>({map}, {rest}, whence));",
                file=f,
            )
        else:
            print(f"   return {map};", file=f)
        print("  }", file=f)
    print("else {", file=f)
    print(f"  static_assert((kInstantBits & {bin(1<<(n-2))}) == 0);", file=f)
    print(
        f"  return FoldMiddle<Traits, kInstantBits>(std::forward<P>(p), std::tuple<>(), {fwd_fs}, whence);",
        file=f,
    )
    print("}", file=f)
    print("}", file=f)

    print(
        f"template <template <typename> class Traits, typename P, {typename_fs}>",
        file=f,
    )
    print(
        f"GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto FoldSeqState(P&& p, {args_fs}, DebugLocation whence) {{",
        file=f,
    )
    print(f"  using Types = SeqStateTypes<Traits, P, {fs}>;", file=f)
    instant_bits = " | ".join(
        f"(Types::NextFactory{i}::kInstantaneousPromise? {1<<(n-2-i)} : 0)"
        for i in range(0, n - 1)
    )
    print(f"  static constexpr uint32_t kInstantBits = {instant_bits};", file=f)
    print(
        f"  return WithResult<typename Types::Result>(FoldSeqStateImpl<Traits, kInstantBits>(std::forward<P>(p), {fwd_fs}, whence));",
        file=f,
    )
    print("}", file=f)

tmpl_lookup = TemplateLookup(directories=["tools/codegen/core/templates/"])
for filename, (template_name, args) in files.items():
    template = tmpl_lookup.get_template(template_name)
    with open(filename, "w") as f:
        print(template.render(**args), file=f)
