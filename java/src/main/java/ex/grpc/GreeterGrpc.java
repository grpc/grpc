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
public class GreeterGrpc {

  private static final com.google.net.stubby.stub.Method<ex.grpc.Helloworld.HelloRequest,
      ex.grpc.Helloworld.HelloReply> METHOD_SAY_HELLO =
      com.google.net.stubby.stub.Method.create(
          com.google.net.stubby.MethodType.UNARY, "sayHello",
          com.google.net.stubby.proto.ProtoUtils.marshaller(ex.grpc.Helloworld.HelloRequest.PARSER),
          com.google.net.stubby.proto.ProtoUtils.marshaller(ex.grpc.Helloworld.HelloReply.PARSER));

  public static GreeterStub newStub(com.google.net.stubby.Channel channel) {
    return new GreeterStub(channel, CONFIG);
  }

  public static GreeterBlockingStub newBlockingStub(
      com.google.net.stubby.Channel channel) {
    return new GreeterBlockingStub(channel, CONFIG);
  }

  public static GreeterFutureStub newFutureStub(
      com.google.net.stubby.Channel channel) {
    return new GreeterFutureStub(channel, CONFIG);
  }

  public static final GreeterServiceDescriptor CONFIG =
      new GreeterServiceDescriptor();

  @javax.annotation.concurrent.Immutable
  public static class GreeterServiceDescriptor extends
      com.google.net.stubby.stub.AbstractServiceDescriptor<GreeterServiceDescriptor> {
    public final com.google.net.stubby.MethodDescriptor<ex.grpc.Helloworld.HelloRequest,
        ex.grpc.Helloworld.HelloReply> sayHello;

    private GreeterServiceDescriptor() {
      sayHello = createMethodDescriptor(
          "helloworld.Greeter", METHOD_SAY_HELLO);
    }

    private GreeterServiceDescriptor(
        java.util.Map<java.lang.String, com.google.net.stubby.MethodDescriptor<?, ?>> methodMap) {
      sayHello = (com.google.net.stubby.MethodDescriptor<ex.grpc.Helloworld.HelloRequest,
          ex.grpc.Helloworld.HelloReply>) methodMap.get(
          CONFIG.sayHello.getName());
    }

    @java.lang.Override
    protected GreeterServiceDescriptor build(
        java.util.Map<java.lang.String, com.google.net.stubby.MethodDescriptor<?, ?>> methodMap) {
      return new GreeterServiceDescriptor(methodMap);
    }

    @java.lang.Override
    public com.google.common.collect.ImmutableList<com.google.net.stubby.MethodDescriptor<?, ?>> methods() {
      return com.google.common.collect.ImmutableList.<com.google.net.stubby.MethodDescriptor<?, ?>>of(
          sayHello);
    }
  }

  public static interface Greeter {

    public void sayHello(ex.grpc.Helloworld.HelloRequest request,
        com.google.net.stubby.stub.StreamObserver<ex.grpc.Helloworld.HelloReply> responseObserver);
  }

  public static interface GreeterBlockingClient {

    public ex.grpc.Helloworld.HelloReply sayHello(ex.grpc.Helloworld.HelloRequest request);
  }

  public static interface GreeterFutureClient {

    public com.google.common.util.concurrent.ListenableFuture<ex.grpc.Helloworld.HelloReply> sayHello(
        ex.grpc.Helloworld.HelloRequest request);
  }

  public static class GreeterStub extends
      com.google.net.stubby.stub.AbstractStub<GreeterStub, GreeterServiceDescriptor>
      implements Greeter {
    private GreeterStub(com.google.net.stubby.Channel channel,
        GreeterServiceDescriptor config) {
      super(channel, config);
    }

    @java.lang.Override
    protected GreeterStub build(com.google.net.stubby.Channel channel,
        GreeterServiceDescriptor config) {
      return new GreeterStub(channel, config);
    }

    @java.lang.Override
    public void sayHello(ex.grpc.Helloworld.HelloRequest request,
        com.google.net.stubby.stub.StreamObserver<ex.grpc.Helloworld.HelloReply> responseObserver) {
      asyncUnaryCall(
          channel.newCall(config.sayHello), request, responseObserver);
    }
  }

  public static class GreeterBlockingStub extends
      com.google.net.stubby.stub.AbstractStub<GreeterBlockingStub, GreeterServiceDescriptor>
      implements GreeterBlockingClient {
    private GreeterBlockingStub(com.google.net.stubby.Channel channel,
        GreeterServiceDescriptor config) {
      super(channel, config);
    }

    @java.lang.Override
    protected GreeterBlockingStub build(com.google.net.stubby.Channel channel,
        GreeterServiceDescriptor config) {
      return new GreeterBlockingStub(channel, config);
    }

    @java.lang.Override
    public ex.grpc.Helloworld.HelloReply sayHello(ex.grpc.Helloworld.HelloRequest request) {
      return blockingUnaryCall(
          channel.newCall(config.sayHello), request);
    }
  }

  public static class GreeterFutureStub extends
      com.google.net.stubby.stub.AbstractStub<GreeterFutureStub, GreeterServiceDescriptor>
      implements GreeterFutureClient {
    private GreeterFutureStub(com.google.net.stubby.Channel channel,
        GreeterServiceDescriptor config) {
      super(channel, config);
    }

    @java.lang.Override
    protected GreeterFutureStub build(com.google.net.stubby.Channel channel,
        GreeterServiceDescriptor config) {
      return new GreeterFutureStub(channel, config);
    }

    @java.lang.Override
    public com.google.common.util.concurrent.ListenableFuture<ex.grpc.Helloworld.HelloReply> sayHello(
        ex.grpc.Helloworld.HelloRequest request) {
      return unaryFutureCall(
          channel.newCall(config.sayHello), request);
    }
  }

  public static com.google.net.stubby.ServerServiceDefinition bindService(
      final Greeter serviceImpl) {
    return com.google.net.stubby.ServerServiceDefinition.builder("helloworld.Greeter")
      .addMethod(createMethodDefinition(
          METHOD_SAY_HELLO,
          asyncUnaryRequestCall(
            new com.google.net.stubby.stub.ServerCalls.UnaryRequestMethod<
                ex.grpc.Helloworld.HelloRequest,
                ex.grpc.Helloworld.HelloReply>() {
              @java.lang.Override
              public void invoke(
                  ex.grpc.Helloworld.HelloRequest request,
                  com.google.net.stubby.stub.StreamObserver<ex.grpc.Helloworld.HelloReply> responseObserver) {
                serviceImpl.sayHello(request, responseObserver);
              }
            }))).build();
  }
}
