/*
 * Copyright 2016 The gRPC Authors
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
 */

package io.grpc.testing.integration;

import com.google.common.base.Preconditions;

/**
 * Enum of interop test cases.
 */
public enum TestCases {
  EMPTY_UNARY("empty (zero bytes) request and response"),
  CACHEABLE_UNARY("cacheable unary rpc sent using GET"),
  LARGE_UNARY("single request and (large) response"),
  CLIENT_COMPRESSED_UNARY("client compressed unary request"),
  CLIENT_COMPRESSED_UNARY_NOPROBE(
      "client compressed unary request (skip initial feature-probing request)"),
  SERVER_COMPRESSED_UNARY("server compressed unary response"),
  CLIENT_STREAMING("request streaming with single response"),
  CLIENT_COMPRESSED_STREAMING("client per-message compression on stream"),
  CLIENT_COMPRESSED_STREAMING_NOPROBE(
      "client per-message compression on stream (skip initial feature-probing request)"),
  SERVER_STREAMING("single request with response streaming"),
  SERVER_COMPRESSED_STREAMING("server per-message compression on stream"),
  PING_PONG("full-duplex ping-pong streaming"),
  EMPTY_STREAM("A stream that has zero-messages in both directions"),
  COMPUTE_ENGINE_CREDS("large_unary with service_account auth"),
  COMPUTE_ENGINE_CHANNEL_CREDENTIALS("large unary with compute engine channel builder"),
  SERVICE_ACCOUNT_CREDS("large_unary with compute engine auth"),
  JWT_TOKEN_CREDS("JWT-based auth"),
  OAUTH2_AUTH_TOKEN("raw oauth2 access token auth"),
  PER_RPC_CREDS("per rpc raw oauth2 access token auth"),
  GOOGLE_DEFAULT_CREDENTIALS(
      "google default credentials, i.e. GoogleManagedChannel based auth"),
  CUSTOM_METADATA("unary and full duplex calls with metadata"),
  STATUS_CODE_AND_MESSAGE("request error code and message"),
  SPECIAL_STATUS_MESSAGE("special characters in status message"),
  UNIMPLEMENTED_METHOD("call an unimplemented RPC method"),
  UNIMPLEMENTED_SERVICE("call an unimplemented RPC service"),
  CANCEL_AFTER_BEGIN("cancel stream after starting it"),
  CANCEL_AFTER_FIRST_RESPONSE("cancel on first response"),
  TIMEOUT_ON_SLEEPING_SERVER("timeout before receiving a response"),
  VERY_LARGE_REQUEST("very large request"),
  PICK_FIRST_UNARY("all requests are sent to one server despite multiple servers are resolved");

  private final String description;

  TestCases(String description) {
    this.description = description;
  }

  /**
   * Returns a description of the test case.
   */
  public String description() {
    return description;
  }

  /**
   * Returns the {@link TestCases} matching the string {@code s}. The
   * matching is done case insensitive.
   */
  public static TestCases fromString(String s) {
    Preconditions.checkNotNull(s, "s");
    return TestCases.valueOf(s.toUpperCase());
  }
}
