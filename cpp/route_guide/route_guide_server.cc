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

#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <grpc/grpc.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "route_guide.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using examples::Point;
using examples::Feature;
using examples::Rectangle;
using examples::RouteSummary;
using examples::RouteNote;
using examples::RouteGuide;

class RouteGuideImpl final : public RouteGuide::Service {
  Status GetFeature(ServerContext* context, const Point* point,
                    Feature* feature) override {
    return Status::OK;
  }
  Status ListFeatures(ServerContext* context, const Rectangle* rectangle,
                      ServerWriter<Feature>* writer) override {
    return Status::OK;
  }
  Status RecordRoute(ServerContext* context, ServerReader<Point>* reader,
                     RouteSummary* summary) override {
    return Status::OK;
  }
  Status RouteChat(ServerContext* context,
                   ServerReaderWriter<RouteNote, RouteNote>* stream) override {
    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  RouteGuideImpl service;

  ServerBuilder builder;
  builder.AddPort(server_address);
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
}

int main(int argc, char** argv) {
  grpc_init();

  RunServer();

  grpc_shutdown();
  return 0;
}
