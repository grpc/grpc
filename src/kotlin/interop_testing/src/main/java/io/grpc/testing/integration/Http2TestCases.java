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
 * Enum of HTTP/2 interop test cases.
 */
public enum Http2TestCases {
  RST_AFTER_HEADER("server resets stream after sending header"),
  RST_AFTER_DATA("server resets stream after sending data"),
  RST_DURING_DATA("server resets stream in the middle of sending data"),
  GOAWAY("server sends goaway after first request and asserts second request uses new connection"),
  PING("server sends pings during request and verifies client response"),
  MAX_STREAMS("server verifies that the client respects MAX_STREAMS setting");

  private final String description;

  Http2TestCases(String description) {
    this.description = description;
  }

  /**
   * Returns a description of the test case.
   */
  public String description() {
    return description;
  }

  /**
   * Returns the {@link Http2TestCases} matching the string {@code s}. The
   * matching is case insensitive.
   */
  public static Http2TestCases fromString(String s) {
    Preconditions.checkNotNull(s, "s");
    try {
      return Http2TestCases.valueOf(s.toUpperCase());
    } catch (IllegalArgumentException ex) {
      throw new IllegalArgumentException("Invalid test case: " + s);
    }
  }
}
