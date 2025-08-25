// Copyright 2025 gRPC authors.
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

#include "src/core/channelz/zviz/data.h"

#include <google/protobuf/text_format.h>

#include "absl/log/check.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "test/core/channelz/zviz/environment_fake.h"
#include "test/core/channelz/zviz/layout_log.h"

namespace grpc_zviz {
namespace {

void FormatAnyDoesNotCrash(
    google::protobuf::Any value,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities) {
  EnvironmentFake env(std::move(entities));
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  Format(env, value, element);
}
FUZZ_TEST(DataTest, FormatAnyDoesNotCrash);

void FormatDatasDoesNotCrash(
    grpc::channelz::v2::Data data,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities) {
  EnvironmentFake env(std::move(entities));
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  Format(env, data, element);
}
FUZZ_TEST(DataTest, FormatDatasDoesNotCrash);

void ExpectDataTransformsTo(std::string proto, std::string expected) {
  EnvironmentFake env({});
  std::vector<std::string> lines;
  layout::LogElement element("", lines);
  grpc::channelz::v2::Data data;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &data));
  Format(env, {data}, element);
  EXPECT_EQ(expected, absl::StrJoin(lines, "\n")) << "DATA: " << proto;
}

TEST(DataTest, ChangeDetectors) {
  ExpectDataTransformsTo(
      R"pb(
        name: "foo"
        value {
          [type.googleapis.com/grpc.channelz.v2.PropertyList] {
            properties {
              key: "foo"
              value { string_value: "bar" }
            }
          }
        }
      )pb",
      R"([0] DATA foo type.googleapis.com/grpc.channelz.v2.PropertyList
[0] [0] APPEND_TABLE property-list
[0] [0] [0,0] APPEND_COLUMN
[0] [0] [0,0] APPEND_TEXT key foo
[0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] APPEND_TEXT value bar
[0] [0] NEW_ROW)");
}

TEST(DataTest, NestedPropertyList) {
  ExpectDataTransformsTo(
      R"pb(
        name: "top"
        value {
          [type.googleapis.com/grpc.channelz.v2.PropertyList] {
            properties {
              key: "outer"
              value {
                any_value {
                  [type.googleapis.com/grpc.channelz.v2.PropertyList] {
                    properties {
                      key: "inner"
                      value { string_value: "value" }
                    }
                  }
                }
              }
            }
          }
        }
      )pb",
      R"([0] DATA top type.googleapis.com/grpc.channelz.v2.PropertyList
[0] [0] APPEND_TABLE property-list
[0] [0] [0,0] APPEND_COLUMN
[0] [0] [0,0] APPEND_TEXT key outer
[0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] APPEND_TABLE property-list
[0] [0] [1,0] [0] [0,0] APPEND_COLUMN
[0] [0] [1,0] [0] [0,0] APPEND_TEXT key inner
[0] [0] [1,0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] [1,0] APPEND_TEXT value value
[0] [0] [1,0] [0] NEW_ROW
[0] [0] NEW_ROW)");
}

TEST(DataTest, PingCallbacks) {
  ExpectDataTransformsTo(
      R"pb(
        name: "http2"
        value {
          [type.googleapis.com/grpc.channelz.v2.PropertyList] {
            properties {
              key: "ping_callbacks"
              value {
                any_value {
                  [type.googleapis.com/grpc.channelz.v2.PropertyList] {
                    properties {
                      key: "num_on_start"
                      value { int64_value: 0 }
                    }
                  }
                }
              }
            }
            properties {
              key: "settings"
              value {
                any_value {
                  [type.googleapis.com/grpc.channelz.v2.PropertyGrid] {
                    columns: "local"
                  }
                }
              }
            }
          }
        }
      )pb",
      R"([0] DATA http2 type.googleapis.com/grpc.channelz.v2.PropertyList
[0] [0] APPEND_TABLE property-list
[0] [0] [0,0] APPEND_COLUMN
[0] [0] [0,0] APPEND_TEXT key ping_callbacks
[0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] APPEND_TABLE property-list
[0] [0] [1,0] [0] [0,0] APPEND_COLUMN
[0] [0] [1,0] [0] [0,0] APPEND_TEXT key num_on_start
[0] [0] [1,0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] [1,0] APPEND_TEXT value 0
[0] [0] [1,0] [0] NEW_ROW
[0] [0] NEW_ROW
[0] [0] [0,1] APPEND_COLUMN
[0] [0] [0,1] APPEND_TEXT key settings
[0] [0] [1,1] APPEND_COLUMN
[0] [0] [1,1] [0] APPEND_TABLE property-grid
[0] [0] [1,1] [0] [0,0] APPEND_COLUMN
[0] [0] [1,1] [0] [1,0] APPEND_COLUMN
[0] [0] [1,1] [0] [1,0] APPEND_TEXT key local
[0] [0] [1,1] [0] NEW_ROW
[0] [0] NEW_ROW)");
}

