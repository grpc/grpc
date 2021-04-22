/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* Microbenchmarks around CHTTP2 HPACK operations */

#include <benchmark/benchmark.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <string.h>
#include <memory>
#include <sstream>

#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/incoming_metadata.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"

#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

static grpc_slice MakeSlice(std::vector<uint8_t> bytes) {
  grpc_slice s = grpc_slice_malloc(bytes.size());
  uint8_t* p = GRPC_SLICE_START_PTR(s);
  for (auto b : bytes) {
    *p++ = b;
  }
  return s;
}

////////////////////////////////////////////////////////////////////////////////
// HPACK encoder
//

static void BM_HpackEncoderInitDestroy(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  std::unique_ptr<grpc_chttp2_hpack_compressor> c(
      new grpc_chttp2_hpack_compressor);
  for (auto _ : state) {
    grpc_chttp2_hpack_compressor_init(c.get());
    grpc_chttp2_hpack_compressor_destroy(c.get());
    grpc_core::ExecCtx::Get()->Flush();
  }

  track_counters.Finish(state);
}
BENCHMARK(BM_HpackEncoderInitDestroy);

static void BM_HpackEncoderEncodeDeadline(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  grpc_millis saved_now = grpc_core::ExecCtx::Get()->Now();

  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  b.deadline = saved_now + 30 * 1000;

  std::unique_ptr<grpc_chttp2_hpack_compressor> c(
      new grpc_chttp2_hpack_compressor);
  grpc_chttp2_hpack_compressor_init(c.get());
  grpc_transport_one_way_stats stats;
  stats = {};
  grpc_slice_buffer outbuf;
  grpc_slice_buffer_init(&outbuf);
  while (state.KeepRunning()) {
    grpc_encode_header_options hopt = {
        static_cast<uint32_t>(state.iterations()),
        true,
        false,
        static_cast<size_t>(1024),
        &stats,
    };
    grpc_chttp2_encode_header(c.get(), nullptr, 0, &b, &hopt, &outbuf);
    grpc_slice_buffer_reset_and_unref_internal(&outbuf);
    grpc_core::ExecCtx::Get()->Flush();
  }
  grpc_metadata_batch_destroy(&b);
  grpc_chttp2_hpack_compressor_destroy(c.get());
  grpc_slice_buffer_destroy_internal(&outbuf);

  std::ostringstream label;
  label << "framing_bytes/iter:"
        << (static_cast<double>(stats.framing_bytes) /
            static_cast<double>(state.iterations()))
        << " header_bytes/iter:"
        << (static_cast<double>(stats.header_bytes) /
            static_cast<double>(state.iterations()));
  track_counters.AddLabel(label.str());
  track_counters.Finish(state);
}
BENCHMARK(BM_HpackEncoderEncodeDeadline);

template <class Fixture>
static void BM_HpackEncoderEncodeHeader(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  static bool logged_representative_output = false;

  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  std::vector<grpc_mdelem> elems = Fixture::GetElems();
  std::vector<grpc_linked_mdelem> storage(elems.size());
  for (size_t i = 0; i < elems.size(); i++) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "addmd", grpc_metadata_batch_add_tail(&b, &storage[i], elems[i])));
  }

  std::unique_ptr<grpc_chttp2_hpack_compressor> c(
      new grpc_chttp2_hpack_compressor);
  grpc_chttp2_hpack_compressor_init(c.get());
  grpc_transport_one_way_stats stats;
  stats = {};
  grpc_slice_buffer outbuf;
  grpc_slice_buffer_init(&outbuf);
  while (state.KeepRunning()) {
    static constexpr int kEnsureMaxFrameAtLeast = 2;
    grpc_encode_header_options hopt = {
        static_cast<uint32_t>(state.iterations()),
        state.range(0) != 0,
        Fixture::kEnableTrueBinary,
        static_cast<size_t>(state.range(1) + kEnsureMaxFrameAtLeast),
        &stats,
    };
    grpc_chttp2_encode_header(c.get(), nullptr, 0, &b, &hopt, &outbuf);
    if (!logged_representative_output && state.iterations() > 3) {
      logged_representative_output = true;
      for (size_t i = 0; i < outbuf.count; i++) {
        char* s = grpc_dump_slice(outbuf.slices[i], GPR_DUMP_HEX);
        gpr_log(GPR_DEBUG, "%" PRIdPTR ": %s", i, s);
        gpr_free(s);
      }
    }
    grpc_slice_buffer_reset_and_unref_internal(&outbuf);
    grpc_core::ExecCtx::Get()->Flush();
  }
  grpc_metadata_batch_destroy(&b);
  grpc_chttp2_hpack_compressor_destroy(c.get());
  grpc_slice_buffer_destroy_internal(&outbuf);

  std::ostringstream label;
  label << "framing_bytes/iter:"
        << (static_cast<double>(stats.framing_bytes) /
            static_cast<double>(state.iterations()))
        << " header_bytes/iter:"
        << (static_cast<double>(stats.header_bytes) /
            static_cast<double>(state.iterations()));
  track_counters.AddLabel(label.str());
  track_counters.Finish(state);
}

