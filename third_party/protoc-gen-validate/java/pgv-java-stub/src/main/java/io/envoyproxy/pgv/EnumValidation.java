package io.envoyproxy.pgv;

import com.google.protobuf.ProtocolMessageEnum;

/**
 * {@code EnumValidation} implements PGV validation for protobuf enumerated types.
 */
public final class EnumValidation {
    private EnumValidation() {
    }

    public static void definedOnly(String field, ProtocolMessageEnum value) throws ValidationException {
        if (value.toString().equals("UNRECOGNIZED")) {
            throw new ValidationException(field, value, "value is not a defined Enum value " + value);
        }
    }
}
