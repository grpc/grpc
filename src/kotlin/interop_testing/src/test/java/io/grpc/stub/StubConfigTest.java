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

package io.grpc.stub;

import static java.util.concurrent.TimeUnit.NANOSECONDS;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotSame;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.same;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import io.grpc.CallOptions;
import io.grpc.Channel;
import io.grpc.ClientCall;
import io.grpc.Deadline;
import io.grpc.MethodDescriptor;
import io.grpc.internal.NoopClientCall;
import io.grpc.testing.integration.Messages.SimpleRequest;
import io.grpc.testing.integration.Messages.SimpleResponse;
import io.grpc.testing.integration.TestServiceGrpc;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;
import org.mockito.ArgumentMatchers;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

/**
 * Tests for stub reconfiguration.
 */
@RunWith(JUnit4.class)
public class StubConfigTest {

  @Mock
  private Channel channel;

  @Mock
  private StreamObserver<SimpleResponse> responseObserver;

  /**
   * Sets up mocks.
   */
  @Before public void setUp() {
    MockitoAnnotations.initMocks(this);
    ClientCall<SimpleRequest, SimpleResponse> call =
        new NoopClientCall<>();
    when(channel.newCall(
            ArgumentMatchers.<MethodDescriptor<SimpleRequest, SimpleResponse>>any(),
            any(CallOptions.class)))
        .thenReturn(call);
  }

  @Test
  public void testConfigureDeadline() {
    Deadline deadline = Deadline.after(2, NANOSECONDS);
    // Create a default stub
    TestServiceGrpc.TestServiceBlockingStub stub = TestServiceGrpc.newBlockingStub(channel);
    assertNull(stub.getCallOptions().getDeadline());
    // Reconfigure it
    TestServiceGrpc.TestServiceBlockingStub reconfiguredStub = stub.withDeadline(deadline);
    // New altered config
    assertEquals(deadline, reconfiguredStub.getCallOptions().getDeadline());
    // Default config unchanged
    assertNull(stub.getCallOptions().getDeadline());
  }

  @Test
  public void testStubCallOptionsPopulatedToNewCall() {
    TestServiceGrpc.TestServiceStub stub = TestServiceGrpc.newStub(channel);
    CallOptions options1 = stub.getCallOptions();
    SimpleRequest request = SimpleRequest.getDefaultInstance();
    stub.unaryCall(request, responseObserver);
    verify(channel).newCall(same(TestServiceGrpc.getUnaryCallMethod()), same(options1));
    stub = stub.withDeadlineAfter(2, NANOSECONDS);
    CallOptions options2 = stub.getCallOptions();
    assertNotSame(options1, options2);
    stub.unaryCall(request, responseObserver);
    verify(channel).newCall(same(TestServiceGrpc.getUnaryCallMethod()), same(options2));
  }
}
