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

#include <google/protobuf/text_format.h>

#include <string>
#include <utility>

#include "fuzztest/fuzztest.h"
#include "src/core/channelz/zviz/entity.h"
#include "src/core/channelz/zviz/html.h"
#include "src/core/channelz/zviz/layout_html.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "test/core/channelz/zviz/environment_fake.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"

namespace grpc_zviz {
namespace {

std::string Render(
    grpc::channelz::v2::Entity entity,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities = {}) {
  html::Container container("body");
  layout::HtmlElement element(container);
  EnvironmentFake env(std::move(entities));
  Format(env, entity, element);
  return container.Render();
}

std::string Render(
    std::string proto,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities = {}) {
  grpc::channelz::v2::Entity entity;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &entity));
  return Render(std::move(entity), std::move(entities));
}

TEST(HtmlIntegrationTest, SimpleEntity) {
  EXPECT_EQ(Render(R"pb(
              kind: "channel" id: 123
            )pb"),
            "<body><div class=\"zviz-banner\">Channel 123</div></body>");
}

void RenderNeverEmpty(
    grpc::channelz::v2::Entity proto,
    absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity> entities = {}) {
  EXPECT_NE(Render(proto, entities), "");
}
FUZZ_TEST(HtmlIntegrationTest, RenderNeverEmpty);

TEST(HtmlIntegrationTest, NestedPropertyList) {
  EXPECT_EQ(Render(R"pb(
              kind: "channel"
              id: 123
              data {
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
                                   grpc.channelz.v2.PropertyList] {
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
              }
            )pb"),
            "<body>"
            "<div class=\"zviz-banner\">Channel 123</div>"
            "<div class=\"zviz-data\">"
            "<div class=\"zviz-heading\">http2</div>"
            "<table class=\"zviz-property-list\">"
            "<tbody>"
            "<tr>"
            "<td><div><div class=\"zviz-key\">ping_callbacks</div></div></td>"
            "<td><div>"
            "<table class=\"zviz-property-list\">"
            "<tbody>"
            "<tr>"
            "<td><div><div class=\"zviz-key\">inflight</div></div></td>"
            "<td><div>"
            "<table class=\"zviz-property-list\">"
            "<tbody>"
            "<tr>"
            "<td><div><div class=\"zviz-key\">num_on_start</div></div></td>"
            "<td><div><div class=\"zviz-value\">0</div></div></td>"
            "</tr>"
            "</tbody>"
            "</table>"
            "</div></td>"
            "</tr>"
            "</tbody>"
            "</table>"
            "</div></td>"
            "</tr>"
            "<tr>"
            "<td><div><div "
            "class=\"zviz-key\">ping_on_rst_stream_percent</div></div></td>"
            "<td><div><div class=\"zviz-value\">1</div></div></td>"
            "</tr>"
            "</tbody>"
            "</table>"
            "</div>"
            "</body>");
}

TEST(HtmlIntegrationTest, ElaborateNestedPropertyList) {
  EXPECT_EQ(Render(R"pb(
              kind: "channel"
              id: 123
              data {
                name: "http2"
                value {
                  [type.googleapis.com/grpc.channelz.v2.PropertyList] {
                    properties {
                      key: "ping_ack_count"
                      value { int64_value: 0 }
                    }
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
                                   grpc.channelz.v2.PropertyList] {
                                    properties {
                                      key: "most_recent_inflight"
                                      value { int64_value: 668833999744423289 }
                                    }
                                    properties {
                                      key: "num_on_ack"
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
                      key: "settings"
                      value {
                        any_value {
                          [type.googleapis.com/grpc.channelz.v2.PropertyGrid] {
                            columns: "local"
                            columns: "sent"
                            columns: "peer"
                            columns: "acked"
                            rows {
                              label: "ENABLE_PUSH"
                              value { bool_value: true }
                              value { bool_value: true }
                              value { bool_value: false }
                              value { bool_value: true }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            )pb"),
            "<body>"
            "<div class=\"zviz-banner\">Channel 123</div>"
            "<div class=\"zviz-data\">"
            "<div class=\"zviz-heading\">http2</div>"
            "<table class=\"zviz-property-list\">"
            "<tbody>"
            "<tr>"
            "<td><div><div class=\"zviz-key\">ping_ack_count</div></div></td>"
            "<td><div><div class=\"zviz-value\">0</div></div></td>"
            "</tr>"
            "<tr>"
            "<td><div><div class=\"zviz-key\">ping_callbacks</div></div></td>"
            "<td><div>"
            "<table class=\"zviz-property-list\">"
            "<tbody>"
            "<tr>"
            "<td><div><div class=\"zviz-key\">inflight</div></div></td>"
            "<td><div>"
            "<table class=\"zviz-property-list\">"
            "<tbody>"
            "<tr>"
            "<td><div><div "
            "class=\"zviz-key\">most_recent_inflight</div></div></td>"
            "<td><div><div "
            "class=\"zviz-value\">668833999744423289</div></div></td>"
            "</tr>"
            "<tr>"
            "<td><div><div class=\"zviz-key\">num_on_ack</div></div></td>"
            "<td><div><div class=\"zviz-value\">0</div></div></td>"
            "</tr>"
            "</tbody>"
            "</table>"
            "</div></td>"
            "</tr>"
            "</tbody>"
            "</table>"
            "</div></td>"
            "</tr>"
            "<tr>"
            "<td><div><div class=\"zviz-key\">settings</div></div></td>"
            "<td><div>"
            "<table class=\"zviz-property-grid\">"
            "<thead>"
            "<tr>"
            "<th><div/></th>"
            "<th><div><div class=\"zviz-key\">local</div></div></th>"
            "<th><div><div class=\"zviz-key\">sent</div></div></th>"
            "<th><div><div class=\"zviz-key\">peer</div></div></th>"
            "<th><div><div class=\"zviz-key\">acked</div></div></th>"
            "</tr>"
            "</thead>"
            "<tbody>"
            "<tr>"
            "<th><div><div class=\"zviz-key\">ENABLE_PUSH</div></div></th>"
            "<td><div><div class=\"zviz-value\">true</div></div></td>"
            "<td><div><div class=\"zviz-value\">true</div></div></td>"
            "<td><div><div class=\"zviz-value\">false</div></div></td>"
            "<td><div><div class=\"zviz-value\">true</div></div></td>"
            "</tr>"
            "</tbody>"
            "</table>"
            "</div></td>"
            "</tr>"
            "</tbody>"
            "</table>"
            "</div>"
            "</body>");
}

}  // namespace
}  // namespace grpc_zviz
