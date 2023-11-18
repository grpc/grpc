//
// Copyright 2022 gRPC authors.
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
//

// TODO(roth): Add the following tests:
// - tests for DumpClientConfigBinary()
// - tests for load-reporting APIs?  (or maybe move those out of XdsClient?)

#include "src/core/ext/xds/xds_client.h"

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/proto/grpc/testing/xds/v3/discovery.pb.h"
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"
#include "test/core/xds/xds_client_test_lib.h"
#include "test/core/xds/xds_transport_fake.h"

// IWYU pragma: no_include <google/protobuf/message.h>
// IWYU pragma: no_include <google/protobuf/stubs/status.h>
// IWYU pragma: no_include <google/protobuf/unknown_field_set.h>
// IWYU pragma: no_include <google/protobuf/util/json_util.h>
// IWYU pragma: no_include "google/protobuf/json/json.h"
// IWYU pragma: no_include "google/protobuf/util/json_util.h"

namespace grpc_core {
namespace testing {
namespace {

using XdsClientTest = XdsClientTestBase;

TEST_F(XdsClientTest, BasicWatch) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, UpdateFromServer) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Server sends an updated version of the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, MultipleWatchersForSameResource) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a second watcher for the same resource.
  auto watcher2 = StartFooWatch("foo1");
  // This watcher should get an immediate notification, because the
  // resource is already cached.
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Server should not have seen another request from the client.
  ASSERT_FALSE(stream->HaveMessageFromClient());
  // Server sends an updated version of the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should deliver the response to both watchers.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel one of the watchers.
  CancelFooWatch(watcher.get(), "foo1");
  // The server should not see any new request.
  ASSERT_FALSE(WaitForRequest(stream.get()));
  // Now cancel the second watcher.
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, SubscribeToMultipleResources) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for "foo2".
  auto watcher2 = StartFooWatch("foo2");
  // XdsClient should have sent a subscription request on the ADS stream.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo2", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo2");
  EXPECT_EQ(resource->value, 7);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  // XdsClient should send an unsubscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(), /*resource_names=*/{"foo2"});
  // Now cancel watch for "foo2".
  CancelFooWatch(watcher2.get(), "foo2");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, UpdateContainsOnlyChangedResource) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for "foo2".
  auto watcher2 = StartFooWatch("foo2");
  // XdsClient should have sent a subscription request on the ADS stream.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo2", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo2");
  EXPECT_EQ(resource->value, 7);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Server sends an update for "foo1".  The response does not contain "foo2".
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("C")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  // XdsClient should send an unsubscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(), /*resource_names=*/{"foo2"});
  // Now cancel watch for "foo2".
  CancelFooWatch(watcher2.get(), "foo2");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceValidationFailure) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response containing an invalid resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo1\",\"value\":[]}")
          .Serialize());
  // XdsClient should deliver an error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "invalid resource: INVALID_ARGUMENT: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // XdsClient should NACK the update.
  // Note that version_info is not populated in the request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(
      *request, XdsFooResourceType::Get()->type_url(),
      /*version_info=*/"", /*response_nonce=*/"A",
      // error_detail=
      absl::InvalidArgumentError(
          "xDS response validation errors: ["
          "resource index 0: foo1: INVALID_ARGUMENT: errors validating JSON: "
          "[field:value error:is not a number]]"),
      /*resource_names=*/{"foo1"});
  // Start a second watch for the same resource.  It should immediately
  // receive the same error.
  auto watcher2 = StartFooWatch("foo1");
  error = watcher2->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "invalid resource: INVALID_ARGUMENT: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // Now server sends an updated version of the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 9))
          .Serialize());
  // XdsClient should deliver the response to both watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 9);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceValidationFailureMultipleResources) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Before the server responds, add a watch for another resource.
  auto watcher2 = StartFooWatch("foo2");
  // Client should send another request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // Add a watch for a third resource.
  auto watcher3 = StartFooWatch("foo3");
  // Client should send another request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2", "foo3"});
  // Add a watch for a fourth resource.
  auto watcher4 = StartFooWatch("foo4");
  // Client should send another request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2", "foo3", "foo4"});
  // Server sends a response containing three invalid resources and one
  // valid resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          // foo1: JSON parsing succeeds, so we know the resource name,
          // but validation fails.
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo1\",\"value\":[]}")
          // foo2: JSON parsing fails, and not wrapped in a Resource
          // wrapper, so we don't actually know the resource's name.
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo2,\"value\":6}")
          // Empty resource.  Will be included in NACK but will not
          // affect any watchers.
          .AddEmptyResource()
          // Invalid resource wrapper.  Will be included in NACK but
          // will not affect any watchers.
          .AddInvalidResourceWrapper()
          // foo3: JSON parsing fails, but it is wrapped in a Resource
          // wrapper, so we do know the resource's name.
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo3,\"value\":6}",
                              /*resource_wrapper_name=*/"foo3")
          // foo4: valid resource.
          .AddFooResource(XdsFooResource("foo4", 5))
          .Serialize());
  // XdsClient should deliver an error to the watchers for foo1 and foo3.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "invalid resource: INVALID_ARGUMENT: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  error = watcher3->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "invalid resource: INVALID_ARGUMENT: JSON parsing failed: "
            "[JSON parse error at index 15] (node ID:xds_client_test)")
      << *error;
  // It cannot delivery an error for foo2, because the client doesn't know
  // that that resource in the response was actually supposed to be foo2.
  EXPECT_FALSE(watcher2->HasEvent());
  // It will delivery a valid resource update for foo4.
  auto resource = watcher4->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo4");
  EXPECT_EQ(resource->value, 5);
  // XdsClient should NACK the update.
  // There was one good resource, so the version will be updated.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               // error_detail=
               absl::InvalidArgumentError(absl::StrCat(
                   "xDS response validation errors: ["
                   // foo1
                   "resource index 0: foo1: "
                   "INVALID_ARGUMENT: errors validating JSON: "
                   "[field:value error:is not a number]; "
                   // foo2 (name not known)
                   "resource index 1: INVALID_ARGUMENT: JSON parsing failed: "
                   "[JSON parse error at index 15]; "
                   // empty resource
                   "resource index 2: incorrect resource type \"\" "
                   "(should be \"",
                   XdsFooResourceType::Get()->type_url(),
                   "\"); "
                   // invalid resource wrapper
                   "resource index 3: Can't decode Resource proto wrapper; "
                   // foo3
                   "resource index 4: foo3: "
                   "INVALID_ARGUMENT: JSON parsing failed: "
                   "[JSON parse error at index 15]]")),
               /*resource_names=*/{"foo1", "foo2", "foo3", "foo4"});
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher2.get(), "foo2", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher3.get(), "foo3", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher4.get(), "foo4");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceValidationFailureForCachedResource) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Send an update containing an invalid resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddInvalidResource(XdsFooResourceType::Get()->type_url(),
                              "{\"name\":\"foo1\",\"value\":[]}")
          .Serialize());
  // XdsClient should deliver an error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "invalid resource: INVALID_ARGUMENT: errors validating JSON: "
            "[field:value error:is not a number] (node ID:xds_client_test)")
      << *error;
  // XdsClient should NACK the update.
  // Note that version_info is set to the previous version in this request,
  // because there were no valid resources in it.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(
      *request, XdsFooResourceType::Get()->type_url(),
      /*version_info=*/"1", /*response_nonce=*/"B",
      // error_detail=
      absl::InvalidArgumentError(
          "xDS response validation errors: ["
          "resource index 0: foo1: INVALID_ARGUMENT: errors validating JSON: "
          "[field:value error:is not a number]]"),
      /*resource_names=*/{"foo1"});
  // Start a second watcher for the same resource.  Even though the last
  // update was a NACK, we should still deliver the cached resource to
  // the watcher.
  // TODO(roth): Consider what the right behavior is here.  It seems
  // inconsistent that the watcher sees the error if it had started
  // before the error was seen but does not if it was started afterwards.
  // One option is to not send errors at all for already-cached resources;
  // another option is to send the errors even for newly started watchers.
  auto watcher2 = StartFooWatch("foo1");
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, WildcardCapableResponseWithEmptyResource) {
  InitXdsClient();
  // Start a watch for "wc1".
  auto watcher = StartWildcardCapableWatch("wc1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server sends a response containing the requested resources plus an
  // empty resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 6))
          .AddEmptyResource()
          .Serialize());
  // XdsClient will delivery a valid resource update for wc1.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should NACK the update.
  // There was one good resource, so the version will be updated.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               // error_detail=
               absl::InvalidArgumentError(absl::StrCat(
                   "xDS response validation errors: ["
                   "resource index 1: incorrect resource type \"\" "
                   "(should be \"",
                   XdsWildcardCapableResourceType::Get()->type_url(), "\")]")),
               /*resource_names=*/{"wc1"});
  // Cancel watch.
  CancelWildcardCapableWatch(watcher.get(), "wc1");
  EXPECT_TRUE(stream->Orphaned());
}

