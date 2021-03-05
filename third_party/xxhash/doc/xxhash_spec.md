xxHash fast digest algorithm
======================

### Notices

Copyright (c) Yann Collet

Permission is granted to copy and distribute this document
for any purpose and without charge,
including translations into other languages
and incorporation into compilations,
provided that the copyright notice and this notice are preserved,
and that any substantive changes or deletions from the original
are clearly marked.
Distribution of this document is unlimited.

### Version

0.1.1 (10/10/18)


Table of Contents
---------------------
- [Introduction](#introduction)
- [XXH32 algorithm description](#xxh32-algorithm-description)
- [XXH64 algorithm description](#xxh64-algorithm-description)
- [Performance considerations](#performance-considerations)
- [Reference Implementation](#reference-implementation)


Introduction
----------------

This document describes the xxHash digest algorithm for both 32-bit and 64-bit variants, named `XXH32` and `XXH64`. The algorithm takes an input a message of arbitrary length and an optional seed value, then produces an output of 32 or 64-bit as "fingerprint" or "digest".

xxHash is primarily designed for speed. It is labeled non-cryptographic, and is not meant to avoid intentional collisions (same digest for 2 different messages), or to prevent producing a message with a predefined digest.

XXH32 is designed to be fast on 32-bit machines.
XXH64 is designed to be fast on 64-bit machines.
Both variants produce different output.
However, a given variant shall produce exactly the same output, irrespective of the cpu / os used. In particular, the result remains identical whatever the endianness and width of the cpu is.

### Operation notations

All operations are performed modulo {32,64} bits. Arithmetic overflows are expected.
`XXH32` uses 32-bit modular operations. `XXH64` uses 64-bit modular operations.

- `+`: denotes modular addition
- `*`: denotes modular multiplication
- `X <<< s`: denotes the value obtained by circularly shifting (rotating) `X` left by `s` bit positions.
- `X >> s`: denotes the value obtained by shifting `X` right by s bit positions. Upper `s` bits become `0`.
- `X xor Y`: denotes the bit-wise XOR of `X` and `Y` (same width).


XXH32 Algorithm Description
-------------------------------------

### Overview

We begin by supposing that we have a message of any length `L` as input, and that we wish to find its digest. Here `L` is an arbitrary nonnegative integer; `L` may be zero. The following steps are performed to compute the digest of the message.

The algorithm collect and transform input in _stripes_ of 16 bytes. The transforms are stored inside 4 "accumulators", each one storing an unsigned 32-bit value. Each accumulator can be processed independently in parallel, speeding up processing for cpu with multiple execution units.

The algorithm uses 32-bits addition, multiplication, rotate, shift and xor operations. Many operations require some 32-bits prime number constants, all defined below:

    static const u32 PRIME32_1 = 0x9E3779B1U;  // 0b10011110001101110111100110110001
    static const u32 PRIME32_2 = 0x85EBCA77U;  // 0b10000101111010111100101001110111
    static const u32 PRIME32_3 = 0xC2B2AE3DU;  // 0b11000010101100101010111000111101
    static const u32 PRIME32_4 = 0x27D4EB2FU;  // 0b00100111110101001110101100101111
    static const u32 PRIME32_5 = 0x165667B1U;  // 0b00010110010101100110011110110001

These constants are prime numbers, and feature a good mix of bits 1 and 0, neither too regular, nor too dissymmetric. These properties help dispersion capabilities.

### Step 1. Initialize internal accumulators

Each accumulator gets an initial value based on optional `seed` input. Since the `seed` is optional, it can be `0`.

        u32 acc1 = seed + PRIME32_1 + PRIME32_2;
        u32 acc2 = seed + PRIME32_2;
        u32 acc3 = seed + 0;
        u32 acc4 = seed - PRIME32_1;

#### Special case: input is less than 16 bytes

When the input is too small (< 16 bytes), the algorithm will not process any stripes. Consequently, it will not make use of parallel accumulators.

In this case, a simplified initialization is performed, using a single accumulator:

      u32 acc  = seed + PRIME32_5;

The algorithm then proceeds directly to step 4.

### Step 2. Process stripes

A stripe is a contiguous segment of 16 bytes.
It is evenly divided into 4 _lanes_, of 4 bytes each.
The first lane is used to update accumulator 1, the second lane is used to update accumulator 2, and so on.

Each lane read its associated 32-bit value using __little-endian__ convention.

For each {lane, accumulator}, the update process is called a _round_, and applies the following formula:

    accN = accN + (laneN * PRIME32_2);
    accN = accN <<< 13;
    accN = accN * PRIME32_1;

This shuffles the bits so that any bit from input _lane_ impacts several bits in output _accumulator_. All operations are performed modulo 2^32.

Input is consumed one full stripe at a time. Step 2 is looped as many times as necessary to consume the whole input, except for the last remaining bytes which cannot form a stripe (< 16 bytes).
When that happens, move to step 3.

### Step 3. Accumulator convergence

All 4 lane accumulators from the previous steps are merged to produce a single remaining accumulator of the same width (32-bit). The associated formula is as follows:

    acc = (acc1 <<< 1) + (acc2 <<< 7) + (acc3 <<< 12) + (acc4 <<< 18);

### Step 4. Add input length

The input total length is presumed known at this stage. This step is just about adding the length to accumulator, so that it participates to final mixing.

    acc = acc + (u32)inputLength;

Note that, if input length is so large that it requires more than 32-bits, only the lower 32-bits are added to the accumulator.

### Step 5. Consume remaining input

There may be up to 15 bytes remaining to consume from the input.
The final stage will digest them according to following pseudo-code:

    while (remainingLength >= 4) {
        lane = read_32bit_little_endian(input_ptr);
        acc = acc + lane * PRIME32_3;
        acc = (acc <<< 17) * PRIME32_4;
        input_ptr += 4; remainingLength -= 4;
    }

    while (remainingLength >= 1) {
        lane = read_byte(input_ptr);
        acc = acc + lane * PRIME32_5;
        acc = (acc <<< 11) * PRIME32_1;
        input_ptr += 1; remainingLength -= 1;
    }

This process ensures that all input bytes are present in the final mix.

### Step 6. Final mix (avalanche)

The final mix ensures that all input bits have a chance to impact any bit in the output digest, resulting in an unbiased distribution. This is also called avalanche effect.

    acc = acc xor (acc >> 15);
    acc = acc * PRIME32_2;
    acc = acc xor (acc >> 13);
    acc = acc * PRIME32_3;
    acc = acc xor (acc >> 16);

### Step 7. Output

The `XXH32()` function produces an unsigned 32-bit value as output.

For systems which require to store and/or display the result in binary or hexadecimal format, the canonical format is defined to reproduce the same value as the natural decimal format, hence follows __big-endian__ convention (most significant byte first).


XXH64 Algorithm Description
-------------------------------------

### Overview

`XXH64`'s algorithm structure is very similar to `XXH32` one. The major difference is that `XXH64` uses 64-bit arithmetic, speeding up memory transfer for 64-bit compliant systems, but also relying on cpu capability to efficiently perform 64-bit operations.

The algorithm collects and transforms input in _stripes_ of 32 bytes. The transforms are stored inside 4 "accumulators", each one storing an unsigned 64-bit value. Each accumulator can be processed independently in parallel, speeding up processing for cpu with multiple execution units.

The algorithm uses 64-bit addition, multiplication, rotate, shift and xor operations. Many operations require some 64-bit prime number constants, all defined below:

    static const u64 PRIME64_1 = 0x9E3779B185EBCA87ULL;  // 0b1001111000110111011110011011000110000101111010111100101010000111
    static const u64 PRIME64_2 = 0xC2B2AE3D27D4EB4FULL;  // 0b1100001010110010101011100011110100100111110101001110101101001111
    static const u64 PRIME64_3 = 0x165667B19E3779F9ULL;  // 0b0001011001010110011001111011000110011110001101110111100111111001
    static const u64 PRIME64_4 = 0x85EBCA77C2B2AE63ULL;  // 0b1000010111101011110010100111011111000010101100101010111001100011
    static const u64 PRIME64_5 = 0x27D4EB2F165667C5ULL;  // 0b0010011111010100111010110010111100010110010101100110011111000101

These constants are prime numbers, and feature a good mix of bits 1 and 0, neither too regular, nor too dissymmetric. These properties help dispersion capabilities.

### Step 1. Initialise internal accumulators

Each accumulator gets an initial value based on optional `seed` input. Since the `seed` is optional, it can be `0`.

        u64 acc1 = seed + PRIME64_1 + PRIME64_2;
        u64 acc2 = seed + PRIME64_2;
        u64 acc3 = seed + 0;
        u64 acc4 = seed - PRIME64_1;

#### Special case: input is less than 32 bytes

When the input is too small (< 32 bytes), the algorithm will not process any stripes. Consequently, it will not make use of parallel accumulators.

In this case, a simplified initialization is performed, using a single accumulator:

      u64 acc  = seed + PRIME64_5;

The algorithm then proceeds directly to step 4.

### Step 2. Process stripes

A stripe is a contiguous segment of 32 bytes.
It is evenly divided into 4 _lanes_, of 8 bytes each.
The first lane is used to update accumulator 1, the second lane is used to update accumulator 2, and so on.

Each lane read its associated 64-bit value using __little-endian__ convention.

For each {lane, accumulator}, the update process is called a _round_, and applies the following formula:

    round(accN,laneN):
    accN = accN + (laneN * PRIME64_2);
    accN = accN <<< 31;
    return accN * PRIME64_1;

This shuffles the bits so that any bit from input _lane_ impacts several bits in output _accumulator_. All operations are performed modulo 2^64.

Input is consumed one full stripe at a time. Step 2 is looped as many times as necessary to consume the whole input, except for the last remaining bytes which cannot form a stripe (< 32 bytes).
When that happens, move to step 3.

### Step 3. Accumulator convergence

All 4 lane accumulators from previous steps are merged to produce a single remaining accumulator of same width (64-bit). The associated formula is as follows.

Note that accumulator convergence is more complex than 32-bit variant, and requires to define another function called _mergeAccumulator()_:

    mergeAccumulator(acc,accN):
    acc  = acc xor round(0, accN);
    acc  = acc * PRIME64_1;
    return acc + PRIME64_4;

which is then used in the convergence formula:

    acc = (acc1 <<< 1) + (acc2 <<< 7) + (acc3 <<< 12) + (acc4 <<< 18);
    acc = mergeAccumulator(acc, acc1);
    acc = mergeAccumulator(acc, acc2);
    acc = mergeAccumulator(acc, acc3);
    acc = mergeAccumulator(acc, acc4);

### Step 4. Add input length

The input total length is presumed known at this stage. This step is just about adding the length to accumulator, so that it participates to final mixing.

    acc = acc + inputLength;

### Step 5. Consume remaining input

There may be up to 31 bytes remaining to consume from the input.
The final stage will digest them according to following pseudo-code:

    while (remainingLength >= 8) {
        lane = read_64bit_little_endian(input_ptr);
        acc = acc xor round(0, lane);
        acc = (acc <<< 27) * PRIME64_1;
        acc = acc + PRIME64_4;
        input_ptr += 8; remainingLength -= 8;
    }

    if (remainingLength >= 4) {
        lane = read_32bit_little_endian(input_ptr);
        acc = acc xor (lane * PRIME64_1);
        acc = (acc <<< 23) * PRIME64_2;
        acc = acc + PRIME64_3;
        input_ptr += 4; remainingLength -= 4;
    }

    while (remainingLength >= 1) {
        lane = read_byte(input_ptr);
        acc = acc xor (lane * PRIME64_5);
        acc = (acc <<< 11) * PRIME64_1;
        input_ptr += 1; remainingLength -= 1;
    }

This process ensures that all input bytes are present in the final mix.

### Step 6. Final mix (avalanche)

The final mix ensures that all input bits have a chance to impact any bit in the output digest, resulting in an unbiased distribution. This is also called avalanche effect.

    acc = acc xor (acc >> 33);
    acc = acc * PRIME64_2;
    acc = acc xor (acc >> 29);
    acc = acc * PRIME64_3;
    acc = acc xor (acc >> 32);

### Step 7. Output

The `XXH64()` function produces an unsigned 64-bit value as output.

For systems which require to store and/or display the result in binary or hexadecimal format, the canonical format is defined to reproduce the same value as the natural decimal format, hence follows __big-endian__ convention (most significant byte first).

Performance considerations
----------------------------------

The xxHash algorithms are simple and compact to implement. They provide a system independent "fingerprint" or digest of a message of arbitrary length.

The algorithm allows input to be streamed and processed in multiple steps. In such case, an internal buffer is needed to ensure data is presented to the algorithm in full stripes.

On 64-bit systems, the 64-bit variant `XXH64` is generally faster to compute, so it is a recommended variant, even when only 32-bit are needed.

On 32-bit systems though, positions are reversed: `XXH64` performance is reduced, due to its usage of 64-bit arithmetic. `XXH32` becomes a faster variant.


Reference Implementation
----------------------------------------

A reference library written in C is available at https://www.xxhash.com.
The web page also links to multiple other implementations written in many different languages.
It links to the [github project page](https://github.com/Cyan4973/xxHash) where an [issue board](https://github.com/Cyan4973/xxHash/issues) can be used for further public discussions on the topic.


Version changes
--------------------
v0.7.3: Minor fixes
v0.1.1: added a note on rationale for selection of constants
v0.1.0: initial release
