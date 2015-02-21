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

#include <grpc/grpc.h>
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "route_guide.pb.h"

using grpc::ChannelArguments;
using grpc::ChannelInterface;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using examples::Point;
using examples::Feature;
using examples::Rectangle;
using examples::RouteSummary;
using examples::RouteNote;
using examples::RouteGuide;

class RouteGuideClient {
 public:
  RouteGuideClient(std::shared_ptr<ChannelInterface> channel)
      : stub_(RouteGuide::NewStub(channel)) {}

  void GetFeature() {
    Point point;
    Feature feature;
    ClientContext context;

    Status status = stub_->GetFeature(&context, point, &feature);
    if (status.IsOk()) {
      std::cout << "GetFeature rpc succeeded." << std::endl;
    } else {
      std::cout << "GetFeature rpc failed." << std::endl;
    }
  }

  void ListFeatures() {
    Rectangle rect;
    Feature feature;
    ClientContext context;

    std::unique_ptr<ClientReader<Feature> > reader(
        stub_->ListFeatures(&context, rect));
    while (reader->Read(&feature)) {
      std::cout << "Received feature" << std::endl;
    }
    Status status = reader->Finish();
    if (status.IsOk()) {
      std::cout << "ListFeatures rpc succeeded." << std::endl;
    } else {
      std::cout << "ListFeatures rpc failed." << std::endl;
    }
  }

  void RecordRoute() {
    Point point;
    RouteSummary summary;
    ClientContext context;

    std::unique_ptr<ClientWriter<Point> > writer(
        stub_->RecordRoute(&context, &summary));
    writer->WritesDone();
    Status status = writer->Finish();
    if (status.IsOk()) {
      std::cout << "RecordRoute rpc succeeded." << std::endl;
    } else {
      std::cout << "RecordRoute rpc failed." << std::endl;
    }
  }

  void RouteChat() {
    RouteNote server_note;
    ClientContext context;

    std::unique_ptr<ClientReaderWriter<RouteNote, RouteNote> > stream(
        stub_->RouteChat(&context));
    stream->WritesDone();
    while (stream->Read(&server_note)) {
    }
    Status status = stream->Finish();
    if (status.IsOk()) {
      std::cout << "RouteChat rpc succeeded." << std::endl;
    } else {
      std::cout << "RouteChat rpc failed." << std::endl;
    }
  }

  void Shutdown() { stub_.reset(); }

 private:
  std::unique_ptr<RouteGuide::Stub> stub_;
};

int main(int argc, char** argv) {
  grpc_init();

  RouteGuideClient guide(
      grpc::CreateChannel("localhost:50051", ChannelArguments()));

  guide.GetFeature();
  guide.ListFeatures();
  guide.RecordRoute();
  guide.RouteChat();

  guide.Shutdown();

  grpc_shutdown();
}
