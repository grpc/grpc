package io.grpc.helloworldexample;

import java.io.IOException;

import static io.grpc.stub.Calls.asyncUnaryCall;
import static io.grpc.stub.Calls.blockingUnaryCall;
import static io.grpc.stub.Calls.createMethodDescriptor;
import static io.grpc.stub.Calls.unaryFutureCall;
import static io.grpc.stub.ServerCalls.asyncUnaryRequestCall;
import static io.grpc.stub.ServerCalls.createMethodDefinition;

public class GreeterGrpc {

    private static final io.grpc.stub.Method<Helloworld.HelloRequest,
            Helloworld.HelloReply> METHOD_SAY_HELLO =
            io.grpc.stub.Method.create(
                    io.grpc.MethodType.UNARY, "SayHello",
                    io.grpc.protobuf.nano.NanoUtils.<Helloworld.HelloRequest>marshaller(
                            new io.grpc.protobuf.nano.Parser<Helloworld.HelloRequest>() {
                                @Override
                                public Helloworld.HelloRequest parse(com.google.protobuf.nano.CodedInputByteBufferNano input) throws IOException {
                                    return Helloworld.HelloRequest.parseFrom(input);
                                }
                            }),
                    io.grpc.protobuf.nano.NanoUtils.<Helloworld.HelloReply>marshaller(
                            new io.grpc.protobuf.nano.Parser<Helloworld.HelloReply>() {
                                @Override
                                public Helloworld.HelloReply parse(com.google.protobuf.nano.CodedInputByteBufferNano input) throws IOException {
                                    return Helloworld.HelloReply.parseFrom(input);
                                }
                            }));

    public static GreeterStub newStub(io.grpc.Channel channel) {
        return new GreeterStub(channel, CONFIG);
    }

    public static GreeterBlockingStub newBlockingStub(
            io.grpc.Channel channel) {
        return new GreeterBlockingStub(channel, CONFIG);
    }

    public static GreeterFutureStub newFutureStub(
            io.grpc.Channel channel) {
        return new GreeterFutureStub(channel, CONFIG);
    }

    public static final GreeterServiceDescriptor CONFIG =
            new GreeterServiceDescriptor();

    public static class GreeterServiceDescriptor extends
            io.grpc.stub.AbstractServiceDescriptor<GreeterServiceDescriptor> {
        public final io.grpc.MethodDescriptor<Helloworld.HelloRequest,
                Helloworld.HelloReply> sayHello;

        private GreeterServiceDescriptor() {
            sayHello = createMethodDescriptor(
                    "helloworld.Greeter", METHOD_SAY_HELLO);
        }

        private GreeterServiceDescriptor(
                java.util.Map<java.lang.String, io.grpc.MethodDescriptor<?, ?>> methodMap) {
            sayHello = (io.grpc.MethodDescriptor<Helloworld.HelloRequest,
                    Helloworld.HelloReply>) methodMap.get(
                    CONFIG.sayHello.getName());
        }

        @java.lang.Override
        protected GreeterServiceDescriptor build(
                java.util.Map<java.lang.String, io.grpc.MethodDescriptor<?, ?>> methodMap) {
            return new GreeterServiceDescriptor(methodMap);
        }

        @java.lang.Override
        public com.google.common.collect.ImmutableList<io.grpc.MethodDescriptor<?, ?>> methods() {
            return com.google.common.collect.ImmutableList.<io.grpc.MethodDescriptor<?, ?>>of(
                    sayHello);
        }
    }

    public static interface Greeter {

        public void sayHello(Helloworld.HelloRequest request,
                io.grpc.stub.StreamObserver<Helloworld.HelloReply> responseObserver);
    }

    public static interface GreeterBlockingClient {

        public Helloworld.HelloReply sayHello(Helloworld.HelloRequest request);
    }

    public static interface GreeterFutureClient {

        public com.google.common.util.concurrent.ListenableFuture<Helloworld.HelloReply> sayHello(
                Helloworld.HelloRequest request);
    }

    public static class GreeterStub extends
            io.grpc.stub.AbstractStub<GreeterStub, GreeterServiceDescriptor>
            implements Greeter {
        private GreeterStub(io.grpc.Channel channel,
                GreeterServiceDescriptor config) {
            super(channel, config);
        }

        @java.lang.Override
        protected GreeterStub build(io.grpc.Channel channel,
                GreeterServiceDescriptor config) {
            return new GreeterStub(channel, config);
        }

        @java.lang.Override
        public void sayHello(Helloworld.HelloRequest request,
                io.grpc.stub.StreamObserver<Helloworld.HelloReply> responseObserver) {
            asyncUnaryCall(
                    channel.newCall(config.sayHello), request, responseObserver);
        }
    }

    public static class GreeterBlockingStub extends
            io.grpc.stub.AbstractStub<GreeterBlockingStub, GreeterServiceDescriptor>
            implements GreeterBlockingClient {
        private GreeterBlockingStub(io.grpc.Channel channel,
                GreeterServiceDescriptor config) {
            super(channel, config);
        }

        @java.lang.Override
        protected GreeterBlockingStub build(io.grpc.Channel channel,
                GreeterServiceDescriptor config) {
            return new GreeterBlockingStub(channel, config);
        }

        @java.lang.Override
        public Helloworld.HelloReply sayHello(Helloworld.HelloRequest request) {
            return blockingUnaryCall(
                    channel.newCall(config.sayHello), request);
        }
    }

    public static class GreeterFutureStub extends
            io.grpc.stub.AbstractStub<GreeterFutureStub, GreeterServiceDescriptor>
            implements GreeterFutureClient {
        private GreeterFutureStub(io.grpc.Channel channel,
                GreeterServiceDescriptor config) {
            super(channel, config);
        }

        @java.lang.Override
        protected GreeterFutureStub build(io.grpc.Channel channel,
                GreeterServiceDescriptor config) {
            return new GreeterFutureStub(channel, config);
        }

        @java.lang.Override
        public com.google.common.util.concurrent.ListenableFuture<Helloworld.HelloReply> sayHello(
                Helloworld.HelloRequest request) {
            return unaryFutureCall(
                    channel.newCall(config.sayHello), request);
        }
    }

    public static io.grpc.ServerServiceDefinition bindService(
            final Greeter serviceImpl) {
        return io.grpc.ServerServiceDefinition.builder("helloworld.Greeter")
                .addMethod(createMethodDefinition(
                        METHOD_SAY_HELLO,
                        asyncUnaryRequestCall(
                                new io.grpc.stub.ServerCalls.UnaryRequestMethod<
                                        Helloworld.HelloRequest,
                                        Helloworld.HelloReply>() {
                                    @java.lang.Override
                                    public void invoke(
                                            Helloworld.HelloRequest request,
                                            io.grpc.stub.StreamObserver<Helloworld.HelloReply> responseObserver) {
                                        serviceImpl.sayHello(request, responseObserver);
                                    }
                                }))).build();
    }
}