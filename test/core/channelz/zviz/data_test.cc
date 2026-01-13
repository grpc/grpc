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

#include "fuzztest/fuzztest.h"
#include "test/core/channelz/zviz/environment_fake.h"
#include "test/core/channelz/zviz/layout_log.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"

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

void FormatPromiseDoesNotCrash(grpc::channelz::v2::Promise promise) {
  Format(promise);
}
FUZZ_TEST(DataTest, FormatPromiseDoesNotCrash);

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

void ExpectPromiseTransformsTo(std::string proto, std::string expected) {
  grpc::channelz::v2::Promise promise;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &promise));
  EXPECT_EQ(expected, Format(promise)) << "PROMISE: " << proto;
}

TEST(PromiseTest, Seq) {
  ExpectPromiseTransformsTo(
      R"pb(
        seq_promise {
          steps { factory: "step1" }
          steps { factory: "step2" }
        }
      )pb",
      R"(Seq(
  step1,
  step2,
))");
}

TEST(PromiseTest, SeqWithActive) {
  ExpectPromiseTransformsTo(
      R"pb(
        seq_promise {
          steps { factory: "step1" }
          steps {
            factory: "(lambda at path/to/some_file.cc:123:45)"
            polling_promise { unknown_promise: "active" }
          }
        }
      )pb",
      R"(Seq(
  step1,
🟢some_file.cc:123,
  Unknown(active),
))");
}

TEST(PromiseTest, Join) {
  ExpectPromiseTransformsTo(
      R"pb(
        join_promise {
          branches { factory: "branch1" }
          branches { factory: "branch2" }
        }
      )pb",
      R"(Join(
  branch1,
  branch2,
))");
}

TEST(PromiseTest, JoinWithActiveAndComplete) {
  ExpectPromiseTransformsTo(
      R"pb(
        join_promise {
          branches { factory: "branch1" result: "done" }
          branches {
            factory: "branch2"
            polling_promise { unknown_promise: "active" }
          }
        }
      )pb",
      R"(Join(
✅branch1,
🟢branch2,
  Unknown(active),
))");
}

TEST(PromiseTest, SeqInSeq) {
  ExpectPromiseTransformsTo(
      R"pb(
        seq_promise {
          steps { factory: "step1" }
          steps {
            factory: "step2"
            polling_promise {
              seq_promise {
                steps { factory: "inner_step1" }
                steps { factory: "inner_step2" }
              }
            }
          }
        }
      )pb",
      R"(Seq(
  step1,
🟢step2,
  Seq(
    inner_step1,
    inner_step2,
  ),
))");
}

TEST(PromiseTest, JoinInJoin) {
  ExpectPromiseTransformsTo(
      R"pb(
        join_promise {
          branches { factory: "branch1" }
          branches {
            factory: "branch2"
            polling_promise {
              join_promise {
                branches { factory: "inner_branch1" }
                branches { factory: "inner_branch2" }
              }
            }
          }
        }
      )pb",
      R"(Join(
  branch1,
🟢branch2,
  Join(
    inner_branch1,
    inner_branch2,
  ),
))");
}

TEST(PromiseTest, SeqInSeqWithInnerActive) {
  ExpectPromiseTransformsTo(
      R"pb(
        seq_promise {
          steps { factory: "step1" }
          steps {
            factory: "step2"
            polling_promise {
              seq_promise {
                steps { factory: "inner_step1" }
                steps {
                  factory: "inner_step2"
                  polling_promise { unknown_promise: "active" }
                }
              }
            }
          }
        }
      )pb",
      R"(Seq(
  step1,
🟢step2,
  Seq(
    inner_step1,
🟢  inner_step2,
    Unknown(active),
  ),
))");
}

TEST(PromiseTest, JoinInJoinWithInnerActive) {
  ExpectPromiseTransformsTo(
      R"pb(
        join_promise {
          branches { factory: "branch1" }
          branches {
            factory: "branch2"
            polling_promise {
              join_promise {
                branches { factory: "inner_branch1" }
                branches {
                  factory: "inner_branch2"
                  polling_promise { unknown_promise: "active" }
                }
              }
            }
          }
        }
      )pb",
      R"(Join(
  branch1,
🟢branch2,
  Join(
    inner_branch1,
🟢  inner_branch2,
    Unknown(active),
  ),
))");
}

