package io.envoyproxy.pgv;

import org.junit.Test;

import java.util.Arrays;

import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class RepeatedValidationTest {
    @Test
    public void minItemsWorks() throws ValidationException {
        // More
        RepeatedValidation.minItems("x", Arrays.asList(10, 20, 30), 2);
        // Equal
        RepeatedValidation.minItems("x", Arrays.asList(10, 20), 2);
        // Fewer
        assertThatThrownBy(() -> RepeatedValidation.minItems("x", Arrays.asList(10), 2)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void maxItemsWorks() throws ValidationException {
        // More
        assertThatThrownBy(() -> RepeatedValidation.maxItems("x", Arrays.asList(10, 20, 30), 2)).isInstanceOf(ValidationException.class);
        // Equal
        RepeatedValidation.maxItems("x", Arrays.asList(10, 20), 2);
        // Fewer
        RepeatedValidation.maxItems("x", Arrays.asList(10), 2);
    }

    @Test
    public void uniqueWorks() throws ValidationException {
        // Unique
        RepeatedValidation.unique("x", Arrays.asList(10, 20, 30, 40));
        // Duplicate
        assertThatThrownBy(() -> RepeatedValidation.unique("x", Arrays.asList(10, 20, 20, 30, 30, 40))).isInstanceOf(ValidationException.class);
    }
}