TEST(DataTest, ThreeLevelNestedPropertyList) {
  ExpectDataTransformsTo(
      R"pb(
        name: "http2"
        value {
          [type.googleapis.com/grpc.channelz.v2.PropertyList] {
            properties {
              key: "ping_callbacks"
              value {
                any_value {
                  [type.googleapis.com/grpc.channelz.v2.PropertyList] {
                    properties {
                      key: "inflight"
                      value {
                        any_value {
                          [type.googleapis.com/grpc.channelz.v2.PropertyList] {
                            properties {
                              key: "num_on_start"
                              value { int64_value: 0 }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            properties {
              key: "ping_on_rst_stream_percent"
              value { int64_value: 1 }
            }
          }
        }
      )pb",
      R"([0] DATA http2 type.googleapis.com/grpc.channelz.v2.PropertyList
[0] [0] APPEND_TABLE property-list
[0] [0] [0,0] APPEND_COLUMN
[0] [0] [0,0] APPEND_TEXT key ping_callbacks
[0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] APPEND_TABLE property-list
[0] [0] [1,0] [0] [0,0] APPEND_COLUMN
[0] [0] [1,0] [0] [0,0] APPEND_TEXT key inflight
[0] [0] [1,0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] [1,0] [0] APPEND_TABLE property-list
[0] [0] [1,0] [0] [1,0] [0] [0,0] APPEND_COLUMN
[0] [0] [1,0] [0] [1,0] [0] [0,0] APPEND_TEXT key num_on_start
[0] [0] [1,0] [0] [1,0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] [1,0] [0] [1,0] APPEND_TEXT value 0
[0] [0] [1,0] [0] [1,0] [0] NEW_ROW
[0] [0] [1,0] [0] NEW_ROW
[0] [0] NEW_ROW
[0] [0] [0,1] APPEND_COLUMN
[0] [0] [0,1] APPEND_TEXT key ping_on_rst_stream_percent
[0] [0] [1,1] APPEND_COLUMN
[0] [0] [1,1] APPEND_TEXT value 1
[0] [0] NEW_ROW)");
}

TEST(DataTest, ElaborateNestedPropertyList) {
  ExpectDataTransformsTo(
      R"pb(
        name: "http2"
        value {
          [type.googleapis.com/grpc.channelz.v2.PropertyList] {
            properties {
              key: "ping_callbacks"
              value {
                any_value {
                  [type.googleapis.com/grpc.channelz.v2.PropertyList] {
                    properties {
                      key: "inflight"
                      value { string_value: "some_value" }
                    }
                  }
                }
              }
            }
            properties {
              key: "ping_on_rst_stream_percent"
              value { uint64_value: 1 }
            }
          }
        }
      )pb",
      R"([0] DATA http2 type.googleapis.com/grpc.channelz.v2.PropertyList
[0] [0] APPEND_TABLE property-list
[0] [0] [0,0] APPEND_COLUMN
[0] [0] [0,0] APPEND_TEXT key ping_callbacks
[0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] APPEND_TABLE property-list
[0] [0] [1,0] [0] [0,0] APPEND_COLUMN
[0] [0] [1,0] [0] [0,0] APPEND_TEXT key inflight
[0] [0] [1,0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] [1,0] APPEND_TEXT value some_value
[0] [0] [1,0] [0] NEW_ROW
[0] [0] NEW_ROW
[0] [0] [0,1] APPEND_COLUMN
[0] [0] [0,1] APPEND_TEXT key ping_on_rst_stream_percent
[0] [0] [1,1] APPEND_COLUMN
[0] [0] [1,1] APPEND_TEXT value 1
[0] [0] NEW_ROW)");
}

TEST(DataTest, NestedPropertyListContainingPropertyTableThenSibling) {
  ExpectDataTransformsTo(
      R"pb(
        name: "http2"
        value {
          [type.googleapis.com/grpc.channelz.v2.PropertyList] {
            properties {
              key: "ping_callbacks"
              value {
                any_value {
                  [type.googleapis.com/grpc.channelz.v2.PropertyList] {
                    properties {
                      key: "inflight"
                      value {
                        any_value {
                          [type.googleapis.com/
                           grpc.channelz.v2.PropertyTable] {}
                        }
                      }
                    }
                  }
                }
              }
            }
            properties {
              key: "ping_on_rst_stream_percent"
              value { uint64_value: 1 }
            }
          }
        }
      )pb",
      R"([0] DATA http2 type.googleapis.com/grpc.channelz.v2.PropertyList
[0] [0] APPEND_TABLE property-list
[0] [0] [0,0] APPEND_COLUMN
[0] [0] [0,0] APPEND_TEXT key ping_callbacks
[0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] APPEND_TABLE property-list
[0] [0] [1,0] [0] [0,0] APPEND_COLUMN
[0] [0] [1,0] [0] [0,0] APPEND_TEXT key inflight
[0] [0] [1,0] [0] [1,0] APPEND_COLUMN
[0] [0] [1,0] [0] [1,0] [0] APPEND_TABLE property-table
[0] [0] [1,0] [0] NEW_ROW
[0] [0] NEW_ROW
[0] [0] [0,1] APPEND_COLUMN
[0] [0] [0,1] APPEND_TEXT key ping_on_rst_stream_percent
[0] [0] [1,1] APPEND_COLUMN
[0] [0] [1,1] APPEND_TEXT value 1
[0] [0] NEW_ROW)");
}

}  // namespace
}  // namespace grpc_zviz
