/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* Microbenchmarks around CHTTP2 HPACK operations */

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <string.h>
#include <sstream>
extern "C" {
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/static_metadata.h"
}
#include "test/cpp/microbenchmarks/helpers.h"
#include "third_party/benchmark/include/benchmark/benchmark.h"

auto &force_library_initialization = Library::get();

////////////////////////////////////////////////////////////////////////////////
// HPACK encoder
//

static void BM_HpackEncoderInitDestroy(benchmark::State &state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_chttp2_hpack_compressor c;
  while (state.KeepRunning()) {
    grpc_chttp2_hpack_compressor_init(&c);
    grpc_chttp2_hpack_compressor_destroy(&exec_ctx, &c);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_HpackEncoderInitDestroy);

template <class Fixture>
static void BM_HpackEncoderEncodeHeader(benchmark::State &state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  static bool logged_representative_output = false;

  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  std::vector<grpc_mdelem> elems = Fixture::GetElems(&exec_ctx);
  std::vector<grpc_linked_mdelem> storage(elems.size());
  for (size_t i = 0; i < elems.size(); i++) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "addmd",
        grpc_metadata_batch_add_tail(&exec_ctx, &b, &storage[i], elems[i])));
  }

  grpc_chttp2_hpack_compressor c;
  grpc_chttp2_hpack_compressor_init(&c);
  grpc_transport_one_way_stats stats;
  memset(&stats, 0, sizeof(stats));
  grpc_slice_buffer outbuf;
  grpc_slice_buffer_init(&outbuf);
  while (state.KeepRunning()) {
    grpc_encode_header_options hopt = {
        static_cast<uint32_t>(state.iterations()),
        state.range(0) != 0,
        Fixture::kEnableTrueBinary,
        (size_t)state.range(1),
        &stats,
    };
    grpc_chttp2_encode_header(&exec_ctx, &c, &b, &hopt, &outbuf);
    if (!logged_representative_output && state.iterations() > 3) {
      logged_representative_output = true;
      for (size_t i = 0; i < outbuf.count; i++) {
        char *s = grpc_dump_slice(outbuf.slices[i], GPR_DUMP_HEX);
        gpr_log(GPR_DEBUG, "%" PRIdPTR ": %s", i, s);
        gpr_free(s);
      }
    }
    grpc_slice_buffer_reset_and_unref_internal(&exec_ctx, &outbuf);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_metadata_batch_destroy(&exec_ctx, &b);
  grpc_chttp2_hpack_compressor_destroy(&exec_ctx, &c);
  grpc_slice_buffer_destroy_internal(&exec_ctx, &outbuf);
  grpc_exec_ctx_finish(&exec_ctx);

  std::ostringstream label;
  label << "framing_bytes/iter:" << (static_cast<double>(stats.framing_bytes) /
                                     static_cast<double>(state.iterations()))
        << " header_bytes/iter:" << (static_cast<double>(stats.header_bytes) /
                                     static_cast<double>(state.iterations()));
  state.SetLabel(label.str());
  track_counters.Finish(state);
}

namespace hpack_encoder_fixtures {

class EmptyBatch {
 public:
  static constexpr bool kEnableTrueBinary = false;
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {};
  }
};

class SingleStaticElem {
 public:
  static constexpr bool kEnableTrueBinary = false;
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE};
  }
};

class SingleInternedElem {
 public:
  static constexpr bool kEnableTrueBinary = false;
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {grpc_mdelem_from_slices(
        exec_ctx, grpc_slice_intern(grpc_slice_from_static_string("abc")),
        grpc_slice_intern(grpc_slice_from_static_string("def")))};
  }
};

template <int kLength, bool kTrueBinary>
class SingleInternedBinaryElem {
 public:
  static constexpr bool kEnableTrueBinary = kTrueBinary;
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    grpc_slice bytes = MakeBytes();
    std::vector<grpc_mdelem> out = {grpc_mdelem_from_slices(
        exec_ctx, grpc_slice_intern(grpc_slice_from_static_string("abc-bin")),
        grpc_slice_intern(bytes))};
    grpc_slice_unref(bytes);
    return out;
  }

 private:
  static grpc_slice MakeBytes() {
    std::vector<char> v;
    for (int i = 0; i < kLength; i++) {
      v.push_back(static_cast<char>(rand()));
    }
    return grpc_slice_from_copied_buffer(v.data(), v.size());
  }
};

