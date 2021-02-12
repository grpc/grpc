package io.envoyproxy.pgv;

import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class CollectiveValidationTest {
    @Test
    public void inWorks() throws ValidationException {
        String[] set = new String[]{"foo", "bar"};
        // In
        CollectiveValidation.in("x", "foo", set);
        // Not In
        assertThatThrownBy(() -> CollectiveValidation.in("x", "baz", set)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void notInWorks() throws ValidationException {
        String[] set = new String[]{"foo", "bar"};
        // In
        assertThatThrownBy(() -> CollectiveValidation.notIn("x", "foo", set)).isInstanceOf(ValidationException.class);
        // Not In
        CollectiveValidation.notIn("x", "baz", set);
    }
}