// This tests resource removal triggered by the server when using a
// resource type that requires all resources to be present in every
// response, similar to LDS and CDS.
TEST_F(XdsClientTest, ResourceDeletion) {
  InitXdsClient();
  // Start a watch for "wc1".
  auto watcher = StartWildcardCapableWatch("wc1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server now sends a response without the resource, thus indicating
  // it's been deleted.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .Serialize());
  // Watcher should see the does-not-exist event.
  EXPECT_TRUE(watcher->WaitForDoesNotExist(absl::Seconds(1)));
  // Start a new watcher for the same resource.  It should immediately
  // receive the same does-not-exist notification.
  auto watcher2 = StartWildcardCapableWatch("wc1");
  EXPECT_TRUE(watcher2->WaitForDoesNotExist(absl::Seconds(1)));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server sends the resource again.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("3")
          .set_nonce("C")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"3", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Cancel watch.
  CancelWildcardCapableWatch(watcher.get(), "wc1");
  CancelWildcardCapableWatch(watcher2.get(), "wc1");
  EXPECT_TRUE(stream->Orphaned());
}

// This tests that when we ignore resource deletions from the server
// when configured to do so.
TEST_F(XdsClientTest, ResourceDeletionIgnoredWhenConfigured) {
  InitXdsClient(FakeXdsBootstrap::Builder().set_ignore_resource_deletion(true));
  // Start a watch for "wc1".
  auto watcher = StartWildcardCapableWatch("wc1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server now sends a response without the resource, thus indicating
  // it's been deleted.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .Serialize());
  // Watcher should not see any update, since we should have ignored the
  // deletion.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(1)));
  // Start a new watcher for the same resource.  It should immediately
  // receive the cached resource.
  auto watcher2 = StartWildcardCapableWatch("wc1");
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Server sends a new value for the resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsWildcardCapableResourceType::Get()->type_url())
          .set_version_info("3")
          .set_nonce("C")
          .AddWildcardCapableResource(XdsWildcardCapableResource("wc1", 7))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "wc1");
  EXPECT_EQ(resource->value, 7);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsWildcardCapableResourceType::Get()->type_url(),
               /*version_info=*/"3", /*response_nonce=*/"C",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"wc1"});
  // Cancel watch.
  CancelWildcardCapableWatch(watcher.get(), "wc1");
  CancelWildcardCapableWatch(watcher2.get(), "wc1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, StreamClosedByServer) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Now server closes the stream.
  stream->MaybeSendStatusToClient(absl::OkStatus());
  // XdsClient should NOT report error to watcher, because we saw a
  // response on the stream before it failed.
  // Stream should be orphaned.
  EXPECT_TRUE(stream->Orphaned());
  // XdsClient should create a new stream.
  stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient sends a subscription request.
  // Note that the version persists from the previous stream, but the
  // nonce does not.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Before the server resends the resource, start a new watcher for the
  // same resource.  This watcher should immediately receive the cached
  // resource.
  auto watcher2 = StartFooWatch("foo1");
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // Server now sends the requested resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // Watcher does NOT get an update, since the resource has not changed.
  EXPECT_FALSE(watcher->WaitForNextResource());
  // XdsClient sends an ACK.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watcher.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, StreamClosedByServerWithoutSeeingResponse) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server closes the stream without sending a response.
  stream->MaybeSendStatusToClient(absl::UnavailableError("ugh"));
  // XdsClient should report an error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: xDS call failed "
            "with no responses received; status: UNAVAILABLE: ugh "
            "(node ID:xds_client_test)")
      << *error;
  // XdsClient should create a new stream.
  stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient sends a subscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server now sends the requested resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // Watcher gets the resource.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient sends an ACK.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watcher.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ConnectionFails) {
  // Lower resources-does-not-exist timeout, to make sure that we're not
  // triggering that here.
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(3));
  // Tell transport to let us manually trigger completion of the
  // send_message ops to XdsClient.
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Transport reports connection failure.
  TriggerConnectionFailure(xds_client_->bootstrap().server(),
                           absl::UnavailableError("connection failed"));
  // XdsClient should report an error to the watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: "
            "connection failed (node ID:xds_client_test)")
      << *error;
  // We should not see a resource-does-not-exist event, because the
  // timer should not be running while the channel is disconnected.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(4)));
  // Start a new watch.  This watcher should be given the same error,
  // since we have not yet recovered.
  auto watcher2 = StartFooWatch("foo1");
  error = watcher2->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: "
            "connection failed (node ID:xds_client_test)")
      << *error;
  // Second watcher should not see resource-does-not-exist either.
  EXPECT_FALSE(watcher2->HasEvent());
  // The ADS stream uses wait_for_ready inside the XdsTransport interface,
  // so when the channel reconnects, the already-started stream will proceed.
  stream->CompleteSendMessageFromClient();
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  stream->CompleteSendMessageFromClient();
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceDoesNotExistUponTimeout) {
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(1));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Do not send a response, but wait for the resource to be reported as
  // not existing.
  EXPECT_TRUE(watcher->WaitForDoesNotExist(absl::Seconds(5)));
  // Start a new watcher for the same resource.  It should immediately
  // receive the same does-not-exist notification.
  auto watcher2 = StartFooWatch("foo1");
  EXPECT_TRUE(watcher2->WaitForDoesNotExist(absl::Seconds(1)));
  // Now server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  CancelFooWatch(watcher2.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceDoesNotExistAfterStreamRestart) {
  // Lower resources-does-not-exist timeout so test finishes faster.
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(3));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Stream fails.
  stream->MaybeSendStatusToClient(absl::UnavailableError("ugh"));
  // XdsClient should report error to watcher.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server default_xds_server: xDS call failed "
            "with no responses received; status: UNAVAILABLE: ugh "
            "(node ID:xds_client_test)")
      << *error;
  // XdsClient should create a new stream.
  stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient sends a subscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server does NOT send a response immediately.
  // Client should receive a resource does-not-exist.
  ASSERT_TRUE(watcher->WaitForDoesNotExist(absl::Seconds(4)));
  // Server now sends the requested resource.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // The resource is delivered to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient sends an ACK.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watcher.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, DoesNotExistTimerNotStartedUntilSendCompletes) {
  // Lower resources-does-not-exist timeout, to make sure that we're not
  // triggering that here.
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(3));
  // Tell transport to let us manually trigger completion of the
  // send_message ops to XdsClient.
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server does NOT send a response.
  // We should not see a resource-does-not-exist event, because the
  // timer should not be running while the channel is disconnected.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(4)));
  // The ADS stream uses wait_for_ready inside the XdsTransport interface,
  // so when the channel connects, the already-started stream will proceed.
  stream->CompleteSendMessageFromClient();
  // Server does NOT send a response.
  // Watcher should see a does-not-exist event.
  EXPECT_TRUE(watcher->WaitForDoesNotExist(absl::Seconds(4)));
  // Now server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  stream->CompleteSendMessageFromClient();
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

