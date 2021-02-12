package io.envoyproxy.pgv;

import com.google.protobuf.Duration;
import com.google.protobuf.Timestamp;
import com.google.protobuf.util.Durations;
import com.google.protobuf.util.Timestamps;
import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class TimestampValidationTest {
    @Test
    public void lessThanWorks() throws ValidationException {
        // Less
        ComparativeValidation.lessThan("x", Timestamps.fromSeconds(10), Timestamps.fromSeconds(20), Timestamps.comparator());
        // Equal
        assertThatThrownBy(() -> ComparativeValidation.lessThan("x", Timestamps.fromSeconds(10), Timestamps.fromSeconds(10), Timestamps.comparator())).isInstanceOf(ValidationException.class);
        // Greater
        assertThatThrownBy(() -> ComparativeValidation.lessThan("x", Timestamps.fromSeconds(20), Timestamps.fromSeconds(10), Timestamps.comparator())).isInstanceOf(ValidationException.class);
    }

    @Test
    public void lessThanOrEqualsWorks() throws ValidationException {
        // Less
        ComparativeValidation.lessThanOrEqual("x", Timestamps.fromSeconds(10), Timestamps.fromSeconds(20), Timestamps.comparator());
        // Equal
        ComparativeValidation.lessThanOrEqual("x", Timestamps.fromSeconds(10), Timestamps.fromSeconds(10), Timestamps.comparator());
        // Greater
        assertThatThrownBy(() -> ComparativeValidation.lessThanOrEqual("x", Timestamps.fromSeconds(20), Timestamps.fromSeconds(10), Timestamps.comparator())).isInstanceOf(ValidationException.class);
    }

    @Test
    public void greaterThanWorks() throws ValidationException {
        // Less
        assertThatThrownBy(() -> ComparativeValidation.greaterThan("x", Timestamps.fromSeconds(10), Timestamps.fromSeconds(20), Timestamps.comparator())).isInstanceOf(ValidationException.class);
        // Equal
        assertThatThrownBy(() -> ComparativeValidation.greaterThan("x", Timestamps.fromSeconds(10), Timestamps.fromSeconds(10), Timestamps.comparator())).isInstanceOf(ValidationException.class);
        // Greater
        ComparativeValidation.greaterThan("x", Timestamps.fromSeconds(20), Timestamps.fromSeconds(10), Timestamps.comparator());
    }

    @Test
    public void greaterThanOrEqualsWorks() throws ValidationException {
        // Less
        assertThatThrownBy(() -> ComparativeValidation.greaterThanOrEqual("x", Timestamps.fromSeconds(10), Timestamps.fromSeconds(20), Timestamps.comparator())).isInstanceOf(ValidationException.class);
        // Equal
        ComparativeValidation.greaterThanOrEqual("x", Timestamps.fromSeconds(10), Timestamps.fromSeconds(10), Timestamps.comparator());
        // Greater
        ComparativeValidation.greaterThanOrEqual("x", Timestamps.fromSeconds(20), Timestamps.fromSeconds(10), Timestamps.comparator());
    }

    @Test
    public void withinWorks() throws ValidationException {
        Timestamp when = Timestamps.fromSeconds(20);
        Duration duration = Durations.fromSeconds(5);

        // Less
        TimestampValidation.within("x", Timestamps.fromSeconds(18), duration, when);
        TimestampValidation.within("x", Timestamps.fromSeconds(20), duration, when);
        TimestampValidation.within("x", Timestamps.fromSeconds(22), duration, when);

        // Equal
        TimestampValidation.within("x", Timestamps.fromSeconds(15), duration, when);
        TimestampValidation.within("x", Timestamps.fromSeconds(25), duration, when);

        // Greater
        assertThatThrownBy(() -> TimestampValidation.within("x", Timestamps.fromSeconds(10), duration, when)).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> TimestampValidation.within("x", Timestamps.fromSeconds(30), duration, when)).isInstanceOf(ValidationException.class);
    }
}