namespace hpack_encoder_fixtures {

class EmptyBatch {
 public:
  static constexpr bool kEnableTrueBinary = false;
  static std::vector<grpc_mdelem> GetElems() { return {}; }
};

class SingleStaticElem {
 public:
  static constexpr bool kEnableTrueBinary = false;
  static std::vector<grpc_mdelem> GetElems() {
    return {GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE};
  }
};

class SingleInternedElem {
 public:
  static constexpr bool kEnableTrueBinary = false;
  static std::vector<grpc_mdelem> GetElems() {
    return {grpc_mdelem_from_slices(
        grpc_slice_intern(grpc_slice_from_static_string("abc")),
        grpc_slice_intern(grpc_slice_from_static_string("def")))};
  }
};

template <int kLength, bool kTrueBinary>
class SingleInternedBinaryElem {
 public:
  static constexpr bool kEnableTrueBinary = kTrueBinary;
  static std::vector<grpc_mdelem> GetElems() {
    grpc_slice bytes = MakeBytes();
    std::vector<grpc_mdelem> out = {grpc_mdelem_from_slices(
        grpc_slice_intern(grpc_slice_from_static_string("abc-bin")),
        grpc_slice_intern(bytes))};
    grpc_slice_unref(bytes);
    return out;
  }

 private:
  static grpc_slice MakeBytes() {
    std::vector<char> v;
    v.reserve(kLength);
    for (int i = 0; i < kLength; i++) {
      v.push_back(static_cast<char>(rand()));
    }
    return grpc_slice_from_copied_buffer(v.data(), v.size());
  }
};

class SingleInternedKeyElem {
 public:
  static constexpr bool kEnableTrueBinary = false;
  static std::vector<grpc_mdelem> GetElems() {
    return {grpc_mdelem_from_slices(
        grpc_slice_intern(grpc_slice_from_static_string("abc")),
        grpc_slice_from_static_string("def"))};
  }
};

class SingleNonInternedElem {
 public:
  static constexpr bool kEnableTrueBinary = false;
  static std::vector<grpc_mdelem> GetElems() {
    return {grpc_mdelem_from_slices(grpc_slice_from_static_string("abc"),
                                    grpc_slice_from_static_string("def"))};
  }
};

template <int kLength, bool kTrueBinary>
class SingleNonInternedBinaryElem {
 public:
  static constexpr bool kEnableTrueBinary = kTrueBinary;
  static std::vector<grpc_mdelem> GetElems() {
    return {grpc_mdelem_from_slices(grpc_slice_from_static_string("abc-bin"),
                                    MakeBytes())};
  }