// In https://github.com/grpc/grpc/issues/29583, we ran into a case
// where we wound up starting a timer after we had already received the
// resource, thus incorrectly reporting the resource as not existing.
// This happened when unsubscribing and then resubscribing to the same
// resource a send_message op was already in flight and then receiving an
// update containing that resource.
TEST_F(XdsClientTest,
       ResourceDoesNotExistUnsubscribeAndResubscribeWhileSendMessagePending) {
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(1));
  // Tell transport to let us manually trigger completion of the
  // send_message ops to XdsClient.
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  stream->CompleteSendMessageFromClient();
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watchers.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  stream->CompleteSendMessageFromClient();
  // Start a watch for a second resource.
  auto watcher2 = StartFooWatch("foo2");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher2->HasEvent());
  // XdsClient sends a request to subscribe to the new resource.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  // NOTE: We do NOT yet tell the XdsClient that the send_message op is
  // complete.
  // Unsubscribe from foo1 and then re-subscribe to it.
  CancelFooWatch(watcher.get(), "foo1");
  watcher = StartFooWatch("foo1");
  // Now send a response from the server containing both foo1 and foo2.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("B")
          .AddFooResource(XdsFooResource("foo1", 6))
          .AddFooResource(XdsFooResource("foo2", 7))
          .Serialize());
  // The watcher for foo1 will receive an update even if the resource
  // has not changed, since the previous value was removed from the
  // cache when we unsubscribed.
  resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // For foo2, the watcher should receive notification for the new resource.
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo2");
  EXPECT_EQ(resource->value, 7);
  // Now we finally tell XdsClient that its previous send_message op is
  // complete.
  stream->CompleteSendMessageFromClient();
  // XdsClient should send an ACK with the updated subscription list
  // (which happens to be identical to the old list), and it should not
  // restart the does-not-exist timer.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", "foo2"});
  stream->CompleteSendMessageFromClient();
  // Make sure the watcher for foo1 does not see a does-not-exist event.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(5)));
  // Cancel watches.
  CancelFooWatch(watcher.get(), "foo1", /*delay_unsubscription=*/true);
  CancelFooWatch(watcher2.get(), "foo2");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, DoNotSendDoesNotExistForCachedResource) {
  // Lower resources-does-not-exist timeout, to make sure that we're not
  // triggering that here.
  InitXdsClient(FakeXdsBootstrap::Builder(), Duration::Seconds(3));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Stream fails because of transport disconnection.
  stream->MaybeSendStatusToClient(absl::UnavailableError("connection failed"));
  // XdsClient should NOT report error to watcher, because we saw a
  // response on the stream before it failed.
  // XdsClient creates a new stream.
  stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Server does NOT send a response.
  // We should not see a resource-does-not-exist event, because the
  // resource was already cached, so the server can optimize by not
  // resending it.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(4)));
  // Now server sends a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // Watcher will not see any update, since the resource is unchanged.
  EXPECT_TRUE(watcher->ExpectNoEvent(absl::Seconds(1)));
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, ResourceWrappedInResourceMessage) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response with the resource wrapped in a Resource message.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6),
                          /*in_resource_wrapper=*/true)
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Cancel watch.
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, MultipleResourceTypes) {
  InitXdsClient();
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for "bar1".
  auto watcher2 = StartBarWatch("bar1");
  // XdsClient should have sent a subscription request on the ADS stream.
  // Note that version and nonce here do NOT use the values for Foo,
  // since each resource type has its own state.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsBarResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"bar1"});
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsBarResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddBarResource(XdsBarResource("bar1", "whee"))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource2 = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource2->name, "bar1");
  EXPECT_EQ(resource2->value, "whee");
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsBarResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"bar1"});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  // XdsClient should send an unsubscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(), /*resource_names=*/{});
  // Now cancel watch for "bar1".
  CancelBarWatch(watcher2.get(), "bar1");
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, Federation) {
  constexpr char kAuthority[] = "xds.example.com";
  const std::string kXdstpResourceName = absl::StrCat(
      "xdstp://", kAuthority, "/", XdsFooResource::TypeUrl(), "/foo2");
  FakeXdsBootstrap::FakeXdsServer authority_server;
  authority_server.set_server_uri("other_xds_server");
  FakeXdsBootstrap::FakeAuthority authority;
  authority.set_server(authority_server);
  InitXdsClient(
      FakeXdsBootstrap::Builder().AddAuthority(kAuthority, authority));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream to the top-level xDS server.
  auto stream = WaitForAdsStream(xds_client_->bootstrap().server());
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for the xdstp resource name.
  auto watcher2 = StartFooWatch(kXdstpResourceName);
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher2->HasEvent());
  // XdsClient will create a new stream to the server for this authority.
  auto stream2 = WaitForAdsStream(authority_server);
  ASSERT_TRUE(stream2 != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  // Note that version and nonce here do NOT use the values for Foo,
  // since each authority has its own state.
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream2->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 3))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, kXdstpResourceName);
  EXPECT_EQ(resource->value, 3);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
  // Now cancel watch for xdstp resource name.
  CancelFooWatch(watcher2.get(), kXdstpResourceName);
  EXPECT_TRUE(stream2->Orphaned());
}

