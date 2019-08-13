<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: test/proto/control.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Generated from protobuf message <code>grpc.testing.ServerStatus</code>
 */
class ServerStatus extends \Google\Protobuf\Internal\Message
{
    /**
     * Generated from protobuf field <code>.grpc.testing.ServerStats stats = 1;</code>
     */
    private $stats = null;
    /**
     * the port bound by the server
     *
     * Generated from protobuf field <code>int32 port = 2;</code>
     */
    private $port = 0;
    /**
     * Number of cores available to the server
     *
     * Generated from protobuf field <code>int32 cores = 3;</code>
     */
    private $cores = 0;

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\Control::initOnce();
        parent::__construct();
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.ServerStats stats = 1;</code>
     * @return \Grpc\Testing\ServerStats
     */
    public function getStats()
    {
        return $this->stats;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.ServerStats stats = 1;</code>
     * @param \Grpc\Testing\ServerStats $var
     * @return $this
     */
    public function setStats($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\ServerStats::class);
        $this->stats = $var;

        return $this;
    }

    /**
     * the port bound by the server
     *
     * Generated from protobuf field <code>int32 port = 2;</code>
     * @return int
     */
    public function getPort()
    {
        return $this->port;
    }

    /**
     * the port bound by the server
     *
     * Generated from protobuf field <code>int32 port = 2;</code>
     * @param int $var
     * @return $this
     */
    public function setPort($var)
    {
        GPBUtil::checkInt32($var);
        $this->port = $var;

        return $this;
    }

    /**
     * Number of cores available to the server
     *
     * Generated from protobuf field <code>int32 cores = 3;</code>
     * @return int
     */
    public function getCores()
    {
        return $this->cores;
    }

    /**
     * Number of cores available to the server
     *
     * Generated from protobuf field <code>int32 cores = 3;</code>
     * @param int $var
     * @return $this
     */
    public function setCores($var)
    {
        GPBUtil::checkInt32($var);
        $this->cores = $var;

        return $this;
    }

}

