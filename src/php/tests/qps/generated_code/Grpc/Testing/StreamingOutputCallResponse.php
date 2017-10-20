<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/messages.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBUtil;

/**
 * Server-streaming response, as configured by the request and parameters.
 *
 * Generated from protobuf message <code>grpc.testing.StreamingOutputCallResponse</code>
 */
class StreamingOutputCallResponse extends \Google\Protobuf\Internal\Message
{
    /**
     * Payload to increase response size.
     *
     * Generated from protobuf field <code>.grpc.testing.Payload payload = 1;</code>
     */
    private $payload = null;

    public function __construct()
    {
        \GPBMetadata\Src\Proto\Grpc\Testing\Messages::initOnce();
        parent::__construct();
    }

    /**
     * Payload to increase response size.
     *
     * Generated from protobuf field <code>.grpc.testing.Payload payload = 1;</code>
     *
     * @return \Grpc\Testing\Payload
     */
    public function getPayload()
    {
        return $this->payload;
    }

    /**
     * Payload to increase response size.
     *
     * Generated from protobuf field <code>.grpc.testing.Payload payload = 1;</code>
     *
     * @param \Grpc\Testing\Payload $var
     *
     * @return $this
     */
    public function setPayload($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\Payload::class);
        $this->payload = $var;

        return $this;
    }
}
