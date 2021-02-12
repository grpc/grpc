package io.envoyproxy.pgv;

/**
 * {@code UnimplementedException} indicates a PGV validation is unimplemented for Java.
 */
public class UnimplementedException extends ValidationException {
    public UnimplementedException(String field, String reason) {
        super(field, "UNIMPLEMENTED", reason);
    }
}
