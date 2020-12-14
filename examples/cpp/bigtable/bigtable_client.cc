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

#include <grpcpp/grpcpp.h>

#include "google/bigtable/v2/bigtable.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

#define TABLE_NAME "projects/cicpclientproj/instances/grpc-3pi-test/tables/grpc-table"
#define ROW_KEY "row_key_1"
#define FAMILY_NAME "cf1"
#define COLUMN_QUALIFIER "column_qualifier"
#define VALUE "value"

int main(int argc, char** argv) {
  auto creds = grpc::GoogleDefaultCredentials();
  auto server_name = "bigtable.googleapis.com";
  auto channel = grpc::CreateChannel(server_name, creds);
  std::unique_ptr<google::bigtable::v2::Bigtable::Stub>
      stub(google::bigtable::v2::Bigtable::NewStub(channel));
  google::bigtable::v2::MutateRowRequest req;
  req.set_table_name(TABLE_NAME);
  req.set_row_key(ROW_KEY);
  auto setCell = req.add_mutations()->mutable_set_cell();
  setCell->set_family_name(FAMILY_NAME);
  setCell->set_column_qualifier(COLUMN_QUALIFIER);
  setCell->set_value(VALUE);
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
  return 0;
}
