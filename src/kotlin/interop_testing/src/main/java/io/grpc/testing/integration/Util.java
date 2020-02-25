/*
 * Copyright 2014 The gRPC Authors
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

import com.google.protobuf.MessageLite;
import io.grpc.Metadata;
import io.grpc.protobuf.lite.ProtoLiteUtils;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.List;
import org.junit.Assert;

/**
 * Utility methods to support integration testing.
 */
public class Util {

  public static final Metadata.Key<Messages.SimpleContext> METADATA_KEY =
      Metadata.Key.of(
          "grpc.testing.SimpleContext" + Metadata.BINARY_HEADER_SUFFIX,
          ProtoLiteUtils.metadataMarshaller(Messages.SimpleContext.getDefaultInstance()));
  public static final Metadata.Key<String> ECHO_INITIAL_METADATA_KEY
      = Metadata.Key.of("x-grpc-test-echo-initial", Metadata.ASCII_STRING_MARSHALLER);
  public static final Metadata.Key<byte[]> ECHO_TRAILING_METADATA_KEY
      = Metadata.Key.of("x-grpc-test-echo-trailing-bin", Metadata.BINARY_BYTE_MARSHALLER);

  /**
   * Combine a host and port into an authority string.
   */
  public static String authorityFromHostAndPort(String host, int port) {
    try {
      return new URI(null, null, host, port, null, null, null).getAuthority();
    } catch (URISyntaxException ex) {
      throw new IllegalArgumentException("Invalid host or port: " + host + " " + port, ex);
    }
  }

  /** Assert that two messages are equal, producing a useful message if not. */
  @SuppressWarnings("LiteProtoToString")
  public static void assertEquals(MessageLite expected, MessageLite actual) {
    if (expected == null || actual == null) {
      Assert.assertEquals(expected, actual);
    } else {
      if (!expected.equals(actual)) {
        // This assertEquals should always complete.
        Assert.assertEquals(expected.toString(), actual.toString());
        // But if it doesn't, then this should.
        Assert.assertEquals(expected, actual);
        Assert.fail("Messages not equal, but assertEquals didn't throw");
      }
    }
  }

  /** Assert that two lists of messages are equal, producing a useful message if not. */
  public static void assertEquals(List<? extends MessageLite> expected,
      List<? extends MessageLite> actual) {
    if (expected == null || actual == null) {
      Assert.assertEquals(expected, actual);
    } else if (expected.size() != actual.size()) {
      Assert.assertEquals(expected, actual);
    } else {
      for (int i = 0; i < expected.size(); i++) {
        assertEquals(expected.get(i), actual.get(i));
      }
    }
  }
}
