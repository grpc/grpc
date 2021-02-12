package io.envoyproxy.pgv;

import com.google.protobuf.Duration;
import com.google.protobuf.Timestamp;
import com.google.protobuf.util.Durations;
import com.google.protobuf.util.Timestamps;

/**
 * {@code TimestampValidation} implements PGV validation for protobuf {@code Timestamp} fields.
 */
public final class TimestampValidation {
    private TimestampValidation() { }

    public static void within(String field, Timestamp value, Duration duration, Timestamp when) throws ValidationException {
        Duration between = Timestamps.between(when, value);
        if (Long.compare(Math.abs(Durations.toNanos(between)), Math.abs(Durations.toNanos(duration))) == 1) {
            throw new ValidationException(field, value, "value must be within " + Durations.toString(duration) + " of " + Timestamps.toString(when));
        }
    }

    /**
     * Converts {@code seconds} and {@code nanos} to a protobuf {@code Timestamp}.
     */
    public static Timestamp toTimestamp(long seconds, int nanos) {
        return Timestamp.newBuilder()
                .setSeconds(seconds)
                .setNanos(nanos)
                .build();
    }

    /**
     * Converts {@code seconds} and {@code nanos} to a protobuf {@code Duration}.
     */
    public static Duration toDuration(long seconds, long nanos) {
        return Duration.newBuilder()
                .setSeconds(seconds)
                .setNanos((int) nanos)
                .build();
    }

    public static Timestamp currentTimestamp() {
        return Timestamps.fromMillis(System.currentTimeMillis());
    }
}
