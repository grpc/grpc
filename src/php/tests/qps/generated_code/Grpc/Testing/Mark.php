<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: test/proto/control.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Request current stats
 *
 * Generated from protobuf message <code>grpc.testing.Mark</code>
 */
class Mark extends \Google\Protobuf\Internal\Message
{
    /**
     * if true, the stats will be reset after taking their snapshot.
     *
     * Generated from protobuf field <code>bool reset = 1;</code>
     */
    private $reset = false;

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\Control::initOnce();
        parent::__construct();
    }

    /**
     * if true, the stats will be reset after taking their snapshot.
     *
     * Generated from protobuf field <code>bool reset = 1;</code>
     * @return bool
     */
    public function getReset()
    {
        return $this->reset;
    }

    /**
     * if true, the stats will be reset after taking their snapshot.
     *
     * Generated from protobuf field <code>bool reset = 1;</code>
     * @param bool $var
     * @return $this
     */
    public function setReset($var)
    {
        GPBUtil::checkBool($var);
        $this->reset = $var;

        return $this;
    }

}

