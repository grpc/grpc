<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/control.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Protobuf type <code>grpc.testing.ClientStatus</code>
 */
class ClientStatus extends \Google\Protobuf\Internal\Message
{
    /**
     * <code>.grpc.testing.ClientStats stats = 1;</code>
     */
    private $stats = null;

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\Control::initOnce();
        parent::__construct();
    }

    /**
     * <code>.grpc.testing.ClientStats stats = 1;</code>
     */
    public function getStats()
    {
        return $this->stats;
    }

    /**
     * <code>.grpc.testing.ClientStats stats = 1;</code>
     */
    public function setStats(&$var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\ClientStats::class);
        $this->stats = $var;
    }

}