 private:
  static grpc_slice MakeBytes() {
    std::vector<char> v;
    v.reserve(kLength);
    for (int i = 0; i < kLength; i++) {
      v.push_back(static_cast<char>(rand()));
    }
    return grpc_slice_from_copied_buffer(v.data(), v.size());
  }
};

class RepresentativeClientInitialMetadata {
 public:
  static constexpr bool kEnableTrueBinary = true;
  static std::vector<grpc_mdelem> GetElems() {
    return {
        GRPC_MDELEM_SCHEME_HTTP,
        GRPC_MDELEM_METHOD_POST,
        grpc_mdelem_from_slices(
            GRPC_MDSTR_PATH,
            grpc_slice_intern(grpc_slice_from_static_string("/foo/bar"))),
        grpc_mdelem_from_slices(GRPC_MDSTR_AUTHORITY,
                                grpc_slice_intern(grpc_slice_from_static_string(
                                    "foo.test.google.fr:1234"))),
        GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP,
        GRPC_MDELEM_TE_TRAILERS,
        GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC,
        grpc_mdelem_from_slices(
            GRPC_MDSTR_USER_AGENT,
            grpc_slice_intern(grpc_slice_from_static_string(
                "grpc-c/3.0.0-dev (linux; chttp2; green)")))};
  }
};

// This fixture reflects how initial metadata are sent by a production client,
// with non-indexed :path and binary headers. The metadata here are the same as
// the corresponding parser benchmark below.
class MoreRepresentativeClientInitialMetadata {
 public:
  static constexpr bool kEnableTrueBinary = true;
  static std::vector<grpc_mdelem> GetElems() {
    return {
        GRPC_MDELEM_SCHEME_HTTP,
        GRPC_MDELEM_METHOD_POST,
        grpc_mdelem_from_slices(GRPC_MDSTR_PATH,
                                grpc_slice_intern(grpc_slice_from_static_string(
                                    "/grpc.test.FooService/BarMethod"))),
        grpc_mdelem_from_slices(GRPC_MDSTR_AUTHORITY,
                                grpc_slice_intern(grpc_slice_from_static_string(
                                    "foo.test.google.fr:1234"))),
        grpc_mdelem_from_slices(
            GRPC_MDSTR_GRPC_TRACE_BIN,
            grpc_slice_from_static_string("\x00\x01\x02\x03\x04\x05\x06\x07\x08"
                                          "\x09\x0a\x0b\x0c\x0d\x0e\x0f"
                                          "\x10\x11\x12\x13\x14\x15\x16\x17\x18"
                                          "\x19\x1a\x1b\x1c\x1d\x1e\x1f"
                                          "\x20\x21\x22\x23\x24\x25\x26\x27\x28"
                                          "\x29\x2a\x2b\x2c\x2d\x2e\x2f"
                                          "\x30")),
        grpc_mdelem_from_slices(
            GRPC_MDSTR_GRPC_TAGS_BIN,
            grpc_slice_from_static_string("\x00\x01\x02\x03\x04\x05\x06\x07\x08"
                                          "\x09\x0a\x0b\x0c\x0d\x0e\x0f"
                                          "\x10\x11\x12\x13")),
        GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP,
        GRPC_MDELEM_TE_TRAILERS,
        GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC,
        grpc_mdelem_from_slices(
            GRPC_MDSTR_USER_AGENT,
            grpc_slice_intern(grpc_slice_from_static_string(
                "grpc-c/3.0.0-dev (linux; chttp2; green)")))};
  }
};

class RepresentativeServerInitialMetadata {
 public:
  static constexpr bool kEnableTrueBinary = true;
  static std::vector<grpc_mdelem> GetElems() {
    return {GRPC_MDELEM_STATUS_200,
            GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC,
            GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP};
  }
};

class RepresentativeServerTrailingMetadata {
 public:
  static constexpr bool kEnableTrueBinary = true;
  static std::vector<grpc_mdelem> GetElems() {
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
                   MoreRepresentativeClientInitialMetadata)
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

static void BM_HpackParserInitDestroy(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  grpc_chttp2_hpack_parser p;
  // Initial destruction so we don't leak memory in the loop.
  grpc_chttp2_hptbl_destroy(&p.table);
  for (auto _ : state) {
    grpc_chttp2_hpack_parser_init(&p);
    // Note that grpc_chttp2_hpack_parser_destroy frees the table dynamic
    // elements so we need to recreate it here. In actual operation,
    // new grpc_chttp2_hpack_parser_destroy allocates the table once
    // and for all.
    new (&p.table) grpc_chttp2_hptbl();
    grpc_chttp2_hpack_parser_destroy(&p);
    grpc_core::ExecCtx::Get()->Flush();
  }

  track_counters.Finish(state);
}
BENCHMARK(BM_HpackParserInitDestroy);

static grpc_error_handle UnrefHeader(void* /*user_data*/, grpc_mdelem md) {
  GRPC_MDELEM_UNREF(md);
  return GRPC_ERROR_NONE;
}

template <class Fixture, grpc_error_handle (*OnHeader)(void*, grpc_mdelem)>
static void BM_HpackParserParseHeader(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  std::vector<grpc_slice> init_slices = Fixture::GetInitSlices();
  std::vector<grpc_slice> benchmark_slices = Fixture::GetBenchmarkSlices();
  grpc_chttp2_hpack_parser p;
  grpc_chttp2_hpack_parser_init(&p);
  const int kArenaSize = 4096 * 4096;
  p.on_header_user_data = grpc_core::Arena::Create(kArenaSize);
  p.on_header = OnHeader;
  for (auto slice : init_slices) {
    GPR_ASSERT(GRPC_ERROR_NONE == grpc_chttp2_hpack_parser_parse(&p, slice));
  }
  while (state.KeepRunning()) {
    for (auto slice : benchmark_slices) {
      GPR_ASSERT(GRPC_ERROR_NONE == grpc_chttp2_hpack_parser_parse(&p, slice));
    }
    grpc_core::ExecCtx::Get()->Flush();
    // Recreate arena every 4k iterations to avoid oom
    if (0 == (state.iterations() & 0xfff)) {
      static_cast<grpc_core::Arena*>(p.on_header_user_data)->Destroy();
      p.on_header_user_data = grpc_core::Arena::Create(kArenaSize);
    }
  }
  // Clean up
  static_cast<grpc_core::Arena*>(p.on_header_user_data)->Destroy();
  for (auto slice : init_slices) grpc_slice_unref(slice);
  for (auto slice : benchmark_slices) grpc_slice_unref(slice);
  grpc_chttp2_hpack_parser_destroy(&p);

  track_counters.Finish(state);
}

namespace hpack_parser_fixtures {

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

// This fixture reflects how initial metadata are sent by a production client,
// with non-indexed :path and binary headers. The metadata here are the same as
// the corresponding encoder benchmark above.
class MoreRepresentativeClientInitialMetadata {
 public:
  static std::vector<grpc_slice> GetInitSlices() {
    return {MakeSlice(
        {0x40, 0x07, ':',  's',  'c',  'h',  'e',  'm',  'e',  0x04, 'h',  't',
         't',  'p',  0x40, 0x07, ':',  'm',  'e',  't',  'h',  'o',  'd',  0x04,
         'P',  'O',  'S',  'T',  0x40, 0x05, ':',  'p',  'a',  't',  'h',  0x1f,
         '/',  'g',  'r',  'p',  'c',  '.',  't',  'e',  's',  't',  '.',  'F',
         'o',  'o',  'S',  'e',  'r',  'v',  'i',  'c',  'e',  '/',  'B',  'a',
         'r',  'M',  'e',  't',  'h',  'o',  'd',  0x40, 0x0a, ':',  'a',  'u',
         't',  'h',  'o',  'r',  'i',  't',  'y',  0x09, 'l',  'o',  'c',  'a',
         'l',  'h',  'o',  's',  't',  0x40, 0x0e, 'g',  'r',  'p',  'c',  '-',
         't',  'r',  'a',  'c',  'e',  '-',  'b',  'i',  'n',  0x31, 0x00, 0x01,
         0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
         0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
         0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
         0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x40,
         0x0d, 'g',  'r',  'p',  'c',  '-',  't',  'a',  'g',  's',  '-',  'b',
         'i',  'n',  0x14, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
         0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x40,
         0x0c, 'c',  'o',  'n',  't',  'e',  'n',  't',  '-',  't',  'y',  'p',
         'e',  0x10, 'a',  'p',  'p',  'l',  'i',  'c',  'a',  't',  'i',  'o',
         'n',  '/',  'g',  'r',  'p',  'c',  0x40, 0x14, 'g',  'r',  'p',  'c',
         '-',  'a',  'c',  'c',  'e',  'p',  't',  '-',  'e',  'n',  'c',  'o',
         'd',  'i',  'n',  'g',  0x15, 'i',  'd',  'e',  'n',  't',  'i',  't',
         'y',  ',',  'd',  'e',  'f',  'l',  'a',  't',  'e',  ',',  'g',  'z',
         'i',  'p',  0x40, 0x02, 't',  'e',  0x08, 't',  'r',  'a',  'i',  'l',
         'e',  'r',  's',  0x40, 0x0a, 'u',  's',  'e',  'r',  '-',  'a',  'g',
         'e',  'n',  't',  0x22, 'b',  'a',  'd',  '-',  'c',  'l',  'i',  'e',
         'n',  't',  ' ',  'g',  'r',  'p',  'c',  '-',  'c',  '/',  '0',  '.',
         '1',  '2',  '.',  '0',  '.',  '0',  ' ',  '(',  'l',  'i',  'n',  'u',
         'x',  ')'})};
  }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    return {MakeSlice(
        {0xc7, 0xc6, 0xc5, 0xc4, 0x7f, 0x04, 0x31, 0x00, 0x01, 0x02, 0x03, 0x04,
         0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
         0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
         0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
         0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x7f, 0x03, 0x14, 0x00,
         0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
         0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0xc1, 0xc0, 0xbf, 0xbe})};
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

static void free_timeout(void* p) { gpr_free(p); }

// Benchmark the current on_initial_header implementation
static grpc_error_handle OnInitialHeader(void* user_data, grpc_mdelem md) {
  // Setup for benchmark. This will bloat the absolute values of this benchmark
  grpc_chttp2_incoming_metadata_buffer buffer(
      static_cast<grpc_core::Arena*>(user_data));
  bool seen_error = false;

  // Below here is the code we actually care about benchmarking
  if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_GRPC_STATUS) &&
      !grpc_mdelem_eq(md, GRPC_MDELEM_GRPC_STATUS_0)) {
    seen_error = true;
  }
  if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_GRPC_TIMEOUT)) {
    grpc_millis* cached_timeout =
        static_cast<grpc_millis*>(grpc_mdelem_get_user_data(md, free_timeout));
    grpc_millis timeout;
    if (cached_timeout != nullptr) {
      timeout = *cached_timeout;
    } else {
      if (GPR_UNLIKELY(
              !grpc_http2_decode_timeout(GRPC_MDVALUE(md), &timeout))) {
        char* val = grpc_slice_to_c_string(GRPC_MDVALUE(md));
        gpr_log(GPR_ERROR, "Ignoring bad timeout value '%s'", val);
        gpr_free(val);
        timeout = GRPC_MILLIS_INF_FUTURE;
      }
      if (GRPC_MDELEM_IS_INTERNED(md)) {
        /* not already parsed: parse it now, and store the
         * result away */
        cached_timeout =
            static_cast<grpc_millis*>(gpr_malloc(sizeof(grpc_millis)));
        *cached_timeout = timeout;
        grpc_mdelem_set_user_data(md, free_timeout, cached_timeout);
      }
    }
    benchmark::DoNotOptimize(timeout);
    GRPC_MDELEM_UNREF(md);
  } else {
    const size_t new_size = buffer.size + GRPC_MDELEM_LENGTH(md);
    if (!seen_error) {
      buffer.size = new_size;
    }
    grpc_error_handle error =
        grpc_chttp2_incoming_metadata_buffer_add(&buffer, md);
    if (error != GRPC_ERROR_NONE) {
      GPR_ASSERT(0);
    }
  }
  return GRPC_ERROR_NONE;
}

