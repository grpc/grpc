package io.envoyproxy.pgv;

/**
 * Base class for failed field validations.
 */
public class ValidationException extends Exception {
    private String field;
    private Object value;
    private String reason;

    public ValidationException(String field, Object value, String reason) {
        super(field + ": " + reason + " - Got " + value.toString());
        this.field = field;
        this.value = value;
        this.reason = reason;
    }

    public String getField() {
        return field;
    }

    public Object getValue() {
        return value;
    }

    public String getReason() {
        return reason;
    }
}
