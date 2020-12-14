/*
 *
 * Copyright 2020 gRPC authors.
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

#include <iostream>
#include <string>

#include "grpcpp/grpcpp.h"
#include "google/bigtable/v2/bigtable.grpc.pb.h"

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

ABSL_FLAG(std::string, project_id, "project_id", "PROJECT_ID");
ABSL_FLAG(std::string, instance, "instance", "INSTANCE");
ABSL_FLAG(std::string, table, "table", "TABLE");
ABSL_FLAG(std::string, row_key, "row_key_1", "ROW_KEY");
ABSL_FLAG(std::string, family_name, "cf1", "FAMILY_NAME");
ABSL_FLAG(std::string, column_qualifier, "column_qualifier_1", "COLUMN_QUALIFIER");
ABSL_FLAG(std::string, value, "value_1", "VALUE");
ABSL_FLAG(bool, no_exit, false, "NO_EXIT");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  std::string TABLE_NAME = "projects/" + absl::GetFlag(FLAGS_project_id) + "/instances/" + absl::GetFlag(FLAGS_instance) + "/tables/" + absl::GetFlag(FLAGS_table);
  auto creds = grpc::GoogleDefaultCredentials();
  auto server_name = "bigtable.googleapis.com";
  auto channel = grpc::CreateChannel(server_name, creds);
  std::unique_ptr<google::bigtable::v2::Bigtable::Stub>
      stub(google::bigtable::v2::Bigtable::NewStub(channel));
  google::bigtable::v2::MutateRowRequest req;
  req.set_table_name(TABLE_NAME);
  req.set_row_key(absl::GetFlag(FLAGS_row_key));
  auto setCell = req.add_mutations()->mutable_set_cell();
  setCell->set_family_name(absl::GetFlag(FLAGS_family_name));
  setCell->set_column_qualifier(absl::GetFlag(FLAGS_column_qualifier));
  setCell->set_value(absl::GetFlag(FLAGS_value));
  ClientContext context;
  google::bigtable::v2::MutateRowResponse resp;
  auto status = stub->MutateRow(&context, req, &resp);
  if (!status.ok()) {
    std::cerr << "Error in MutateRow() request: " << status.error_message()
              << " [" << status.error_code() << "] " << status.error_details()
              << std::endl;
  } else {
    std::cout << "Stored successfully!" << std::endl;
  }
  if (absl::GetFlag(FLAGS_no_exit)) {
    while (true) { }
  }
  return 0;
}