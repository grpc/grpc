package io.envoyproxy.pgv.grpc;

import com.google.protobuf.Any;
import com.google.rpc.BadRequest;
import com.google.rpc.Code;
import com.google.rpc.Status;
import io.envoyproxy.pgv.ValidationException;
import io.grpc.StatusRuntimeException;
import io.grpc.protobuf.StatusProto;

/**
 * {@code ValidationExceptions} provides utilities for converting {@link ValidationException} objects into gRPC
 * {@code StatusRuntimeException} objects.
 */
public final class ValidationExceptions {
    private ValidationExceptions() {
    }

    /**
     * Convert a {@link ValidationException} into a gRPC {@code StatusRuntimeException}
     * with status code {@code Code.INVALID_ARGUMENT},
     * the {@link ValidationException} exception message,
     * and {@link Any} error details containing {@link BadRequest} with field violation details.
     *
     * @param ex the {@code ValidationException} to convert.
     * @return a gRPC {@code StatusRuntimeException}
     */
    public static StatusRuntimeException asStatusRuntimeException(ValidationException ex) {
        BadRequest badRequestElement = BadRequest.newBuilder()
                .addFieldViolations(BadRequest.FieldViolation.newBuilder().setField(ex.getField()).setDescription(ex.getReason()).build())
                .build();

        return StatusProto.toStatusRuntimeException(Status.newBuilder()
                .setCode(Code.INVALID_ARGUMENT.getNumber())
                .setMessage(ex.getMessage())
                .addDetails(Any.pack(badRequestElement)).build());
    }
}
