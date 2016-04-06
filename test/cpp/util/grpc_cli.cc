/*

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

/*
  A command line tool to talk to any grpc server.
  Example of talking to grpc interop server:
  1. Prepare request binary file:
    a. create a text file input.txt, containing the following:
        response_size: 10
        payload: {
          body: "hello world"
        }
    b. under grpc/ run
        protoc --proto_path=src/proto/grpc/testing/ \
        --encode=grpc.testing.SimpleRequest
  src/proto/grpc/testing/messages.proto \
        < input.txt > input.bin
  2. Start a server
    make interop_server && bins/opt/interop_server --port=50051
  3. Run the tool
    make grpc_cli && bins/opt/grpc_cli call localhost:50051 \
    /grpc.testing.TestService/UnaryCall --enable_ssl=false \
    --input_binary_file=input.bin --output_binary_file=output.bin
  4. Decode response
    protoc --proto_path=src/proto/grpc/testing/ \
    --decode=grpc.testing.SimpleResponse src/proto/grpc/testing/messages.proto \
    < output.bin > output.txt
  5. Now the text form of response should be in output.txt
  Optionally, metadata can be passed to server via flag --metadata, e.g.
    --metadata="MyHeaderKey1:Value1:MyHeaderKey2:Value2"
*/

#include <fstream>
#include <iostream>
#include <sstream>

#include <gflags/gflags.h>
#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include <grpc++/support/string_ref.h>
#include <grpc/grpc.h>

#include "test/cpp/util/cli_call.h"
#include "test/cpp/util/string_ref_helper.h"
#include "test/cpp/util/test_config.h"

DEFINE_bool(enable_ssl, true, "Whether to use ssl/tls.");
DEFINE_bool(use_auth, false, "Whether to create default google credentials.");
DEFINE_string(input_binary_file, "",
              "Path to input file containing serialized request.");
DEFINE_string(output_binary_file, "output.bin",
              "Path to output file to write serialized response.");
DEFINE_string(metadata, "",
              "Metadata to send to server, in the form of key1:val1:key2:val2");

void ParseMetadataFlag(
    std::multimap<grpc::string, grpc::string>* client_metadata) {
  if (FLAGS_metadata.empty()) {
    return;
  }
  std::vector<grpc::string> fields;
  const char* delim = ":";
  size_t cur, next = -1;
  do {
    cur = next + 1;
    next = FLAGS_metadata.find_first_of(delim, cur);
    fields.push_back(FLAGS_metadata.substr(cur, next - cur));
  } while (next != grpc::string::npos);
  if (fields.size() % 2) {
    std::cout << "Failed to parse metadata flag" << std::endl;
    exit(1);
  }
  for (size_t i = 0; i < fields.size(); i += 2) {
    client_metadata->insert(
        std::pair<grpc::string, grpc::string>(fields[i], fields[i + 1]));
  }
}

template <typename T>
void PrintMetadata(const T& m, const grpc::string& message) {
  if (m.empty()) {
    return;
  }
  std::cout << message << std::endl;
  grpc::string pair;
  for (typename T::const_iterator iter = m.begin(); iter != m.end(); ++iter) {
    pair.clear();
    pair.append(iter->first.data(), iter->first.size());
    pair.append(" : ");
    pair.append(iter->second.data(), iter->second.size());
    std::cout << pair << std::endl;
  }
}

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);

  if (argc < 4 || grpc::string(argv[1]) != "call") {
    std::cout << "Usage: grpc_cli call server_host:port full_method_string\n"
              << "Example: grpc_cli call service.googleapis.com "
              << "/grpc.testing.TestService/UnaryCall "
              << "--input_binary_file=input.bin --output_binary_file=output.bin"
              << std::endl;
  }
  grpc::string server_address(argv[2]);
  // TODO(yangg) basic check of method string
  grpc::string method(argv[3]);

  if (FLAGS_input_binary_file.empty()) {
    std::cout << "Missing --input_binary_file for serialized request."
              << std::endl;
    return 1;
  }
  std::cout << "connecting to " << server_address << std::endl;

  std::ifstream input_file(FLAGS_input_binary_file,
                           std::ios::in | std::ios::binary);
  std::stringstream input_stream;
  input_stream << input_file.rdbuf();

  std::shared_ptr<grpc::ChannelCredentials> creds;
  if (!FLAGS_enable_ssl) {
    creds = grpc::InsecureChannelCredentials();
  } else {
    if (FLAGS_use_auth) {
      creds = grpc::GoogleDefaultCredentials();
    } else {
      creds = grpc::SslCredentials(grpc::SslCredentialsOptions());
    }
  }
  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateChannel(server_address, creds);

  grpc::string response;
  std::multimap<grpc::string, grpc::string> client_metadata;
  std::multimap<grpc::string_ref, grpc::string_ref> server_initial_metadata,
      server_trailing_metadata;
  ParseMetadataFlag(&client_metadata);
  PrintMetadata(client_metadata, "Sending client initial metadata:");
  grpc::Status s = grpc::testing::CliCall::Call(
      channel, method, input_stream.str(), &response, client_metadata,
      &server_initial_metadata, &server_trailing_metadata);
  PrintMetadata(server_initial_metadata,
                "Received initial metadata from server:");
  PrintMetadata(server_trailing_metadata,
                "Received trailing metadata from server:");
  if (s.ok()) {
    std::cout << "Rpc succeeded with OK status" << std::endl;
    if (!response.empty()) {
      std::ofstream output_file(FLAGS_output_binary_file,
                                std::ios::trunc | std::ios::binary);
      output_file << response;
    }
  } else {
    std::cout << "Rpc failed with status code " << s.error_code()
              << " error message " << s.error_message() << std::endl;
  }

  return 0;
}
