<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/echo_messages.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Generated from protobuf message <code>grpc.testing.EchoResponse</code>
 */
class EchoResponse extends \Google\Protobuf\Internal\Message
{
    /**
     * Generated from protobuf field <code>string message = 1;</code>
     */
    protected $message = '';
    /**
     * Generated from protobuf field <code>.grpc.testing.ResponseParams param = 2;</code>
     */
    protected $param = null;

    /**
     * Constructor.
     *
     * @param array $data {
     *     Optional. Data for populating the Message object.
     *
     *     @type string $message
     *     @type \Grpc\Testing\ResponseParams $param
     * }
     */
    public function __construct($data = NULL) {
        \GPBMetadata\Src\Proto\Grpc\Testing\EchoMessages::initOnce();
        parent::__construct($data);
    }

    /**
     * Generated from protobuf field <code>string message = 1;</code>
     * @return string
     */
    public function getMessage()
    {
        return $this->message;
    }

    /**
     * Generated from protobuf field <code>string message = 1;</code>
     * @param string $var
     * @return $this
     */
    public function setMessage($var)
    {
        GPBUtil::checkString($var, True);
        $this->message = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.ResponseParams param = 2;</code>
     * @return \Grpc\Testing\ResponseParams
     */
    public function getParam()
    {
        return $this->param;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.ResponseParams param = 2;</code>
     * @param \Grpc\Testing\ResponseParams $var
     * @return $this
     */
    public function setParam($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\ResponseParams::class);
        $this->param = $var;

        return $this;
    }

}