TEST_F(XdsClientTest, FederationAuthorityDefaultsToTopLevelXdsServer) {
  constexpr char kAuthority[] = "xds.example.com";
  const std::string kXdstpResourceName = absl::StrCat(
      "xdstp://", kAuthority, "/", XdsFooResource::TypeUrl(), "/foo2");
  // Authority does not specify any xDS servers, so XdsClient will use
  // the top-level xDS server in the bootstrap config for this authority.
  InitXdsClient(FakeXdsBootstrap::Builder().AddAuthority(
      kAuthority, FakeXdsBootstrap::FakeAuthority()));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream to the top-level xDS server.
  auto stream = WaitForAdsStream(xds_client_->bootstrap().server());
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for the xdstp resource name.
  auto watcher2 = StartFooWatch(kXdstpResourceName);
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher2->HasEvent());
  // XdsClient will send a subscription request on the ADS stream that
  // includes both resources, since both are being obtained from the
  // same server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", kXdstpResourceName});
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 3))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, kXdstpResourceName);
  EXPECT_EQ(resource->value, 3);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1", kXdstpResourceName});
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  // XdsClient should send an unsubscription request.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  // Now cancel watch for xdstp resource name.
  CancelFooWatch(watcher2.get(), kXdstpResourceName);
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, FederationWithUnknownAuthority) {
  constexpr char kAuthority[] = "xds.example.com";
  const std::string kXdstpResourceName = absl::StrCat(
      "xdstp://", kAuthority, "/", XdsFooResource::TypeUrl(), "/foo2");
  // Note: Not adding authority to bootstrap config.
  InitXdsClient();
  // Start a watch for the xdstp resource name.
  auto watcher = StartFooWatch(kXdstpResourceName);
  // Watcher should immediately get an error about the unknown authority.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "authority \"xds.example.com\" not present in bootstrap config")
      << *error;
}