TEST(PromiseTest, DeeplyNestedSeq) {
  ExpectPromiseTransformsTo(
      R"pb(
        seq_promise {
          steps {
            factory: "step1"
            polling_promise {
              seq_promise {
                steps {
                  factory: "inner_step1"
                  polling_promise {
                    seq_promise {
                      steps {
                        factory: "inner_inner_step1"
                        polling_promise { unknown_promise: "active" }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      )pb",
      R"(Seq(
🟢step1,
  Seq(
🟢  inner_step1,
    Seq(
🟢    inner_inner_step1,
      Unknown(active),
    ),
  ),
))");
}

TEST(PromiseTest, DeeplyNestedJoinWithResult) {
  ExpectPromiseTransformsTo(
      R"pb(
        join_promise {
          branches {
            factory: "branch1"
            polling_promise {
              join_promise {
                branches { factory: "inner_branch1" result: "done" }
                branches {
                  factory: "inner_branch2"
                  polling_promise {
                    join_promise {
                      branches {
                        factory: "inner_inner_branch1"
                        polling_promise { unknown_promise: "active" }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      )pb",
      R"(Join(
🟢branch1,
  Join(
✅  inner_branch1,
🟢  inner_branch2,
    Join(
🟢    inner_inner_branch1,
      Unknown(active),
    ),
  ),
))");
}

TEST(PromiseTest, DeeplyNestedJoinWithInnermostResult) {
  ExpectPromiseTransformsTo(
      R"pb(
        join_promise {
          branches {
            factory: "branch1"
            polling_promise {
              join_promise {
                branches {
                  factory: "inner_branch1"
                  polling_promise { unknown_promise: "active" }
                }
                branches {
                  factory: "inner_branch2"
                  polling_promise {
                    join_promise {
                      branches { factory: "inner_inner_branch1" result: "done" }
                      branches {
                        factory: "inner_inner_branch2"
                        polling_promise { unknown_promise: "active" }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      )pb",
      R"(Join(
🟢branch1,
  Join(
🟢  inner_branch1,
    Unknown(active),
🟢  inner_branch2,
    Join(
✅    inner_inner_branch1,
🟢    inner_inner_branch2,
      Unknown(active),
    ),
  ),
))");
}

TEST(PromiseTest, DeeplyNestedJoin) {
  ExpectPromiseTransformsTo(
      R"pb(
        join_promise {
          branches {
            factory: "branch1"
            polling_promise {
              join_promise {
                branches {
                  factory: "inner_branch1"
                  polling_promise {
                    join_promise {
                      branches {
                        factory: "inner_inner_branch1"
                        polling_promise { unknown_promise: "active" }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      )pb",
      R"(Join(
🟢branch1,
  Join(
🟢  inner_branch1,
    Join(
🟢    inner_inner_branch1,
      Unknown(active),
    ),
  ),
))");
}

TEST(PromiseTest, Map) {
  ExpectPromiseTransformsTo(
      R"pb(
        map_promise {
          promise { unknown_promise: "the_promise" }
          map_fn: "the_map_fn"
        }
      )pb",
      R"(Map(
  Unknown(the_promise),
  the_map_fn
))");
}

TEST(PromiseTest, TrySeq) {
  ExpectPromiseTransformsTo(
      R"pb(
        seq_promise {
          kind: TRY
          steps { factory: "step1" }
          steps { factory: "step2" }
        }
      )pb",
      R"(TrySeq(
  step1,
  step2,
))");
}

TEST(PromiseTest, TryJoin) {
  ExpectPromiseTransformsTo(
      R"pb(
        join_promise {
          kind: TRY
          branches { factory: "branch1" }
          branches { factory: "branch2" }
        }
      )pb",
      R"(TryJoin(
  branch1,
  branch2,
))");
}

TEST(PromiseTest, Loop) {
  ExpectPromiseTransformsTo(
      R"pb(
        loop_promise {
          loop_factory: "loop_factory"
          promise { unknown_promise: "body" }
        }
      )pb",
      R"(Loop(
  loop_factory,
  Unknown(body)
))");
}

TEST(PromiseTest, LoopWithRepeatedPromiseFactory) {
  ExpectPromiseTransformsTo(
      R"pb(
        loop_promise {
          loop_factory: "grpc_core::promise_detail::RepeatedPromiseFactory<void, (lambda at src/core/ext/transport/chaotic_good_legacy/data_endpoints.cc:202:15)>"
          promise { unknown_promise: "body" }
        }
      )pb",
      R"(Loop(
  data_endpoints.cc:202,
  Unknown(body)
))");
}

TEST(PromiseTest, If) {
  ExpectPromiseTransformsTo(
      R"pb(
        if_promise {
          condition: true
          true_factory: "true_factory"
          false_factory: "false_factory"
          promise { unknown_promise: "branch" }
        }
      )pb",
      R"(If(true, true_factory, false_factory,
  Unknown(branch)
))");
}

TEST(PromiseTest, Race) {
  ExpectPromiseTransformsTo(
      R"pb(
        race_promise {
          children { unknown_promise: "child1" }
          children { unknown_promise: "child2" }
        }
      )pb",
      R"(Race(
  Unknown(child1),
  Unknown(child2),
))");
}

TEST(PromiseTest, UnknownPromiseLambda) {
  ExpectPromiseTransformsTo(
      R"pb(
        unknown_promise: "(lambda at path/to/some_file.cc:123:45)"
      )pb",
      R"(some_file.cc:123)");
}

TEST(PromiseTest, UnknownPromisePlain) {
  ExpectPromiseTransformsTo(
      R"pb(
        unknown_promise: "plain"
      )pb",
      R"(Unknown(plain))");
}

TEST(PromiseTest, Custom) {
  ExpectPromiseTransformsTo(
      R"pb(
        custom_promise {
          type: "MyCustomPromise"
          properties {
            properties {
              key: "foo"
              value { string_value: "bar" }
            }
          }
        }
      )pb",
      R"(MyCustomPromise foo:bar)");
}

TEST(PromiseTest, CustomMultiline) {
  ExpectPromiseTransformsTo(
      R"pb(
        custom_promise {
          type: "MyCustomPromise"
          properties {
            properties {
              key: "foo"
              value { string_value: "bar" }
            }
            properties {
              key: "baz"
              value {
                string_value: "this is a very long string that should cause multiline formatting"
              }
            }
          }
        }
      )pb",
      R"(MyCustomPromise {
    foo: bar
    baz: this is a very long string that should cause multiline formatting
  })");
}

TEST(PromiseTest, ForEachReader) {
  ExpectPromiseTransformsTo(
      R"pb(
        for_each_promise {
          reader_factory: "reader"
          action_factory: "action"
          reader_promise { unknown_promise: "reader_promise" }
        }
      )pb",
      R"(ForEach(
  reader, action,
  Unknown(reader_promise)
))");
}

TEST(PromiseTest, ForEachAction) {
  ExpectPromiseTransformsTo(
      R"pb(
        for_each_promise {
          reader_factory: "reader"
          action_factory: "action"
          action_promise { unknown_promise: "action_promise" }
        }
      )pb",
      R"(ForEach(
  reader, action,
  Unknown(action_promise)
))");
}

TEST(DataTest, PromiseInData) {
  ExpectDataTransformsTo(
      R"pb(
        name: "some_promise"
        value {
          [type.googleapis.com/grpc.channelz.v2.Promise] {
            map_promise {
              promise { unknown_promise: "the_promise" }
              map_fn: "the_map_fn"
            }
          }
        }
      )pb",
      R"([0] DATA some_promise type.googleapis.com/grpc.channelz.v2.Promise
[0] APPEND_TEXT data Map(
  Unknown(the_promise),
  the_map_fn
))");
}

}  // namespace
}  // namespace grpc_zviz
