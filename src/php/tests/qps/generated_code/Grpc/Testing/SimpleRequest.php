<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: test/proto/messages.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Unary request.
 *
 * Generated from protobuf message <code>grpc.testing.SimpleRequest</code>
 */
class SimpleRequest extends \Google\Protobuf\Internal\Message
{
    /**
     * DEPRECATED, don't use. To be removed shortly.
     * Desired payload type in the response from the server.
     * If response_type is RANDOM, server randomly chooses one from other formats.
     *
     * Generated from protobuf field <code>.grpc.testing.PayloadType response_type = 1;</code>
     */
    private $response_type = 0;
    /**
     * Desired payload size in the response from the server.
     *
     * Generated from protobuf field <code>int32 response_size = 2;</code>
     */
    private $response_size = 0;
    /**
     * Optional input payload sent along with the request.
     *
     * Generated from protobuf field <code>.grpc.testing.Payload payload = 3;</code>
     */
    private $payload = null;
    /**
     * Whether SimpleResponse should include username.
     *
     * Generated from protobuf field <code>bool fill_username = 4;</code>
     */
    private $fill_username = false;
    /**
     * Whether SimpleResponse should include OAuth scope.
     *
     * Generated from protobuf field <code>bool fill_oauth_scope = 5;</code>
     */
    private $fill_oauth_scope = false;
    /**
     * Whether to request the server to compress the response. This field is
     * "nullable" in order to interoperate seamlessly with clients not able to
     * implement the full compression tests by introspecting the call to verify
     * the response's compression status.
     *
     * Generated from protobuf field <code>.grpc.testing.BoolValue response_compressed = 6;</code>
     */
    private $response_compressed = null;
    /**
     * Whether server should return a given status
     *
     * Generated from protobuf field <code>.grpc.testing.EchoStatus response_status = 7;</code>
     */
    private $response_status = null;
    /**
     * Whether the server should expect this request to be compressed.
     *
     * Generated from protobuf field <code>.grpc.testing.BoolValue expect_compressed = 8;</code>
     */
    private $expect_compressed = null;

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\Messages::initOnce();
        parent::__construct();
    }

    /**
     * DEPRECATED, don't use. To be removed shortly.
     * Desired payload type in the response from the server.
     * If response_type is RANDOM, server randomly chooses one from other formats.
     *
     * Generated from protobuf field <code>.grpc.testing.PayloadType response_type = 1;</code>
     * @return int
     */
    public function getResponseType()
    {
        return $this->response_type;
    }

    /**
     * DEPRECATED, don't use. To be removed shortly.
     * Desired payload type in the response from the server.
     * If response_type is RANDOM, server randomly chooses one from other formats.
     *
     * Generated from protobuf field <code>.grpc.testing.PayloadType response_type = 1;</code>
     * @param int $var
     * @return $this
     */
    public function setResponseType($var)
    {
        GPBUtil::checkEnum($var, \Grpc\Testing\PayloadType::class);
        $this->response_type = $var;

        return $this;
    }

    /**
     * Desired payload size in the response from the server.
     *
     * Generated from protobuf field <code>int32 response_size = 2;</code>
     * @return int
     */
    public function getResponseSize()
    {
        return $this->response_size;
    }

    /**
     * Desired payload size in the response from the server.
     *
     * Generated from protobuf field <code>int32 response_size = 2;</code>
     * @param int $var
     * @return $this
     */
    public function setResponseSize($var)
    {
        GPBUtil::checkInt32($var);
        $this->response_size = $var;

        return $this;
    }

    /**
     * Optional input payload sent along with the request.
     *
     * Generated from protobuf field <code>.grpc.testing.Payload payload = 3;</code>
     * @return \Grpc\Testing\Payload
     */
    public function getPayload()
    {
        return $this->payload;
    }

    /**
     * Optional input payload sent along with the request.
     *
     * Generated from protobuf field <code>.grpc.testing.Payload payload = 3;</code>
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
     * Whether SimpleResponse should include username.
     *
     * Generated from protobuf field <code>bool fill_username = 4;</code>
     * @return bool
     */
    public function getFillUsername()
    {
        return $this->fill_username;
    }

    /**
     * Whether SimpleResponse should include username.
     *
     * Generated from protobuf field <code>bool fill_username = 4;</code>
     * @param bool $var
     * @return $this
     */
    public function setFillUsername($var)
    {
        GPBUtil::checkBool($var);
        $this->fill_username = $var;

        return $this;
    }

    /**
     * Whether SimpleResponse should include OAuth scope.
     *
     * Generated from protobuf field <code>bool fill_oauth_scope = 5;</code>
     * @return bool
     */
    public function getFillOauthScope()
    {
        return $this->fill_oauth_scope;
    }

    /**
     * Whether SimpleResponse should include OAuth scope.
     *
     * Generated from protobuf field <code>bool fill_oauth_scope = 5;</code>
     * @param bool $var
     * @return $this
     */
    public function setFillOauthScope($var)
    {
        GPBUtil::checkBool($var);
        $this->fill_oauth_scope = $var;

        return $this;
    }

    /**
     * Whether to request the server to compress the response. This field is
     * "nullable" in order to interoperate seamlessly with clients not able to
     * implement the full compression tests by introspecting the call to verify
     * the response's compression status.
     *
     * Generated from protobuf field <code>.grpc.testing.BoolValue response_compressed = 6;</code>
     * @return \Grpc\Testing\BoolValue
     */
    public function getResponseCompressed()
    {
        return $this->response_compressed;
    }

    /**
     * Whether to request the server to compress the response. This field is
     * "nullable" in order to interoperate seamlessly with clients not able to
     * implement the full compression tests by introspecting the call to verify
     * the response's compression status.
     *
     * Generated from protobuf field <code>.grpc.testing.BoolValue response_compressed = 6;</code>
     * @param \Grpc\Testing\BoolValue $var
     * @return $this
     */
    public function setResponseCompressed($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\BoolValue::class);
        $this->response_compressed = $var;

        return $this;
    }

    /**
     * Whether server should return a given status
     *
     * Generated from protobuf field <code>.grpc.testing.EchoStatus response_status = 7;</code>
     * @return \Grpc\Testing\EchoStatus
     */
    public function getResponseStatus()
    {
        return $this->response_status;
    }

    /**
     * Whether server should return a given status
     *
     * Generated from protobuf field <code>.grpc.testing.EchoStatus response_status = 7;</code>
     * @param \Grpc\Testing\EchoStatus $var
     * @return $this
     */
    public function setResponseStatus($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\EchoStatus::class);
        $this->response_status = $var;

        return $this;
    }

    /**
     * Whether the server should expect this request to be compressed.
     *
     * Generated from protobuf field <code>.grpc.testing.BoolValue expect_compressed = 8;</code>
     * @return \Grpc\Testing\BoolValue
     */
    public function getExpectCompressed()
    {
        return $this->expect_compressed;
    }

    /**
     * Whether the server should expect this request to be compressed.
     *
     * Generated from protobuf field <code>.grpc.testing.BoolValue expect_compressed = 8;</code>
     * @param \Grpc\Testing\BoolValue $var
     * @return $this
     */
    public function setExpectCompressed($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\BoolValue::class);
        $this->expect_compressed = $var;

        return $this;
    }

}