TEST_F(XdsClientTest, FederationWithUnparseableXdstpResourceName) {
  // Note: Not adding authority to bootstrap config.
  InitXdsClient();
  // Start a watch for the xdstp resource name.
  auto watcher = StartFooWatch("xdstp://x");
  // Watcher should immediately get an error about the unknown authority.
  auto error = watcher->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(), "Unable to parse resource name xdstp://x")
      << *error;
}

// TODO(roth,apolcyn): remove this test when the
// GRPC_EXPERIMENTAL_XDS_FEDERATION env var is removed.
TEST_F(XdsClientTest, FederationDisabledWithNewStyleName) {
  testing::ScopedEnvVar env_var("GRPC_EXPERIMENTAL_XDS_FEDERATION", "false");
  // We will use this xdstp name, whose authority is not present in
  // the bootstrap config.  But since federation is not enabled, we
  // will treat this as an opaque old-style name, so we'll send it to
  // the default server.
  constexpr char kXdstpResourceName[] =
      "xdstp://xds.example.com/test.v3.foo/foo1";
  InitXdsClient();
  // Start a watch for the xdstp name.
  auto watcher = StartFooWatch(kXdstpResourceName);
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream.
  auto stream = WaitForAdsStream();
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, kXdstpResourceName);
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  // Cancel watch.
  CancelFooWatch(watcher.get(), kXdstpResourceName);
  EXPECT_TRUE(stream->Orphaned());
}

