package io.envoyproxy.pgv;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * {@code RepeatedValidation} implements PGV validators for collection-type validators.
 */
public final class RepeatedValidation {
    private RepeatedValidation() {
    }

    public static <T> void minItems(String field, List<T> values, int expected) throws ValidationException {
        if (values.size() < expected) {
            throw new ValidationException(field, values, "must have at least " + expected + " items");
        }
    }

    public static <T> void maxItems(String field, List<T> values, int expected) throws ValidationException {
        if (values.size() > expected) {
            throw new ValidationException(field, values, "must have at most " + expected + " items");
        }
    }

    public static <T> void unique(String field, List<T> values) throws ValidationException {
        Set<T> seen = new HashSet<>();
        for (T value : values) {
            // Abort at the first sign of a duplicate
            if (!seen.add(value)) {
                throw new ValidationException(field, values, "must have all unique values");
            }
        }
    }

    @FunctionalInterface
    public interface ValidationConsumer<T> {
        void accept(T value) throws ValidationException;
    }

    public static <T> void forEach(List<T> values, ValidationConsumer<T> consumer) throws ValidationException {
        for (T value : values) {
            consumer.accept(value);
        }
    }
}
