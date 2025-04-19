<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# NO CHECKED-IN PROTOBUF GENCODE
# source: src/proto/grpc/testing/messages.proto

namespace Grpc\Testing;

use UnexpectedValueException;

/**
 * The type of payload that should be returned.
 *
 * Protobuf type <code>grpc.testing.PayloadType</code>
 */
class PayloadType
{
    /**
     * Compressable text format.
     *
     * Generated from protobuf enum <code>COMPRESSABLE = 0;</code>
     */
    const COMPRESSABLE = 0;

    private static $valueToName = [
        self::COMPRESSABLE => 'COMPRESSABLE',
    ];

    public static function name($value)
    {
        if (!isset(self::$valueToName[$value])) {
            throw new UnexpectedValueException(sprintf(
                    'Enum %s has no name defined for value %s', __CLASS__, $value));
        }
        return self::$valueToName[$value];
    }


    public static function value($name)
    {
        $const = __CLASS__ . '::' . strtoupper($name);
        if (!defined($const)) {
            throw new UnexpectedValueException(sprintf(
                    'Enum %s has no value defined for name %s', __CLASS__, $name));
        }
        return constant($const);
    }
}

