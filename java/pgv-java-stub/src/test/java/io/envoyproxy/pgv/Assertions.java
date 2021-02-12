package io.envoyproxy.pgv;

import org.assertj.core.api.ThrowableAssert.ThrowingCallable;

import static org.assertj.core.api.Assertions.assertThatThrownBy;

public final class Assertions {
    private Assertions() {
        // Intentionally left blank.
    }

    public static void assertValidationException(final ThrowingCallable f) {
        assertThatThrownBy(f).isInstanceOf(ValidationException.class);
    }
}
