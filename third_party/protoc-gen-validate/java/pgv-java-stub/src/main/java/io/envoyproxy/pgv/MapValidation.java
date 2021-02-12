package io.envoyproxy.pgv;

import java.util.Collection;
import java.util.Map;

/**
 * {@code MapValidation} implements PGV validation for protobuf {@code Map} fields.
 */
public final class MapValidation {
    private MapValidation() {
    }

    public static void min(String field, Map value, int expected) throws ValidationException {
        if (Math.min(value.size(), expected) != expected) {
            throw new ValidationException(field, value, "value size must be at least " + expected);
        }
    }

    public static void max(String field, Map value, int expected) throws ValidationException {
        if (Math.max(value.size(), expected) != expected) {
            throw new ValidationException(field, value, "value size must not exceed " + expected);
        }
    }

    public static void noSparse(String field, Map value) throws ValidationException {
        throw new UnimplementedException(field, "no_sparse validation is not implemented for Java because protobuf maps cannot be sparse in Java");
    }

    @FunctionalInterface
    public interface MapValidator<T> {
        void accept(T val) throws ValidationException;
    }

    public static <T> void validateParts(Collection<T> vals, MapValidator<T> validator) throws ValidationException {
       for (T val : vals) {
           validator.accept(val);
       }
    }
}
