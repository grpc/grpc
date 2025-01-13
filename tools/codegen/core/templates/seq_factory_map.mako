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

template <template<typename> class Traits, typename Arg, ${",".join(f"typename F{i}" for i in range(0,n-1))}>
auto SeqFactoryMap(${",".join(f"F{i}&& f{i}" for i in range(0,n-1))}) {
  if constexpr (!std::is_same_v<Arg, void>) {
    return [${",".join(f"f{i} = std::forward<F{i}>(f{i})" for i in range(0,n-1))}](Arg x) mutable {
      OncePromiseFactory<decltype(x), F0> next(std::move(f0));
      return SeqMap<Traits>(next.Make(std::move(x)), ${",".join(f"std::move(f{i})" for i in range(1,n-1))});
    };
  } else {
    return [${",".join(f"f{i} = std::forward<F{i}>(f{i})" for i in range(0,n-1))}]() mutable {
      OncePromiseFactory<void, F0> next(std::move(f0));
      return SeqMap<Traits>(next.Make(), ${",".join(f"std::move(f{i})" for i in range(1,n-1))});
    };
  }
}
