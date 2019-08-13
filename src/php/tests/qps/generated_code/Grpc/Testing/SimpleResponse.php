<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: test/proto/messages.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Unary response, as configured by the request.
 *
 * Generated from protobuf message <code>grpc.testing.SimpleResponse</code>
 */
class SimpleResponse extends \Google\Protobuf\Internal\Message
{
    /**
     * Payload to increase message size.
     *
     * Generated from protobuf field <code>.grpc.testing.Payload payload = 1;</code>
     */
    private $payload = null;
    /**
     * The user the request came from, for verifying authentication was
     * successful when the client expected it.
     *
     * Generated from protobuf field <code>string username = 2;</code>
     */
    private $username = '';
    /**
     * OAuth scope.
     *
     * Generated from protobuf field <code>string oauth_scope = 3;</code>
     */
    private $oauth_scope = '';

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\Messages::initOnce();
        parent::__construct();
    }

    /**
     * Payload to increase message size.
     *
     * Generated from protobuf field <code>.grpc.testing.Payload payload = 1;</code>
     * @return \Grpc\Testing\Payload
     */
    public function getPayload()
    {
        return $this->payload;
    }

    /**
     * Payload to increase message size.
     *
     * Generated from protobuf field <code>.grpc.testing.Payload payload = 1;</code>
     * @param \Grpc\Testing\Payload $var
     * @return $this
     */
    public function setPayload($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\Payload::class);
        $this->payload = $var;

        return $this;
    }

    /**
     * The user the request came from, for verifying authentication was
     * successful when the client expected it.
     *
     * Generated from protobuf field <code>string username = 2;</code>
     * @return string
     */
    public function getUsername()
    {
        return $this->username;
    }

    /**
     * The user the request came from, for verifying authentication was
     * successful when the client expected it.
     *
     * Generated from protobuf field <code>string username = 2;</code>
     * @param string $var
     * @return $this
     */
    public function setUsername($var)
    {
        GPBUtil::checkString($var, True);
        $this->username = $var;

        return $this;
    }

    /**
     * OAuth scope.
     *
     * Generated from protobuf field <code>string oauth_scope = 3;</code>
     * @return string
     */
    public function getOauthScope()
    {
        return $this->oauth_scope;
    }

    /**
     * OAuth scope.
     *
     * Generated from protobuf field <code>string oauth_scope = 3;</code>
     * @param string $var
     * @return $this
     */
    public function setOauthScope($var)
    {
        GPBUtil::checkString($var, True);
        $this->oauth_scope = $var;

        return $this;
    }

}