class SingleInternedKeyElem {
 public:
  static constexpr bool kEnableTrueBinary = false;
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {grpc_mdelem_from_slices(
        exec_ctx, grpc_slice_intern(grpc_slice_from_static_string("abc")),
        grpc_slice_from_static_string("def"))};
  }
};

class SingleNonInternedElem {
 public:
  static constexpr bool kEnableTrueBinary = false;
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {grpc_mdelem_from_slices(exec_ctx,
                                    grpc_slice_from_static_string("abc"),
                                    grpc_slice_from_static_string("def"))};
  }
};

template <int kLength, bool kTrueBinary>
class SingleNonInternedBinaryElem {
 public:
  static constexpr bool kEnableTrueBinary = kTrueBinary;
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {grpc_mdelem_from_slices(
        exec_ctx, grpc_slice_from_static_string("abc-bin"), MakeBytes())};
  }

 private:
  static grpc_slice MakeBytes() {
    std::vector<char> v;
    for (int i = 0; i < kLength; i++) {
      v.push_back(static_cast<char>(rand()));
    }
    return grpc_slice_from_copied_buffer(v.data(), v.size());
  }
};

class RepresentativeClientInitialMetadata {
 public:
  static constexpr bool kEnableTrueBinary = true;
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {
        GRPC_MDELEM_SCHEME_HTTP, GRPC_MDELEM_METHOD_POST,
        grpc_mdelem_from_slices(
            exec_ctx, GRPC_MDSTR_PATH,
            grpc_slice_intern(grpc_slice_from_static_string("/foo/bar"))),
        grpc_mdelem_from_slices(exec_ctx, GRPC_MDSTR_AUTHORITY,
                                grpc_slice_intern(grpc_slice_from_static_string(
                                    "foo.test.google.fr:1234"))),
        GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP,
        GRPC_MDELEM_TE_TRAILERS,
        GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC,
        grpc_mdelem_from_slices(
            exec_ctx, GRPC_MDSTR_USER_AGENT,
            grpc_slice_intern(grpc_slice_from_static_string(
                "grpc-c/3.0.0-dev (linux; chttp2; green)")))};
  }
};

class RepresentativeServerInitialMetadata {
 public:
  static constexpr bool kEnableTrueBinary = true;
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {GRPC_MDELEM_STATUS_200,
            GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC,
            GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP};
  }
};

class RepresentativeServerTrailingMetadata {
 public:
  static constexpr bool kEnableTrueBinary = true;
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {GRPC_MDELEM_GRPC_STATUS_0};
  }
};

BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader, EmptyBatch)->Args({0, 16384});
// test with eof (shouldn't affect anything)
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader, EmptyBatch)->Args({1, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader, SingleStaticElem)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader, SingleInternedKeyElem)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader, SingleInternedElem)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleInternedBinaryElem<1, false>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleInternedBinaryElem<3, false>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleInternedBinaryElem<10, false>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleInternedBinaryElem<31, false>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleInternedBinaryElem<100, false>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleInternedBinaryElem<1, true>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleInternedBinaryElem<3, true>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleInternedBinaryElem<10, true>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleInternedBinaryElem<31, true>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleInternedBinaryElem<100, true>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader, SingleNonInternedElem)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleNonInternedBinaryElem<1, false>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleNonInternedBinaryElem<3, false>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleNonInternedBinaryElem<10, false>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleNonInternedBinaryElem<31, false>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleNonInternedBinaryElem<100, false>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleNonInternedBinaryElem<1, true>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleNonInternedBinaryElem<3, true>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleNonInternedBinaryElem<10, true>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleNonInternedBinaryElem<31, true>)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   SingleNonInternedBinaryElem<100, true>)
    ->Args({0, 16384});
