package io.envoyproxy.pgv.grpc;

import com.google.protobuf.Any;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.rpc.BadRequest;
import com.google.rpc.Status;
import io.envoyproxy.pgv.ReflectiveValidatorIndex;
import io.envoyproxy.pgv.ValidationException;
import io.envoyproxy.pgv.Validator;
import io.envoyproxy.pgv.ValidatorIndex;
import io.envoyproxy.pgv.grpc.asubpackage.GreeterGrpc;
import io.envoyproxy.pgv.grpc.asubpackage.HelloJKRequest;
import io.envoyproxy.pgv.grpc.asubpackage.HelloResponse;
import io.grpc.BindableService;
import io.grpc.ServerInterceptors;
import io.grpc.StatusRuntimeException;
import io.grpc.protobuf.StatusProto;
import io.grpc.stub.StreamObserver;
import io.grpc.testing.GrpcServerRule;
import org.assertj.core.api.Condition;
import org.junit.Rule;
import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThatExceptionOfType;

public class ValidatingServerInterceptorTest {
    @Rule
    public GrpcServerRule serverRule = new GrpcServerRule();

    private BindableService svc = new GreeterGrpc.GreeterImplBase() {
        @Override
        public void sayHello(HelloJKRequest request, StreamObserver<HelloResponse> responseObserver) {
            responseObserver.onNext(HelloResponse.newBuilder().setMessage("Hello " + request.getName()).build());
            responseObserver.onCompleted();
        }
    };

    @Test
    public void InterceptorPassesValidMessages() {
        ValidatingServerInterceptor interceptor = new ValidatingServerInterceptor(ValidatorIndex.ALWAYS_VALID);

        serverRule.getServiceRegistry().addService(ServerInterceptors.intercept(svc, interceptor));

        GreeterGrpc.GreeterBlockingStub stub = GreeterGrpc.newBlockingStub(serverRule.getChannel());
        stub.sayHello(HelloJKRequest.newBuilder().setName("World").build());
    }

    @Test
    public void InterceptorPassesValidMessagesGenerated() {
        ValidatingServerInterceptor interceptor = new ValidatingServerInterceptor(new ReflectiveValidatorIndex());

        serverRule.getServiceRegistry().addService(ServerInterceptors.intercept(svc, interceptor));

        GreeterGrpc.GreeterBlockingStub stub = GreeterGrpc.newBlockingStub(serverRule.getChannel());
        stub.sayHello(HelloJKRequest.newBuilder().setName("World").build());
    }

    @Test
    public void InterceptorRejectsInvalidMessages() {
        ValidatingServerInterceptor interceptor = new ValidatingServerInterceptor(new ValidatorIndex() {
            @Override
            public <T> Validator<T> validatorFor(Class clazz) {
                return proto -> {
                    throw new ValidationException("one", "", "is invalid");
                };
            }
        });

        serverRule.getServiceRegistry().addService(ServerInterceptors.intercept(svc, interceptor));

        GreeterGrpc.GreeterBlockingStub stub = GreeterGrpc.newBlockingStub(serverRule.getChannel());
        assertThatExceptionOfType(StatusRuntimeException.class).isThrownBy(() -> stub.sayHello(HelloJKRequest.newBuilder().setName("World").build()))
                .withMessage("INVALID_ARGUMENT: one: is invalid - Got ")
                .has(new Condition<>(e -> {
                    try {
                        Status status = StatusProto.fromThrowable(e);
                        Any any = status.getDetailsList().get(0);
                        BadRequest badRequest = any.unpack(BadRequest.class);
                        return badRequest.getFieldViolationsCount() == 1 && badRequest.getFieldViolations(0).getField().equals("one")
                                && badRequest.getFieldViolations(0).getDescription().equals("is invalid");
                    } catch (InvalidProtocolBufferException ex) {
                        return false;
                    }
                }, "BadRequest details"));
    }

    @Test
    public void InterceptorRejectsInvalidMessagesGenerated() {
        ValidatingServerInterceptor interceptor = new ValidatingServerInterceptor(new ReflectiveValidatorIndex());

        serverRule.getServiceRegistry().addService(ServerInterceptors.intercept(svc, interceptor));

        GreeterGrpc.GreeterBlockingStub stub = GreeterGrpc.newBlockingStub(serverRule.getChannel());

        assertThatExceptionOfType(StatusRuntimeException.class).isThrownBy(() -> stub.sayHello(HelloJKRequest.newBuilder().setName("Bananas").build()))
                .withMessageStartingWith("INVALID_ARGUMENT: .io.envoyproxy.pgv.grpc.HelloJKRequest.name: must equal World")
                .has(new Condition<>(e -> {
                    try {
                        Status status = StatusProto.fromThrowable(e);
                        Any any = status.getDetailsList().get(0);
                        BadRequest badRequest = any.unpack(BadRequest.class);
                        return badRequest.getFieldViolationsCount() == 1 && badRequest.getFieldViolations(0).getField().equals(".io.envoyproxy.pgv.grpc.HelloJKRequest.name")
                                && badRequest.getFieldViolations(0).getDescription().equals("must equal World");
                    } catch (InvalidProtocolBufferException ex) {
                        return false;
                    }
                }, "BadRequest details"));
    }
}
