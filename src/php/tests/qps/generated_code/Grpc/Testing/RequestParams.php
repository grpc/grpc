<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: test/proto/echo_messages.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Generated from protobuf message <code>grpc.testing.RequestParams</code>
 */
class RequestParams extends \Google\Protobuf\Internal\Message
{
    /**
     * Generated from protobuf field <code>bool echo_deadline = 1;</code>
     */
    private $echo_deadline = false;
    /**
     * Generated from protobuf field <code>int32 client_cancel_after_us = 2;</code>
     */
    private $client_cancel_after_us = 0;
    /**
     * Generated from protobuf field <code>int32 server_cancel_after_us = 3;</code>
     */
    private $server_cancel_after_us = 0;
    /**
     * Generated from protobuf field <code>bool echo_metadata = 4;</code>
     */
    private $echo_metadata = false;
    /**
     * Generated from protobuf field <code>bool check_auth_context = 5;</code>
     */
    private $check_auth_context = false;
    /**
     * Generated from protobuf field <code>int32 response_message_length = 6;</code>
     */
    private $response_message_length = 0;
    /**
     * Generated from protobuf field <code>bool echo_peer = 7;</code>
     */
    private $echo_peer = false;
    /**
     * will force check_auth_context.
     *
     * Generated from protobuf field <code>string expected_client_identity = 8;</code>
     */
    private $expected_client_identity = '';
    /**
     * Generated from protobuf field <code>bool skip_cancelled_check = 9;</code>
     */
    private $skip_cancelled_check = false;
    /**
     * Generated from protobuf field <code>string expected_transport_security_type = 10;</code>
     */
    private $expected_transport_security_type = '';
    /**
     * Generated from protobuf field <code>.grpc.testing.DebugInfo debug_info = 11;</code>
     */
    private $debug_info = null;
    /**
     * Server should not see a request with this set.
     *
     * Generated from protobuf field <code>bool server_die = 12;</code>
     */
    private $server_die = false;
    /**
     * Generated from protobuf field <code>string binary_error_details = 13;</code>
     */
    private $binary_error_details = '';
    /**
     * Generated from protobuf field <code>.grpc.testing.ErrorStatus expected_error = 14;</code>
     */
    private $expected_error = null;
    /**
     * Amount to sleep when invoking server
     *
     * Generated from protobuf field <code>int32 server_sleep_us = 15;</code>
     */
    private $server_sleep_us = 0;

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\EchoMessages::initOnce();
        parent::__construct();
    }

    /**
     * Generated from protobuf field <code>bool echo_deadline = 1;</code>
     * @return bool
     */
    public function getEchoDeadline()
    {
        return $this->echo_deadline;
    }

    /**
     * Generated from protobuf field <code>bool echo_deadline = 1;</code>
     * @param bool $var
     * @return $this
     */
    public function setEchoDeadline($var)
    {
        GPBUtil::checkBool($var);
        $this->echo_deadline = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>int32 client_cancel_after_us = 2;</code>
     * @return int
     */
    public function getClientCancelAfterUs()
    {
        return $this->client_cancel_after_us;
    }

    /**
     * Generated from protobuf field <code>int32 client_cancel_after_us = 2;</code>
     * @param int $var
     * @return $this
     */
    public function setClientCancelAfterUs($var)
    {
        GPBUtil::checkInt32($var);
        $this->client_cancel_after_us = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>int32 server_cancel_after_us = 3;</code>
     * @return int
     */
    public function getServerCancelAfterUs()
    {
        return $this->server_cancel_after_us;
    }

    /**
     * Generated from protobuf field <code>int32 server_cancel_after_us = 3;</code>
     * @param int $var
     * @return $this
     */
    public function setServerCancelAfterUs($var)
    {
        GPBUtil::checkInt32($var);
        $this->server_cancel_after_us = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>bool echo_metadata = 4;</code>
     * @return bool
     */
    public function getEchoMetadata()
    {
        return $this->echo_metadata;
    }

    /**
     * Generated from protobuf field <code>bool echo_metadata = 4;</code>
     * @param bool $var
     * @return $this
     */
    public function setEchoMetadata($var)
    {
        GPBUtil::checkBool($var);
        $this->echo_metadata = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>bool check_auth_context = 5;</code>
     * @return bool
     */
    public function getCheckAuthContext()
    {
        return $this->check_auth_context;
    }

    /**
     * Generated from protobuf field <code>bool check_auth_context = 5;</code>
     * @param bool $var
     * @return $this
     */
    public function setCheckAuthContext($var)
    {
        GPBUtil::checkBool($var);
        $this->check_auth_context = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>int32 response_message_length = 6;</code>
     * @return int
     */
    public function getResponseMessageLength()
    {
        return $this->response_message_length;
    }

    /**
     * Generated from protobuf field <code>int32 response_message_length = 6;</code>
     * @param int $var
     * @return $this
     */
    public function setResponseMessageLength($var)
    {
        GPBUtil::checkInt32($var);
        $this->response_message_length = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>bool echo_peer = 7;</code>
     * @return bool
     */
    public function getEchoPeer()
    {
        return $this->echo_peer;
    }

    /**
     * Generated from protobuf field <code>bool echo_peer = 7;</code>
     * @param bool $var
     * @return $this
     */
    public function setEchoPeer($var)
    {
        GPBUtil::checkBool($var);
        $this->echo_peer = $var;

        return $this;
    }

    /**
     * will force check_auth_context.
     *
     * Generated from protobuf field <code>string expected_client_identity = 8;</code>
     * @return string
     */
    public function getExpectedClientIdentity()
    {
        return $this->expected_client_identity;
    }

    /**
     * will force check_auth_context.
     *
     * Generated from protobuf field <code>string expected_client_identity = 8;</code>
     * @param string $var
     * @return $this
     */
    public function setExpectedClientIdentity($var)
    {
        GPBUtil::checkString($var, True);
        $this->expected_client_identity = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>bool skip_cancelled_check = 9;</code>
     * @return bool
     */
    public function getSkipCancelledCheck()
    {
        return $this->skip_cancelled_check;
    }

    /**
     * Generated from protobuf field <code>bool skip_cancelled_check = 9;</code>
     * @param bool $var
     * @return $this
     */
    public function setSkipCancelledCheck($var)
    {
        GPBUtil::checkBool($var);
        $this->skip_cancelled_check = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>string expected_transport_security_type = 10;</code>
     * @return string
     */
    public function getExpectedTransportSecurityType()
    {
        return $this->expected_transport_security_type;
    }

    /**
     * Generated from protobuf field <code>string expected_transport_security_type = 10;</code>
     * @param string $var
     * @return $this
     */
    public function setExpectedTransportSecurityType($var)
    {
        GPBUtil::checkString($var, True);
        $this->expected_transport_security_type = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.DebugInfo debug_info = 11;</code>
     * @return \Grpc\Testing\DebugInfo
     */
    public function getDebugInfo()
    {
        return $this->debug_info;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.DebugInfo debug_info = 11;</code>
     * @param \Grpc\Testing\DebugInfo $var
     * @return $this
     */
    public function setDebugInfo($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\DebugInfo::class);
        $this->debug_info = $var;

        return $this;
    }

    /**
     * Server should not see a request with this set.
     *
     * Generated from protobuf field <code>bool server_die = 12;</code>
     * @return bool
     */
    public function getServerDie()
    {
        return $this->server_die;
    }

    /**
     * Server should not see a request with this set.
     *
     * Generated from protobuf field <code>bool server_die = 12;</code>
     * @param bool $var
     * @return $this
     */
    public function setServerDie($var)
    {
        GPBUtil::checkBool($var);
        $this->server_die = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>string binary_error_details = 13;</code>
     * @return string
     */
    public function getBinaryErrorDetails()
    {
        return $this->binary_error_details;
    }

    /**
     * Generated from protobuf field <code>string binary_error_details = 13;</code>
     * @param string $var
     * @return $this
     */
    public function setBinaryErrorDetails($var)
    {
        GPBUtil::checkString($var, True);
        $this->binary_error_details = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.ErrorStatus expected_error = 14;</code>
     * @return \Grpc\Testing\ErrorStatus
     */
    public function getExpectedError()
    {
        return $this->expected_error;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.ErrorStatus expected_error = 14;</code>
     * @param \Grpc\Testing\ErrorStatus $var
     * @return $this
     */
    public function setExpectedError($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\ErrorStatus::class);
        $this->expected_error = $var;

        return $this;
    }

    /**
     * Amount to sleep when invoking server
     *
     * Generated from protobuf field <code>int32 server_sleep_us = 15;</code>
     * @return int
     */
    public function getServerSleepUs()
    {
        return $this->server_sleep_us;
    }

    /**
     * Amount to sleep when invoking server
     *
     * Generated from protobuf field <code>int32 server_sleep_us = 15;</code>
     * @param int $var
     * @return $this
     */
    public function setServerSleepUs($var)
    {
        GPBUtil::checkInt32($var);
        $this->server_sleep_us = $var;

        return $this;
    }

}

