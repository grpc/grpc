package io.envoyproxy.pgv;

import com.google.protobuf.Any;
import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class AnyValidationTest {

    @Test
    public void inWorks() throws ValidationException {
        String[] set = new String[]{"type.googleapis.com/google.protobuf.Duration"};

        // In
        CollectiveValidation.in("x", Any.newBuilder().setTypeUrl("type.googleapis.com/google.protobuf.Duration").build().getTypeUrl(), set);

        // Not In
        assertThatThrownBy(() -> CollectiveValidation.in("x", Any.newBuilder().setTypeUrl("junk").build().getTypeUrl(), set)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void notInWorks() throws ValidationException {
        String[] set = new String[]{"type.googleapis.com/google.protobuf.Duration"};

        // In
        assertThatThrownBy(() -> CollectiveValidation.notIn("x", Any.newBuilder().setTypeUrl("type.googleapis.com/google.protobuf.Duration").build().getTypeUrl(), set)).isInstanceOf(ValidationException.class);

        // Not In
        CollectiveValidation.notIn("x", Any.newBuilder().setTypeUrl("junk").build().getTypeUrl(), set);
    }
}
