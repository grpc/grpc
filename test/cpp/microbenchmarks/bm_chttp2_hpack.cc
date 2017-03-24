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

#include <grpc/support/log.h>
#include <string.h>
#include <sstream>
extern "C" {
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/slice/slice_internal.h"
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
    grpc_chttp2_encode_header(&exec_ctx, &c, (uint32_t)state.iterations(), &b,
                              state.range(0), state.range(1), &stats, &outbuf);
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
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {};
  }
};

class SingleStaticElem {
 public:
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE};
  }
};

class SingleInternedElem {
 public:
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {grpc_mdelem_from_slices(
        exec_ctx, grpc_slice_intern(grpc_slice_from_static_string("abc")),
        grpc_slice_intern(grpc_slice_from_static_string("def")))};
  }
};

class SingleInternedKeyElem {
 public:
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {grpc_mdelem_from_slices(
        exec_ctx, grpc_slice_intern(grpc_slice_from_static_string("abc")),
        grpc_slice_from_static_string("def"))};
  }
};

class SingleNonInternedElem {
 public:
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {grpc_mdelem_from_slices(exec_ctx,
                                    grpc_slice_from_static_string("abc"),
                                    grpc_slice_from_static_string("def"))};
  }
};

class RepresentativeClientInitialMetadata {
 public:
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
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {GRPC_MDELEM_STATUS_200,
            GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC,
            GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP};
  }
};

class RepresentativeServerTrailingMetadata {
 public:
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
BENCHMARK_TEMPLATE(BM_HpackEncoderEncodeHeader, SingleNonInternedElem)
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
  grpc_chttp2_hpack_parser_destroy(&exec_ctx, &p);
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}

namespace hpack_parser_fixtures {

static grpc_slice MakeSlice(std::initializer_list<uint8_t> bytes) {
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
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeClientInitialMetadata);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeServerInitialMetadata);
BENCHMARK_TEMPLATE(BM_HpackParserParseHeader,
                   RepresentativeServerTrailingMetadata);

}  // namespace hpack_parser_fixtures

BENCHMARK_MAIN();
