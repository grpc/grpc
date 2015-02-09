package ex.grpc;

import static com.google.net.stubby.stub.Calls.createMethodDescriptor;
import static com.google.net.stubby.stub.Calls.asyncUnaryCall;
import static com.google.net.stubby.stub.Calls.asyncServerStreamingCall;
import static com.google.net.stubby.stub.Calls.asyncClientStreamingCall;
import static com.google.net.stubby.stub.Calls.duplexStreamingCall;
import static com.google.net.stubby.stub.Calls.blockingUnaryCall;
import static com.google.net.stubby.stub.Calls.blockingServerStreamingCall;
import static com.google.net.stubby.stub.Calls.unaryFutureCall;
import static com.google.net.stubby.stub.ServerCalls.createMethodDefinition;
import static com.google.net.stubby.stub.ServerCalls.asyncUnaryRequestCall;
import static com.google.net.stubby.stub.ServerCalls.asyncStreamingRequestCall;

@javax.annotation.Generated("by gRPC proto compiler")
public class GreetingsGrpc {

  private static final com.google.net.stubby.stub.Method<ex.grpc.Helloworld.HelloRequest,
      ex.grpc.Helloworld.HelloReply> METHOD_HELLO =
      com.google.net.stubby.stub.Method.create(
          com.google.net.stubby.MethodType.UNARY, "hello",
          com.google.net.stubby.proto.ProtoUtils.marshaller(ex.grpc.Helloworld.HelloRequest.PARSER),
          com.google.net.stubby.proto.ProtoUtils.marshaller(ex.grpc.Helloworld.HelloReply.PARSER));

  public static GreetingsStub newStub(com.google.net.stubby.Channel channel) {
    return new GreetingsStub(channel, CONFIG);
  }

  public static GreetingsBlockingStub newBlockingStub(
      com.google.net.stubby.Channel channel) {
    return new GreetingsBlockingStub(channel, CONFIG);
  }

  public static GreetingsFutureStub newFutureStub(
      com.google.net.stubby.Channel channel) {
    return new GreetingsFutureStub(channel, CONFIG);
  }

  public static final GreetingsServiceDescriptor CONFIG =
      new GreetingsServiceDescriptor();

  @javax.annotation.concurrent.Immutable
  public static class GreetingsServiceDescriptor extends
      com.google.net.stubby.stub.AbstractServiceDescriptor<GreetingsServiceDescriptor> {
    public final com.google.net.stubby.MethodDescriptor<ex.grpc.Helloworld.HelloRequest,
        ex.grpc.Helloworld.HelloReply> hello;

    private GreetingsServiceDescriptor() {
      hello = createMethodDescriptor(
          "helloworld.Greetings", METHOD_HELLO);
    }

    private GreetingsServiceDescriptor(
        java.util.Map<java.lang.String, com.google.net.stubby.MethodDescriptor<?, ?>> methodMap) {
      hello = (com.google.net.stubby.MethodDescriptor<ex.grpc.Helloworld.HelloRequest,
          ex.grpc.Helloworld.HelloReply>) methodMap.get(
          CONFIG.hello.getName());
    }

    @java.lang.Override
    protected GreetingsServiceDescriptor build(
        java.util.Map<java.lang.String, com.google.net.stubby.MethodDescriptor<?, ?>> methodMap) {
      return new GreetingsServiceDescriptor(methodMap);
    }

    @java.lang.Override
    public com.google.common.collect.ImmutableList<com.google.net.stubby.MethodDescriptor<?, ?>> methods() {
      return com.google.common.collect.ImmutableList.<com.google.net.stubby.MethodDescriptor<?, ?>>of(
          hello);
    }
  }

  public static interface Greetings {

    public void hello(ex.grpc.Helloworld.HelloRequest request,
        com.google.net.stubby.stub.StreamObserver<ex.grpc.Helloworld.HelloReply> responseObserver);
  }

  public static interface GreetingsBlockingClient {

    public ex.grpc.Helloworld.HelloReply hello(ex.grpc.Helloworld.HelloRequest request);
  }

  public static interface GreetingsFutureClient {

    public com.google.common.util.concurrent.ListenableFuture<ex.grpc.Helloworld.HelloReply> hello(
        ex.grpc.Helloworld.HelloRequest request);
  }

  public static class GreetingsStub extends
      com.google.net.stubby.stub.AbstractStub<GreetingsStub, GreetingsServiceDescriptor>
      implements Greetings {
    private GreetingsStub(com.google.net.stubby.Channel channel,
        GreetingsServiceDescriptor config) {
      super(channel, config);
    }

    @java.lang.Override
    protected GreetingsStub build(com.google.net.stubby.Channel channel,
        GreetingsServiceDescriptor config) {
      return new GreetingsStub(channel, config);
    }

    @java.lang.Override
    public void hello(ex.grpc.Helloworld.HelloRequest request,
        com.google.net.stubby.stub.StreamObserver<ex.grpc.Helloworld.HelloReply> responseObserver) {
      asyncUnaryCall(
          channel.newCall(config.hello), request, responseObserver);
    }
  }

  public static class GreetingsBlockingStub extends
      com.google.net.stubby.stub.AbstractStub<GreetingsBlockingStub, GreetingsServiceDescriptor>
      implements GreetingsBlockingClient {
    private GreetingsBlockingStub(com.google.net.stubby.Channel channel,
        GreetingsServiceDescriptor config) {
      super(channel, config);
    }

    @java.lang.Override
    protected GreetingsBlockingStub build(com.google.net.stubby.Channel channel,
        GreetingsServiceDescriptor config) {
      return new GreetingsBlockingStub(channel, config);
    }

    @java.lang.Override
    public ex.grpc.Helloworld.HelloReply hello(ex.grpc.Helloworld.HelloRequest request) {
      return blockingUnaryCall(
          channel.newCall(config.hello), request);
    }
  }

  public static class GreetingsFutureStub extends
      com.google.net.stubby.stub.AbstractStub<GreetingsFutureStub, GreetingsServiceDescriptor>
      implements GreetingsFutureClient {
    private GreetingsFutureStub(com.google.net.stubby.Channel channel,
        GreetingsServiceDescriptor config) {
      super(channel, config);
    }

    @java.lang.Override
    protected GreetingsFutureStub build(com.google.net.stubby.Channel channel,
        GreetingsServiceDescriptor config) {
      return new GreetingsFutureStub(channel, config);
    }

    @java.lang.Override
    public com.google.common.util.concurrent.ListenableFuture<ex.grpc.Helloworld.HelloReply> hello(
        ex.grpc.Helloworld.HelloRequest request) {
      return unaryFutureCall(
          channel.newCall(config.hello), request);
    }
  }

  public static com.google.net.stubby.ServerServiceDefinition bindService(
      final Greetings serviceImpl) {
    return com.google.net.stubby.ServerServiceDefinition.builder("helloworld.Greetings")
      .addMethod(createMethodDefinition(
          METHOD_HELLO,
          asyncUnaryRequestCall(
            new com.google.net.stubby.stub.ServerCalls.UnaryRequestMethod<
                ex.grpc.Helloworld.HelloRequest,
                ex.grpc.Helloworld.HelloReply>() {
              @java.lang.Override
              public void invoke(
                  ex.grpc.Helloworld.HelloRequest request,
                  com.google.net.stubby.stub.StreamObserver<ex.grpc.Helloworld.HelloReply> responseObserver) {
                serviceImpl.hello(request, responseObserver);
              }
            }))).build();
  }
}
