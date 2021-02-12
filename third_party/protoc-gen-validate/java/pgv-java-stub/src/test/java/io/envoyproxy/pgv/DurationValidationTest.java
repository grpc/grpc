package io.envoyproxy.pgv;

import com.google.protobuf.Duration;
import com.google.protobuf.util.Durations;
import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class DurationValidationTest {
    @Test
    public void lessThanWorks() throws ValidationException {
        // Less
        ComparativeValidation.lessThan("x", Durations.fromSeconds(10), Durations.fromSeconds(20), Durations.comparator());
        // Equal
        assertThatThrownBy(() -> ComparativeValidation.lessThan("x", Durations.fromSeconds(10), Durations.fromSeconds(10), Durations.comparator())).isInstanceOf(ValidationException.class);
        // Greater
        assertThatThrownBy(() -> ComparativeValidation.lessThan("x", Durations.fromSeconds(20), Durations.fromSeconds(10), Durations.comparator())).isInstanceOf(ValidationException.class);
    }

    @Test
    public void lessThanOrEqualsWorks() throws ValidationException {
        // Less
        ComparativeValidation.lessThanOrEqual("x", Durations.fromSeconds(10), Durations.fromSeconds(20), Durations.comparator());
        // Equal
        ComparativeValidation.lessThanOrEqual("x", Durations.fromSeconds(10), Durations.fromSeconds(10), Durations.comparator());
        // Greater
        assertThatThrownBy(() -> ComparativeValidation.lessThanOrEqual("x", Durations.fromSeconds(20), Durations.fromSeconds(10), Durations.comparator())).isInstanceOf(ValidationException.class);
    }

    @Test
    public void greaterThanWorks() throws ValidationException {
        // Less
        assertThatThrownBy(() -> ComparativeValidation.greaterThan("x", Durations.fromSeconds(10), Durations.fromSeconds(20), Durations.comparator())).isInstanceOf(ValidationException.class);
        // Equal
        assertThatThrownBy(() -> ComparativeValidation.greaterThan("x", Durations.fromSeconds(10), Durations.fromSeconds(10), Durations.comparator())).isInstanceOf(ValidationException.class);
        // Greater
        ComparativeValidation.greaterThan("x", Durations.fromSeconds(20), Durations.fromSeconds(10), Durations.comparator());
    }

    @Test
    public void greaterThanOrEqualsWorks() throws ValidationException {
        // Less
        assertThatThrownBy(() -> ComparativeValidation.greaterThanOrEqual("x", Durations.fromSeconds(10), Durations.fromSeconds(20), Durations.comparator())).isInstanceOf(ValidationException.class);
        // Equal
        ComparativeValidation.greaterThanOrEqual("x", Durations.fromSeconds(10), Durations.fromSeconds(10), Durations.comparator());
        // Greater
        ComparativeValidation.greaterThanOrEqual("x", Durations.fromSeconds(20), Durations.fromSeconds(10), Durations.comparator());
    }

    @Test
    public void inWorks() throws ValidationException {
        Duration[] set = new Duration[]{TimestampValidation.toDuration(1, 0), TimestampValidation.toDuration(2, 0)};
        // In
        CollectiveValidation.in("x", TimestampValidation.toDuration(1, 0), set);
        // Not In
        assertThatThrownBy(() -> CollectiveValidation.in("x", TimestampValidation.toDuration(3, 0), set)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void notInWorks() throws ValidationException {
        Duration[] set = new Duration[]{TimestampValidation.toDuration(1, 0), TimestampValidation.toDuration(2, 0)};
        // In
        assertThatThrownBy(() -> CollectiveValidation.notIn("x", TimestampValidation.toDuration(1, 0), set)).isInstanceOf(ValidationException.class);
        // Not In
        CollectiveValidation.notIn("x", TimestampValidation.toDuration(3, 0), set);
    }
}
