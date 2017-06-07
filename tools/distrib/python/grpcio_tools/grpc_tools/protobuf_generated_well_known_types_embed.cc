// Copyright 2017 gRPC authors.
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
// limitations under the License.\n"
  "\n"
  "/* This code will be inserted into generated code for\n"
  " * google/protobuf/timestamp.proto. */\n"
  "\n"
  "/**\n"
  " * Returns a JavaScript 'Date' object corresponding to this Timestamp.\n"
  " * @return {!Date}\n"
  " */\n"
  "proto.google.protobuf.Timestamp.prototype.toDate = function() {\n"
  "  var seconds = this.getSeconds();\n"
  "  var nanos = this.getNanos();\n"
  "\n"
  "  return new Date((seconds * 1000) + (nanos / 1000000));\n"
  "};\n"
  "\n"
  "\n"
  "/**\n"
  " * Sets the value of this Timestamp object to be the given Date.\n"
  " * @param {!Date} value The value to set.\n"
  " */\n"
  "proto.google.protobuf.Timestamp.prototype.fromDate = function(value) {\n"
  "  var millis = value.getTime();\n"
  "  this.setSeconds(Math.floor(value.getTime() / 1000));\n"
  "  this.setNanos(value.getMilliseconds() * 1000000);\n"
  "};\n"
},
  {NULL, NULL}  // Terminate the list.
};
