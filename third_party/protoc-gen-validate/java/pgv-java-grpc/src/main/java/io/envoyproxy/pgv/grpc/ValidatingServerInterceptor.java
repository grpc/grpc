package io.envoyproxy.pgv.grpc;

import io.envoyproxy.pgv.ValidationException;
import io.envoyproxy.pgv.ValidatorIndex;
import io.grpc.*;

/**
 * {@code ValidatingServerInterceptor} is a gRPC {@code ServerInterceptor} that validates inbound gRPC messages before
 * processing. Non-compliant messages result in an {@code INVALID_ARGUMENT} status response.
 */
public class ValidatingServerInterceptor implements ServerInterceptor {
    private final ValidatorIndex index;

    public ValidatingServerInterceptor(ValidatorIndex index) {
        this.index = index;
    }

    @Override
    public <ReqT, RespT> ServerCall.Listener<ReqT> interceptCall(ServerCall<ReqT, RespT> call, Metadata headers, ServerCallHandler<ReqT, RespT> next) {
        return new ForwardingServerCallListener.SimpleForwardingServerCallListener<ReqT>(next.startCall(call, headers)) {

            // Implementations are free to block for extended periods of time. Implementations are not
            // required to be thread-safe.
            private boolean aborted = false;

            @Override
            public void onMessage(ReqT message) {
                try {
                    index.validatorFor(message.getClass()).assertValid(message);
                    super.onMessage(message);
                } catch (ValidationException ex) {
                    StatusRuntimeException status = ValidationExceptions.asStatusRuntimeException(ex);
                    aborted = true;
                    call.close(status.getStatus(), status.getTrailers());
                }
            }

            @Override
            public void onHalfClose() {
                if (!aborted) {
                    super.onHalfClose();
                }
            }
        };
    }
}
