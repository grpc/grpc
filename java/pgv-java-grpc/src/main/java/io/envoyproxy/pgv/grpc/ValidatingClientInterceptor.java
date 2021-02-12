package io.envoyproxy.pgv.grpc;

import io.envoyproxy.pgv.ValidationException;
import io.envoyproxy.pgv.ValidatorIndex;
import io.grpc.*;

/**
 * {@code ValidatingClientInterceptor} is a gRPC {@code ClientInterceptor} that validates outbound gRPC messages before
 * transmission. Non-compliant messages result in an {@code INVALID_ARGUMENT} status exception.
 */
public class ValidatingClientInterceptor implements ClientInterceptor {
    private final ValidatorIndex index;

    public ValidatingClientInterceptor(ValidatorIndex index) {
        this.index = index;
    }

    @Override
    public <ReqT, RespT> ClientCall<ReqT, RespT> interceptCall(MethodDescriptor<ReqT, RespT> method, CallOptions callOptions, Channel next) {
        return new ForwardingClientCall.SimpleForwardingClientCall<ReqT, RespT>(next.newCall(method, callOptions)) {
            @Override
            public void sendMessage(ReqT message) {
                try {
                    index.validatorFor(message.getClass()).assertValid(message);
                    super.sendMessage(message);
                } catch (ValidationException ex) {
                    throw ValidationExceptions.asStatusRuntimeException(ex);
                }
            }
        };
    }
}
