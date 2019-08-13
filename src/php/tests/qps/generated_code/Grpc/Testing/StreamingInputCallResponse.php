<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: test/proto/messages.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Client-streaming response.
 *
 * Generated from protobuf message <code>grpc.testing.StreamingInputCallResponse</code>
 */
class StreamingInputCallResponse extends \Google\Protobuf\Internal\Message
{
    /**
     * Aggregated size of payloads received from the client.
     *
     * Generated from protobuf field <code>int32 aggregated_payload_size = 1;</code>
     */
    private $aggregated_payload_size = 0;

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\Messages::initOnce();
        parent::__construct();
    }

    /**
     * Aggregated size of payloads received from the client.
     *
     * Generated from protobuf field <code>int32 aggregated_payload_size = 1;</code>
     * @return int
     */
    public function getAggregatedPayloadSize()
    {
        return $this->aggregated_payload_size;
    }

    /**
     * Aggregated size of payloads received from the client.
     *
     * Generated from protobuf field <code>int32 aggregated_payload_size = 1;</code>
     * @param int $var
     * @return $this
     */
    public function setAggregatedPayloadSize($var)
    {
        GPBUtil::checkInt32($var);
        $this->aggregated_payload_size = $var;

        return $this;
    }

}

