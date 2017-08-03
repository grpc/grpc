<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/control.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Protobuf type <code>grpc.testing.ClientArgs</code>
 */
class ClientArgs extends \Google\Protobuf\Internal\Message
{
    protected $argtype;

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\Control::initOnce();
        parent::__construct();
    }

    /**
     * <code>.grpc.testing.ClientConfig setup = 1;</code>
     */
    public function getSetup()
    {
        return $this->readOneof(1);
    }

    /**
     * <code>.grpc.testing.ClientConfig setup = 1;</code>
     */
    public function setSetup(&$var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\ClientConfig::class);
        $this->writeOneof(1, $var);
    }

    /**
     * <code>.grpc.testing.Mark mark = 2;</code>
     */
    public function getMark()
    {
        return $this->readOneof(2);
    }

    /**
     * <code>.grpc.testing.Mark mark = 2;</code>
     */
    public function setMark(&$var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\Mark::class);
        $this->writeOneof(2, $var);
    }

    public function getArgtype()
    {
        return $this->whichOneof("argtype");
    }

}

