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

#include "src/core/channelz/zviz/style.h"

#include <string>

namespace grpc_zviz {

// Returns a std::string because this is passed to a handler that may outlive
// the current library.
std::string DefaultStyle() {
  return R"css(
body {
  font-family: sans-serif;
  background-color: #f8f8f8;
  color: #333;
}
.zviz-banner {
  background-color: #e0e0e0;
  padding: 10px;
  font-size: 1.2em;
  font-weight: bold;
}
.zviz-heading {
  font-weight: bold;
  margin-top: 10px;
  margin-bottom: 5px;
  background-color: #eee;
  padding: 5px;
  border-radius: 3px;
}
.zviz-property-list > .zviz-heading,
.zviz-property-table > .zviz-heading,
.zviz-property-grid > .zviz-heading {
  background-color: transparent;
  padding: 0;
  border-radius: 0;
  margin-top: 0;
}
.zviz-data {
}
.zviz-key {
  font-weight: bold;
  padding-right: 1em;
  vertical-align: top;
}
table {
  border-collapse: collapse;
  width: auto;
  margin-bottom: 10px;
}
.zviz-property-list td {
  vertical-align: top;
}
.zviz-property-list > tbody > tr {
  border-bottom: 1px solid #ccc;
}
.zviz-property-list > tbody > tr:last-child {
  border-bottom: none;
}
th, td {
  padding: 8px;
  text-align: left;
}
.zviz-property-table th,
.zviz-property-table td,
.zviz-property-grid th,
.zviz-property-grid td {
  padding: 8px 4px;
}
thead {
  background-color: #f2f2f2;
  border-bottom: 2px solid #ccc;
}
)css";
}

}  // namespace grpc_zviz