// test with a tiny frame size, to highlight continuation costs
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader, SingleNonInternedElem)
    ->Args({0, 1});

BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   RepresentativeClientInitialMetadata)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   RepresentativeServerInitialMetadata)
    ->Args({0, 16384});
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader,
                   RepresentativeServerTrailingMetadata)
    ->Args({1, 16384});

}  // namespace hpack_encoder_fixtures

////////////////////////////////////////////////////////////////////////////////
// HPACK parser
//

static void BM_HpackParserInitDestroy(benchmark::State &state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_chttp2_hpack_parser p;
  while (state.KeepRunning()) {
    grpc_chttp2_hpack_parser_init(&exec_ctx, &p);
    grpc_chttp2_hpack_parser_destroy(&exec_ctx, &p);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_HpackParserInitDestroy);

static void UnrefHeader(grpc_exec_ctx *exec_ctx, void *user_data,
                        grpc_mdelem md) {
  GRPC_MDELEM_UNREF(exec_ctx, md);
}

template <class Fixture>
static void BM_HpackParserParseHeader(benchmark::State &state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  std::vector<grpc_slice> init_slices = Fixture::GetInitSlices();
  std::vector<grpc_slice> benchmark_slices = Fixture::GetBenchmarkSlices();
  grpc_chttp2_hpack_parser p;
  grpc_chttp2_hpack_parser_init(&exec_ctx, &p);
  p.on_header = UnrefHeader;
  p.on_header_user_data = nullptr;
  for (auto slice : init_slices) {
    grpc_chttp2_hpack_parser_parse(&exec_ctx, &p, slice);
  }
  while (state.KeepRunning()) {
    for (auto slice : benchmark_slices) {
      grpc_chttp2_hpack_parser_parse(&exec_ctx, &p, slice);
    }
    grpc_exec_ctx_flush(&exec_ctx);
  }
  for (auto slice : init_slices) grpc_slice_unref(slice);
  for (auto slice : benchmark_slices) grpc_slice_unref(slice);
  grpc_chttp2_hpack_parser_destroy(&exec_ctx, &p);
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}

namespace hpack_parser_fixtures {

static grpc_slice MakeSlice(std::vector<uint8_t> bytes) {
  grpc_slice s = grpc_slice_malloc(bytes.size());
  uint8_t *p = GRPC_SLICE_START_PTR(s);
  for (auto b : bytes) {
    *p++ = b;
  }
  return s;
}

class EmptyBatch {
 public:
  static std::vector<grpc_slice> GetInitSlices() { return {}; }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice({})};
  }
};

class IndexedSingleStaticElem {
 public:
  static std::vector<grpc_slice> GetInitSlices() {
    return {MakeSlice(
        {0x40, 0x07, ':', 's', 't', 'a', 't', 'u', 's', 0x03, '2', '0', '0'})};
  }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice({0xbe})};
  }
};

class AddIndexedSingleStaticElem {
 public:
  static std::vector<grpc_slice> GetInitSlices() { return {}; }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice(
        {0x40, 0x07, ':', 's', 't', 'a', 't', 'u', 's', 0x03, '2', '0', '0'})};
  }
};

class KeyIndexedSingleStaticElem {
 public:
  static std::vector<grpc_slice> GetInitSlices() {
    return {MakeSlice(
        {0x40, 0x07, ':', 's', 't', 'a', 't', 'u', 's', 0x03, '2', '0', '0'})};
  }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice({0x7e, 0x03, 'd', 'e', 'f'})};
  }
};

class IndexedSingleInternedElem {
 public:
  static std::vector<grpc_slice> GetInitSlices() {
    return {MakeSlice({0x40, 0x03, 'a', 'b', 'c', 0x03, 'd', 'e', 'f'})};
  }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice({0xbe})};
  }
};

class AddIndexedSingleInternedElem {
 public:
  static std::vector<grpc_slice> GetInitSlices() { return {}; }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice({0x40, 0x03, 'a', 'b', 'c', 0x03, 'd', 'e', 'f'})};
  }
};

