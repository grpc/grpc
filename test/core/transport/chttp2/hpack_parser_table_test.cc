//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/ext/transport/chttp2/transport/hpack_parser_table.h"

#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {
void AssertIndex(const HPackTable* tbl, uint32_t idx, const char* key,
                 const char* value) {
  const auto* md = tbl->Lookup(idx);
  ASSERT_NE(md, nullptr);
  EXPECT_EQ(md->md.DebugString(), absl::StrCat(key, ": ", value));
}
}  // namespace

TEST(HpackParserTableTest, StaticTable) {
  ExecCtx exec_ctx;
  HPackTable tbl;

  AssertIndex(&tbl, 1, ":authority", "");
  AssertIndex(&tbl, 2, ":method", "GET");
  AssertIndex(&tbl, 3, ":method", "POST");
  AssertIndex(&tbl, 4, ":path", "/");
  AssertIndex(&tbl, 5, ":path", "/index.html");
  AssertIndex(&tbl, 6, ":scheme", "http");
  AssertIndex(&tbl, 7, ":scheme", "https");
  AssertIndex(&tbl, 8, ":status", "200");
  AssertIndex(&tbl, 9, ":status", "204");
  AssertIndex(&tbl, 10, ":status", "206");
  AssertIndex(&tbl, 11, ":status", "304");
  AssertIndex(&tbl, 12, ":status", "400");
  AssertIndex(&tbl, 13, ":status", "404");
  AssertIndex(&tbl, 14, ":status", "500");
  AssertIndex(&tbl, 15, "accept-charset", "");
  AssertIndex(&tbl, 16, "accept-encoding", "gzip, deflate");
  AssertIndex(&tbl, 17, "accept-language", "");
  AssertIndex(&tbl, 18, "accept-ranges", "");
  AssertIndex(&tbl, 19, "accept", "");
  AssertIndex(&tbl, 20, "access-control-allow-origin", "");
  AssertIndex(&tbl, 21, "age", "");
  AssertIndex(&tbl, 22, "allow", "");
  AssertIndex(&tbl, 23, "authorization", "");
  AssertIndex(&tbl, 24, "cache-control", "");
  AssertIndex(&tbl, 25, "content-disposition", "");
  AssertIndex(&tbl, 26, "content-encoding", "");
  AssertIndex(&tbl, 27, "content-language", "");
  AssertIndex(&tbl, 28, "content-length", "");
  AssertIndex(&tbl, 29, "content-location", "");
  AssertIndex(&tbl, 30, "content-range", "");
  AssertIndex(&tbl, 31, "content-type", "");
  AssertIndex(&tbl, 32, "cookie", "");
  AssertIndex(&tbl, 33, "date", "");
  AssertIndex(&tbl, 34, "etag", "");
  AssertIndex(&tbl, 35, "expect", "");
  AssertIndex(&tbl, 36, "expires", "");
  AssertIndex(&tbl, 37, "from", "");
  AssertIndex(&tbl, 38, "host", "");
  AssertIndex(&tbl, 39, "if-match", "");
  AssertIndex(&tbl, 40, "if-modified-since", "");
  AssertIndex(&tbl, 41, "if-none-match", "");
  AssertIndex(&tbl, 42, "if-range", "");
  AssertIndex(&tbl, 43, "if-unmodified-since", "");
  AssertIndex(&tbl, 44, "last-modified", "");
  AssertIndex(&tbl, 45, "link", "");
  AssertIndex(&tbl, 46, "location", "");
  AssertIndex(&tbl, 47, "max-forwards", "");
  AssertIndex(&tbl, 48, "proxy-authenticate", "");
  AssertIndex(&tbl, 49, "proxy-authorization", "");
  AssertIndex(&tbl, 50, "range", "");
  AssertIndex(&tbl, 51, "referer", "");
  AssertIndex(&tbl, 52, "refresh", "");
  AssertIndex(&tbl, 53, "retry-after", "");
  AssertIndex(&tbl, 54, "server", "");
  AssertIndex(&tbl, 55, "set-cookie", "");
  AssertIndex(&tbl, 56, "strict-transport-security", "");
  AssertIndex(&tbl, 57, "transfer-encoding", "");
  AssertIndex(&tbl, 58, "user-agent", "");
  AssertIndex(&tbl, 59, "vary", "");
  AssertIndex(&tbl, 60, "via", "");
  AssertIndex(&tbl, 61, "www-authenticate", "");
}

TEST(HpackParserTableTest, ManyAdditions) {
  HPackTable tbl;
  int i;

  ExecCtx exec_ctx;

  for (i = 0; i < 100000; i++) {
    std::string key = absl::StrCat("K.", i);
    std::string value = absl::StrCat("VALUE.", i);
    auto key_slice = Slice::FromCopiedString(key);
    auto value_slice = Slice::FromCopiedString(value);
    auto memento = HPackTable::Memento{
        ParsedMetadata<grpc_metadata_batch>(
            ParsedMetadata<grpc_metadata_batch>::FromSlicePair{},
            std::move(key_slice), std::move(value_slice),
            key.length() + value.length() + 32),
        HpackParseResult()};
    ASSERT_TRUE(tbl.Add(std::move(memento)));
    AssertIndex(&tbl, 1 + hpack_constants::kLastStaticEntry, key.c_str(),
                value.c_str());
    if (i) {
      std::string key = absl::StrCat("K.", i - 1);
      std::string value = absl::StrCat("VALUE.", i - 1);
      AssertIndex(&tbl, 2 + hpack_constants::kLastStaticEntry, key.c_str(),
                  value.c_str());
    }
  }
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
