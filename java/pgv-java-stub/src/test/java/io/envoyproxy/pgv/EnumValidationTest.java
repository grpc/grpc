package io.envoyproxy.pgv;

import io.envoyproxy.pvg.cases.Enum;
import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class EnumValidationTest {
    @Test
    public void definedOnlyWorks() throws ValidationException {
        // Defined
        EnumValidation.definedOnly("x", Enum.TestEnum.ONE);
        // Not Defined
        assertThatThrownBy(() -> EnumValidation.definedOnly("x", Enum.TestEnum.UNRECOGNIZED)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void inWorks() throws ValidationException {
        Enum.TestEnum[] set = new Enum.TestEnum[]{
                Enum.TestEnum.forNumber(0),
                Enum.TestEnum.forNumber(2),
        };
        // In
        CollectiveValidation.in("x", Enum.TestEnum.TWO, set);
        // Not In
        assertThatThrownBy(() -> CollectiveValidation.in("x", Enum.TestEnum.ONE, set)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void notInWorks() throws ValidationException {
        Enum.TestEnum[] set = new Enum.TestEnum[]{
                Enum.TestEnum.forNumber(0),
                Enum.TestEnum.forNumber(2),
        };
        // In
        assertThatThrownBy(() -> CollectiveValidation.notIn("x", Enum.TestEnum.TWO, set)).isInstanceOf(ValidationException.class);
        // Not In
        CollectiveValidation.notIn("x", Enum.TestEnum.ONE, set);
    }
}