TEST_F(XdsClientTest, FederationChannelFailureReportedToWatchers) {
  constexpr char kAuthority[] = "xds.example.com";
  const std::string kXdstpResourceName = absl::StrCat(
      "xdstp://", kAuthority, "/", XdsFooResource::TypeUrl(), "/foo2");
  FakeXdsBootstrap::FakeXdsServer authority_server;
  authority_server.set_server_uri("other_xds_server");
  FakeXdsBootstrap::FakeAuthority authority;
  authority.set_server(authority_server);
  InitXdsClient(
      FakeXdsBootstrap::Builder().AddAuthority(kAuthority, authority));
  // Start a watch for "foo1".
  auto watcher = StartFooWatch("foo1");
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher->HasEvent());
  // XdsClient should have created an ADS stream to the top-level xDS server.
  auto stream = WaitForAdsStream(xds_client_->bootstrap().server());
  ASSERT_TRUE(stream != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  auto request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("1")
          .set_nonce("A")
          .AddFooResource(XdsFooResource("foo1", 6))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  auto resource = watcher->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, "foo1");
  EXPECT_EQ(resource->value, 6);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"1", /*response_nonce=*/"A",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{"foo1"});
  // Start a watch for the xdstp resource name.
  auto watcher2 = StartFooWatch(kXdstpResourceName);
  // Watcher should initially not see any resource reported.
  EXPECT_FALSE(watcher2->HasEvent());
  // XdsClient will create a new stream to the server for this authority.
  auto stream2 = WaitForAdsStream(authority_server);
  ASSERT_TRUE(stream2 != nullptr);
  // XdsClient should have sent a subscription request on the ADS stream.
  // Note that version and nonce here do NOT use the values for Foo,
  // since each authority has its own state.
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"", /*response_nonce=*/"",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  CheckRequestNode(*request);  // Should be present on the first request.
  // Send a response.
  stream2->SendMessageToClient(
      ResponseBuilder(XdsFooResourceType::Get()->type_url())
          .set_version_info("2")
          .set_nonce("B")
          .AddFooResource(XdsFooResource(kXdstpResourceName, 3))
          .Serialize());
  // XdsClient should have delivered the response to the watcher.
  resource = watcher2->WaitForNextResource();
  ASSERT_NE(resource, nullptr);
  EXPECT_EQ(resource->name, kXdstpResourceName);
  EXPECT_EQ(resource->value, 3);
  // XdsClient should have sent an ACK message to the xDS server.
  request = WaitForRequest(stream2.get());
  ASSERT_TRUE(request.has_value());
  CheckRequest(*request, XdsFooResourceType::Get()->type_url(),
               /*version_info=*/"2", /*response_nonce=*/"B",
               /*error_detail=*/absl::OkStatus(),
               /*resource_names=*/{kXdstpResourceName});
  // Now cause a channel failure on the stream to the authority's xDS server.
  TriggerConnectionFailure(authority_server,
                           absl::UnavailableError("connection failed"));
  // The watcher for the xdstp resource name should see the error.
  auto error = watcher2->WaitForNextError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(error->message(),
            "xDS channel for server other_xds_server: connection failed "
            "(node ID:xds_client_test)")
      << *error;
  // The watcher for "foo1" should not see any error.
  EXPECT_FALSE(watcher->HasEvent());
  // Cancel watch for "foo1".
  CancelFooWatch(watcher.get(), "foo1");
  EXPECT_TRUE(stream->Orphaned());
  // Now cancel watch for xdstp resource name.
  CancelFooWatch(watcher2.get(), kXdstpResourceName);
  EXPECT_TRUE(stream2->Orphaned());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