// Benchmark timeout handling
static grpc_error_handle OnHeaderTimeout(void* /*user_data*/, grpc_mdelem md) {
  if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_GRPC_TIMEOUT)) {
    grpc_millis* cached_timeout =
        static_cast<grpc_millis*>(grpc_mdelem_get_user_data(md, free_timeout));
    grpc_millis timeout;
    if (cached_timeout != nullptr) {
      timeout = *cached_timeout;
    } else {
      if (!grpc_http2_decode_timeout(GRPC_MDVALUE(md), &timeout)) {
        char* val = grpc_slice_to_c_string(GRPC_MDVALUE(md));
        gpr_log(GPR_ERROR, "Ignoring bad timeout value '%s'", val);
        gpr_free(val);
        timeout = GRPC_MILLIS_INF_FUTURE;
      }
      if (GRPC_MDELEM_IS_INTERNED(md)) {
        /* not already parsed: parse it now, and store the
         * result away */
        cached_timeout =
            static_cast<grpc_millis*>(gpr_malloc(sizeof(grpc_millis)));
        *cached_timeout = timeout;
        grpc_mdelem_set_user_data(md, free_timeout, cached_timeout);
      }
    }
    benchmark::DoNotOptimize(timeout);
    GRPC_MDELEM_UNREF(md);
  } else {
    GPR_ASSERT(0);
  }
  return GRPC_ERROR_NONE;
}

