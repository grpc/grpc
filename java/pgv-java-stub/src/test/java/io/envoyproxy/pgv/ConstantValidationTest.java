package io.envoyproxy.pgv;

import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class ConstantValidationTest {
    @Test
    public void constantBooleanWorks() throws ValidationException {
        ConstantValidation.constant("x", true, true);
        assertThatThrownBy(() -> ConstantValidation.constant("x", true, false)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void constantFloatWorks() throws ValidationException {
        ConstantValidation.constant("x", 1.23F, 1.23F);
        assertThatThrownBy(() -> ConstantValidation.constant("x", 1.23F, 3.21F)).isInstanceOf(ValidationException.class);
    }
}
