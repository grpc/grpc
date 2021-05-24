
xxHash - Extremely fast hash algorithm
======================================

xxHash is an Extremely fast Hash algorithm, running at RAM speed limits.
It successfully completes the [SMHasher](https://code.google.com/p/smhasher/wiki/SMHasher) test suite
which evaluates collision, dispersion and randomness qualities of hash functions.
Code is highly portable, and hashes are identical across all platforms (little / big endian).

|Branch      |Status   |
|------------|---------|
|dev         | [![Build Status](https://travis-ci.org/Cyan4973/xxHash.svg?branch=dev)](https://travis-ci.org/Cyan4973/xxHash?branch=dev) |


Benchmarks
-------------------------

The reference system uses an Intel i7-9700K cpu, and runs Ubuntu x64 20.04.
The [open source benchmark program] is compiled with `clang` v10.0 using `-O3` flag.

| Hash Name     | Width | Bandwidth (GB/s) | Small Data Velocity | Quality | Comment |
| ---------     | ----- | ---------------- | ----- | --- | --- |
| __XXH3__ (SSE2) |  64 | 31.5 GB/s        | 133.1 | 10
| __XXH128__ (SSE2) | 128 | 29.6 GB/s      | 118.1 | 10
| _RAM sequential read_ | N/A | 28.0 GB/s  |   N/A | N/A | _for reference_
| City64        |    64 | 22.0 GB/s        |  76.6 | 10
| T1ha2         |    64 | 22.0 GB/s        |  99.0 |  9 | Slightly worse [collisions]
| City128       |   128 | 21.7 GB/s        |  57.7 | 10
| __XXH64__     |    64 | 19.4 GB/s        |  71.0 | 10
| SpookyHash    |    64 | 19.3 GB/s        |  53.2 | 10
| Mum           |    64 | 18.0 GB/s        |  67.0 |  9 | Slightly worse [collisions]
| __XXH32__     |    32 |  9.7 GB/s        |  71.9 | 10
| City32        |    32 |  9.1 GB/s        |  66.0 | 10
| Murmur3       |    32 |  3.9 GB/s        |  56.1 | 10
| SipHash       |    64 |  3.0 GB/s        |  43.2 | 10
| FNV64         |    64 |  1.2 GB/s        |  62.7 |  5 | Poor avalanche properties
| Blake2        |   256 |  1.1 GB/s        |   5.1 | 10 | Cryptographic
| SHA1          |   160 |  0.8 GB/s        |   5.6 | 10 | Cryptographic but broken
| MD5           |   128 |  0.6 GB/s        |   7.8 | 10 | Cryptographic but broken

[open source benchmark program]: https://github.com/Cyan4973/xxHash/tree/release/tests/bench
[collisions]: https://github.com/Cyan4973/xxHash/wiki/Collision-ratio-comparison#collision-study

note 1: Small data velocity is a _rough_ evaluation of algorithm's efficiency on small data. For more detailed analysis, please refer to next paragraph.

note 2: some algorithms feature _faster than RAM_ speed. In which case, they can only reach their full speed when input data is already in CPU cache (L3 or better). Otherwise, they max out on RAM speed limit.

### Small data

Performance on large data is only one part of the picture.
Hashing is also very useful in constructions like hash tables and bloom filters.
In these use cases, it's frequent to hash a lot of small data (starting at a few bytes).
Algorithm's performance can be very different for such scenarios, since parts of the algorithm,
such as initialization or finalization, become fixed cost.
The impact of branch mis-prediction also becomes much more present.

XXH3 has been designed for excellent performance on both long and small inputs,
which can be observed in the following graph:

![XXH3, latency, random size](https://user-images.githubusercontent.com/750081/61976089-aedeab00-af9f-11e9-9239-e5375d6c080f.png)

For a more detailed analysis, visit the wiki :
https://github.com/Cyan4973/xxHash/wiki/Performance-comparison#benchmarks-concentrating-on-small-data-

Quality
-------------------------

Speed is not the only property that matters.
Produced hash values must respect excellent dispersion and randomness properties,
so that any sub-section of it can be used to maximally spread out a table or index,
as well as reduce the amount of collisions to the minimal theoretical level, following the [birthday paradox].

`xxHash` has been tested with Austin Appleby's excellent SMHasher test suite,
and passes all tests, ensuring reasonable quality levels.
It also passes extended tests from [newer forks of SMHasher], featuring additional scenarios and conditions.

Finally, xxHash provides its own [massive collision tester](https://github.com/Cyan4973/xxHash/tree/dev/tests/collisions),
able to generate and compare billions of hash to test the limits of 64-bit hash algorithms.
On this front too, xxHash features good results, in line with the [birthday paradox].
A more detailed analysis is documented [in the wiki](https://github.com/Cyan4973/xxHash/wiki/Collision-ratio-comparison).

[birthday paradox]: https://en.wikipedia.org/wiki/Birthday_problem
[newer forks of SMHasher]: https://github.com/rurban/smhasher


### Build modifiers

The following macros can be set at compilation time to modify libxxhash's behavior. They are generally disabled by default.

- `XXH_INLINE_ALL`: Make all functions `inline`, with implementations being directly included within `xxhash.h`.
                    Inlining functions is beneficial for speed on small keys.
                    It's _extremely effective_ when key length is expressed as _a compile time constant_,
                    with performance improvements observed in the +200% range .
                    See [this article](https://fastcompression.blogspot.com/2018/03/xxhash-for-small-keys-impressive-power.html) for details.
- `XXH_PRIVATE_API`: same outcome as `XXH_INLINE_ALL`. Still available for legacy support.
                     The name underlines that `XXH_*` symbols will not be exported.
- `XXH_NAMESPACE`: Prefixes all symbols with the value of `XXH_NAMESPACE`.
                   This macro can only use compilable character set.
                   Useful to evade symbol naming collisions,
                   in case of multiple inclusions of xxHash's source code.
                   Client applications still use the regular function names,
                   as symbols are automatically translated through `xxhash.h`.
- `XXH_FORCE_MEMORY_ACCESS`: The default method `0` uses a portable `memcpy()` notation.
                             Method `1` uses a gcc-specific `packed` attribute, which can provide better performance for some targets.
                             Method `2` forces unaligned reads, which is not standards compliant, but might sometimes be the only way to extract better read performance.
                             Method `3` uses a byteshift operation, which is best for old compilers which don't inline `memcpy()` or big-endian systems without a byteswap instruction
- `XXH_FORCE_ALIGN_CHECK`: Use a faster direct read path when input is aligned.
                           This option can result in dramatic performance improvement when input to hash is aligned on 32 or 64-bit boundaries,
                           when running on architectures unable to load memory from unaligned addresses, or suffering a performance penalty from it.
                           It is (slightly) detrimental on platform with good unaligned memory access performance (same instruction for both aligned and unaligned accesses).
                           This option is automatically disabled on `x86`, `x64` and `aarch64`, and enabled on all other platforms.
- `XXH_VECTOR` : manually select a vector instruction set (default: auto-selected at compilation time). Available instruction sets are `XXH_SCALAR`, `XXH_SSE2`, `XXH_AVX2`, `XXH_AVX512`, `XXH_NEON` and `XXH_VSX`. Compiler may require additional flags to ensure proper support (for example, `gcc` on linux will require `-mavx2` for AVX2, and `-mavx512f` for AVX512).
- `XXH_NO_PREFETCH` : disable prefetching. XXH3 only.
- `XXH_PREFETCH_DIST` : select prefecting distance. XXH3 only.
- `XXH_NO_INLINE_HINTS`: By default, xxHash uses `__attribute__((always_inline))` and `__forceinline` to improve performance at the cost of code size.
                         Defining this macro to 1 will mark all internal functions as `static`, allowing the compiler to decide whether to inline a function or not.
                         This is very useful when optimizing for smallest binary size,
                         and is automatically defined when compiling with `-O0`, `-Os`, `-Oz`, or `-fno-inline` on GCC and Clang.
                         This may also increase performance depending on compiler and architecture.
- `XXH_REROLL`: Reduces the size of the generated code by not unrolling some loops.
                Impact on performance may vary, depending on platform and algorithm.
- `XXH_ACCEPT_NULL_INPUT_POINTER`: if set to `1`, when input is a `NULL` pointer,
                                   xxHash'd result is the same as a zero-length input
                                   (instead of a dereference segfault).
                                   Adds one branch at the beginning of each hash.
- `XXH_STATIC_LINKING_ONLY`: gives access to the state declaration for static allocation.
                             Incompatible with dynamic linking, due to risks of ABI changes.
- `XXH_NO_LONG_LONG`: removes compilation of algorithms relying on 64-bit types (XXH3 and XXH64). Only XXH32 will be compiled.
                      Useful for targets (architectures and compilers) without 64-bit support.
- `XXH_IMPORT`: MSVC specific: should only be defined for dynamic linking, as it prevents linkage errors.
- `XXH_CPU_LITTLE_ENDIAN`: By default, endianess is determined by a runtime test resolved at compile time.
                           If, for some reason, the compiler cannot simplify the runtime test, it can cost performance.
                           It's possible to skip auto-detection and simply state that the architecture is little-endian by setting this macro to 1.
                           Setting it to 0 states big-endian.

For the Command Line Interface `xxhsum`, the following environment variables can also be set :
- `DISPATCH=1` : use `xxh_x86dispatch.c`, to automatically select between `scalar`, `sse2`, `avx2` or `avx512` instruction set at runtime, depending on local host. This option is only valid for `x86`/`x64` systems.


### Building xxHash - Using vcpkg

You can download and install xxHash using the [vcpkg](https://github.com/Microsoft/vcpkg) dependency manager:

    git clone https://github.com/Microsoft/vcpkg.git
    cd vcpkg
    ./bootstrap-vcpkg.sh
    ./vcpkg integrate install
    ./vcpkg install xxhash

The xxHash port in vcpkg is kept up to date by Microsoft team members and community contributors. If the version is out of date, please [create an issue or pull request](https://github.com/Microsoft/vcpkg) on the vcpkg repository.


### Example

The simplest example calls xxhash 64-bit variant as a one-shot function
generating a hash value from a single buffer, and invoked from a C/C++ program:

```C
#include "xxhash.h"

    (...)
    XXH64_hash_t hash = XXH64(buffer, size, seed);
}
```

Streaming variant is more involved, but makes it possible to provide data incrementally:

```C
#include "stdlib.h"   /* abort() */
#include "xxhash.h"


XXH64_hash_t calcul_hash_streaming(FileHandler fh)
{
    /* create a hash state */
    XXH64_state_t* const state = XXH64_createState();
    if (state==NULL) abort();

    size_t const bufferSize = SOME_SIZE;
    void* const buffer = malloc(bufferSize);
    if (buffer==NULL) abort();

    /* Initialize state with selected seed */
    XXH64_hash_t const seed = 0;   /* or any other value */
    if (XXH64_reset(state, seed) == XXH_ERROR) abort();

    /* Feed the state with input data, any size, any number of times */
    (...)
    while ( /* some data left */ ) {
        size_t const length = get_more_data(buffer, bufferSize, fh);
        if (XXH64_update(state, buffer, length) == XXH_ERROR) abort();
        (...)
    }
    (...)

    /* Produce the final hash value */
    XXH64_hash_t const hash = XXH64_digest(state);

    /* State could be re-used; but in this example, it is simply freed  */
    free(buffer);
    XXH64_freeState(state);

    return hash;
}
```


### License

The library files `xxhash.c` and `xxhash.h` are BSD licensed.
The utility `xxhsum` is GPL licensed.


### Other programming languages

Beyond the C reference version,
xxHash is also available from many different programming languages,
thanks to great contributors.
They are [listed here](http://www.xxhash.com/#other-languages).


### Packaging status

Many distributions bundle a package manager
which allows easy xxhash installation as both a `libxxhash` library
and `xxhsum` command line interface.

[![Packaging status](https://repology.org/badge/vertical-allrepos/xxhash.svg)](https://repology.org/project/xxhash/versions)


### Special Thanks

- Takayuki Matsuoka, aka @t-mat, for creating `xxhsum -c` and great support during early xxh releases
- Mathias Westerdahl, aka @JCash, for introducing the first version of `XXH64`
- Devin Hussey, aka @easyaspi314, for incredible low-level optimizations on `XXH3` and `XXH128`