// Send the same deadline repeatedly
class SameDeadline {
 public:
  static std::vector<grpc_slice> GetInitSlices() {
    return {
        grpc_slice_from_static_string("@\x0cgrpc-timeout\x03"
                                      "30S")};
  }
  static std::vector<grpc_slice> GetBenchmarkSlices() {
    // Use saved key and literal value.
    return {MakeSlice({0x0f, 0x2f, 0x03, '3', '0', 'S'})};
  }
};

BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, EmptyBatch, UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, IndexedSingleStaticElem,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, AddIndexedSingleStaticElem,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, KeyIndexedSingleStaticElem,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, IndexedSingleInternedElem,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, AddIndexedSingleInternedElem,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, KeyIndexedSingleInternedElem,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedElem, UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<1, false>,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<3, false>,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<10, false>,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<31, false>,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<100, false>,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<1, true>,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<3, true>,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<10, true>,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<31, true>,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, NonIndexedBinaryElem<100, true>,
                   UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeClientInitialMetadata, UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   MoreRepresentativeClientInitialMetadata, UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeServerInitialMetadata, UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeServerTrailingMetadata, UnrefHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeClientInitialMetadata, OnInitialHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   MoreRepresentativeClientInitialMetadata, OnInitialHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeServerInitialMetadata, OnInitialHeader);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader, SameDeadline, OnHeaderTimeout);

}  // namespace hpack_parser_fixtures

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