class KeyIndexedSingleInternedElem {
 public:
  static std::vector<grpc_slice> GetInitSlices() {
    return {MakeSlice({0x40, 0x03, 'a', 'b', 'c', 0x03, 'd', 'e', 'f'})};
  }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice({0x7e, 0x03, 'g', 'h', 'i'})};
  }
};

class NonIndexedElem {
 public:
  static std::vector<grpc_slice> GetInitSlices() { return {}; }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice({0x00, 0x03, 'a', 'b', 'c', 0x03, 'd', 'e', 'f'})};
  }
};

template <int kLength, bool kTrueBinary>
class NonIndexedBinaryElem;

template <int kLength>
class NonIndexedBinaryElem<kLength, true> {
 public:
  static std::vector<grpc_slice> GetInitSlices() { return {}; }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    std::vector<uint8_t> v = {
        0x00, 0x07, 'a', 'b', 'c',
        '-',  'b',  'i', 'n', static_cast<uint8_t>(kLength + 1),
        0};
    for (int i = 0; i < kLength; i++) {
      v.push_back(static_cast<uint8_t>(i));
    }
    return {MakeSlice(v)};
  }
};

template <>
class NonIndexedBinaryElem<1, false> {
 public:
  static std::vector<grpc_slice> GetInitSlices() { return {}; }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice(
        {0x00, 0x07, 'a', 'b', 'c', '-', 'b', 'i', 'n', 0x82, 0xf7, 0xb3})};
  }
};

template <>
class NonIndexedBinaryElem<3, false> {
 public:
  static std::vector<grpc_slice> GetInitSlices() { return {}; }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice({0x00, 0x07, 'a', 'b', 'c', '-', 'b', 'i', 'n', 0x84,
                       0x7f, 0x4e, 0x29, 0x3f})};
  }
};

template <>
class NonIndexedBinaryElem<10, false> {
 public:
  static std::vector<grpc_slice> GetInitSlices() { return {}; }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice({0x00, 0x07, 'a',  'b',  'c',  '-',  'b',
                       'i',  'n',  0x8b, 0x71, 0x0c, 0xa5, 0x81,
                       0x73, 0x7b, 0x47, 0x13, 0xe9, 0xf7, 0xe3})};
  }
};

template <>
class NonIndexedBinaryElem<31, false> {
 public:
  static std::vector<grpc_slice> GetInitSlices() { return {}; }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice({0x00, 0x07, 'a',  'b',  'c',  '-',  'b',  'i',  'n',
                       0xa3, 0x92, 0x43, 0x7f, 0xbe, 0x7c, 0xea, 0x6f, 0xf3,
                       0x3d, 0xa7, 0xa7, 0x67, 0xfb, 0xe2, 0x82, 0xf7, 0xf2,
                       0x8f, 0x1f, 0x9d, 0xdf, 0xf1, 0x7e, 0xb3, 0xef, 0xb2,
                       0x8f, 0x53, 0x77, 0xce, 0x0c, 0x13, 0xe3, 0xfd, 0x87})};
  }
};

template <>
class NonIndexedBinaryElem<100, false> {
 public:
  static std::vector<grpc_slice> GetInitSlices() { return {}; }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice(
        {0x00, 0x07, 'a',  'b',  'c',  '-',  'b',  'i',  'n',  0xeb, 0x1d, 0x4d,
         0xe8, 0x96, 0x8c, 0x14, 0x20, 0x06, 0xc1, 0xc3, 0xdf, 0x6e, 0x1f, 0xef,
         0xde, 0x2f, 0xde, 0xb7, 0xf2, 0xfe, 0x6d, 0xd4, 0xe4, 0x7d, 0xf5, 0x55,
         0x46, 0x52, 0x3d, 0x91, 0xf2, 0xd4, 0x6f, 0xca, 0x34, 0xcd, 0xd9, 0x39,
         0xbd, 0x03, 0x27, 0xe3, 0x9c, 0x74, 0xcc, 0x17, 0x34, 0xed, 0xa6, 0x6a,
         0x77, 0x73, 0x10, 0xcd, 0x8e, 0x4e, 0x5c, 0x7c, 0x72, 0x39, 0xd8, 0xe6,
         0x78, 0x6b, 0xdb, 0xa5, 0xb7, 0xab, 0xe7, 0x46, 0xae, 0x21, 0xab, 0x7f,
         0x01, 0x89, 0x13, 0xd7, 0xca, 0x17, 0x6e, 0xcb, 0xd6, 0x79, 0x71, 0x68,
         0xbf, 0x8a, 0x3f, 0x32, 0xe8, 0xba, 0xf5, 0xbe, 0xb3, 0xbc, 0xde, 0x28,
         0xc7, 0xcf, 0x62, 0x7a, 0x58, 0x2c, 0xcf, 0x4d, 0xe3})};
  }
};

