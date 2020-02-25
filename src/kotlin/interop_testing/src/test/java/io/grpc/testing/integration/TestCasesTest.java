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

import static io.grpc.testing.integration.TestCases.fromString;
import static org.junit.Assert.assertEquals;

import java.util.HashSet;
import java.util.Set;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/**
 * Unit tests for {@link TestCases}.
 */
@RunWith(JUnit4.class)
public class TestCasesTest {

  @Test(expected = IllegalArgumentException.class)
  public void unknownStringThrowsException() {
    fromString("does_not_exist_1234");
  }

  @Test
  public void testCaseNamesShouldMapToEnums() {
    // names of testcases as defined in the interop spec
    String[] testCases = {
      "empty_unary",
      "cacheable_unary",
      "large_unary",
      "client_compressed_unary",
      "server_compressed_unary",
      "client_streaming",
      "client_compressed_streaming",
      "compute_engine_channel_credentials",
      "server_streaming",
      "server_compressed_streaming",
      "ping_pong",
      "empty_stream",
      "compute_engine_creds",
      "service_account_creds",
      "jwt_token_creds",
      "oauth2_auth_token",
      "per_rpc_creds",
      "google_default_credentials",
      "custom_metadata",
      "status_code_and_message",
      "special_status_message",
      "unimplemented_method",
      "unimplemented_service",
      "cancel_after_begin",
      "cancel_after_first_response",
      "timeout_on_sleeping_server"
    };

    // additional test cases
    String[] additionalTestCases = {
      "client_compressed_unary_noprobe",
      "client_compressed_streaming_noprobe",
      "very_large_request",
      "pick_first_unary"
    };

    assertEquals(testCases.length + additionalTestCases.length, TestCases.values().length);

    Set<TestCases> testCaseSet = new HashSet<>(testCases.length);
    for (String testCase : testCases) {
      testCaseSet.add(TestCases.fromString(testCase));
    }
    for (String testCase : additionalTestCases) {
      testCaseSet.add(TestCases.fromString(testCase));
    }

    assertEquals(TestCases.values().length, testCaseSet.size());
  }
}
