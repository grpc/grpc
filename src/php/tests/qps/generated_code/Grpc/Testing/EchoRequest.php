<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: test/proto/echo_messages.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Generated from protobuf message <code>grpc.testing.EchoRequest</code>
 */
class EchoRequest extends \Google\Protobuf\Internal\Message
{
    /**
     * Generated from protobuf field <code>string message = 1;</code>
     */
    private $message = '';
    /**
     * Generated from protobuf field <code>.grpc.testing.RequestParams param = 2;</code>
     */
    private $param = null;

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\EchoMessages::initOnce();
        parent::__construct();
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
     * Generated from protobuf field <code>.grpc.testing.RequestParams param = 2;</code>
     * @return \Grpc\Testing\RequestParams
     */
    public function getParam()
    {
        return $this->param;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.RequestParams param = 2;</code>
     * @param \Grpc\Testing\RequestParams $var
     * @return $this
     */
    public function setParam($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\RequestParams::class);
        $this->param = $var;

        return $this;
    }

}