class RepresentativeClientInitialMetadata {
 public:
  static std::vector<grpc_slice> GetInitSlices() {
    return {grpc_slice_from_static_string(
        // generated with:
        // ```
        // tools/codegen/core/gen_header_frame.py --compression inc --no_framing
        // < test/core/bad_client/tests/simple_request.headers
        // ```
        "@\x05:path\x08/foo/bar"
        "@\x07:scheme\x04http"
        "@\x07:method\x04POST"
        "@\x0a:authority\x09localhost"
        "@\x0c"
        "content-type\x10"
        "application/grpc"
        "@\x14grpc-accept-encoding\x15identity,deflate,gzip"
        "@\x02te\x08trailers"
        "@\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)")};
  }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    // generated with:
    // ```
    // tools/codegen/core/gen_header_frame.py --compression pre --no_framing
    // --hex < test/core/bad_client/tests/simple_request.headers
    // ```
    return {MakeSlice({0xc5, 0xc4, 0xc3, 0xc2, 0xc1, 0xc0, 0xbf, 0xbe})};
  }
};

class RepresentativeServerInitialMetadata {
 public:
  static std::vector<grpc_slice> GetInitSlices() {
    return {grpc_slice_from_static_string(
        // generated with:
        // ```
        // tools/codegen/core/gen_header_frame.py --compression inc --no_framing
        // <
        // test/cpp/microbenchmarks/representative_server_initial_metadata.headers
        // ```
        "@\x07:status\x03"
        "200"
        "@\x0c"
        "content-type\x10"
        "application/grpc"
        "@\x14grpc-accept-encoding\x15identity,deflate,gzip")};
  }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    // generated with:
    // ```
    // tools/codegen/core/gen_header_frame.py --compression pre --no_framing
    // --hex <
    // test/cpp/microbenchmarks/representative_server_initial_metadata.headers
    // ```
    return {MakeSlice({0xc0, 0xbf, 0xbe})};
  }
};

class RepresentativeServerTrailingMetadata {
 public:
  static std::vector<grpc_slice> GetInitSlices() {
    return {grpc_slice_from_static_string(
        // generated with:
        // ```
        // tools/codegen/core/gen_header_frame.py --compression inc --no_framing
        // <
        // test/cpp/microbenchmarks/representative_server_trailing_metadata.headers
        // ```
        "@\x0bgrpc-status\x01"
        "0"
        "@\x0cgrpc-message\x00")};
  }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    // generated with:
    // ```
    // tools/codegen/core/gen_header_frame.py --compression pre --no_framing
    // --hex <
    // test/cpp/microbenchmarks/representative_server_trailing_metadata.headers
    // ```
    return {MakeSlice({0xbf, 0xbe})};
  }
};

BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, EmptyBatch);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, IndexedSingleStaticElem);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, AddIndexedSingleStaticElem);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, KeyIndexedSingleStaticElem);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, IndexedSingleInternedElem);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, AddIndexedSingleInternedElem);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, KeyIndexedSingleInternedElem);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedElem);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<1, false>);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<3, false>);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<10, false>);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<31, false>);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<100, false>);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<1, true>);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<3, true>);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<10, true>);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<31, true>);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<100, true>);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeClientInitialMetadata);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeServerInitialMetadata);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeServerTrailingMetadata);

}  // namespace hpack_parser_fixtures

BENCHMARK_MAIN();
