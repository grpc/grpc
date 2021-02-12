package io.envoyproxy.pgv;

/**
 * {@code Validator} is the base interface for all generated PGV validators.
 * @param <T> The type to validate
 */
@FunctionalInterface
public interface ValidatorImpl<T> {
    /**
     * Asserts validation rules on a protobuf object.
     *
     * @param proto the protobuf object to validate.
     * @throws ValidationException with the first validation error encountered.
     */
    void assertValid(T proto, ValidatorIndex index) throws ValidationException;

    ValidatorImpl ALWAYS_VALID = (proto, index) -> {
        // Do nothing. Always valid.
    };
}
