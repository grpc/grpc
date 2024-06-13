<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# NO CHECKED-IN PROTOBUF GENCODE
# source: src/proto/grpc/testing/messages.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Accumulated stats for RPCs sent by a test client.
 *
 * Generated from protobuf message <code>grpc.testing.LoadBalancerAccumulatedStatsResponse</code>
 */
class LoadBalancerAccumulatedStatsResponse extends \Google\Protobuf\Internal\Message
{
    /**
     * The total number of RPCs have ever issued for each type.
     * Deprecated: use stats_per_method.rpcs_started instead.
     *
     * Generated from protobuf field <code>map<string, int32> num_rpcs_started_by_method = 1 [deprecated = true];</code>
     * @deprecated
     */
    private $num_rpcs_started_by_method;
    /**
     * The total number of RPCs have ever completed successfully for each type.
     * Deprecated: use stats_per_method.result instead.
     *
     * Generated from protobuf field <code>map<string, int32> num_rpcs_succeeded_by_method = 2 [deprecated = true];</code>
     * @deprecated
     */
    private $num_rpcs_succeeded_by_method;
    /**
     * The total number of RPCs have ever failed for each type.
     * Deprecated: use stats_per_method.result instead.
     *
     * Generated from protobuf field <code>map<string, int32> num_rpcs_failed_by_method = 3 [deprecated = true];</code>
     * @deprecated
     */
    private $num_rpcs_failed_by_method;
    /**
     * Per-method RPC statistics.  The key is the RpcType in string form; e.g.
     * 'EMPTY_CALL' or 'UNARY_CALL'
     *
     * Generated from protobuf field <code>map<string, .grpc.testing.LoadBalancerAccumulatedStatsResponse.MethodStats> stats_per_method = 4;</code>
     */
    private $stats_per_method;

    /**
     * Constructor.
     *
     * @param array $data {
     *     Optional. Data for populating the Message object.
     *
     *     @type array|\Google\Protobuf\Internal\MapField $num_rpcs_started_by_method
     *           The total number of RPCs have ever issued for each type.
     *           Deprecated: use stats_per_method.rpcs_started instead.
     *     @type array|\Google\Protobuf\Internal\MapField $num_rpcs_succeeded_by_method
     *           The total number of RPCs have ever completed successfully for each type.
     *           Deprecated: use stats_per_method.result instead.
     *     @type array|\Google\Protobuf\Internal\MapField $num_rpcs_failed_by_method
     *           The total number of RPCs have ever failed for each type.
     *           Deprecated: use stats_per_method.result instead.
     *     @type array|\Google\Protobuf\Internal\MapField $stats_per_method
     *           Per-method RPC statistics.  The key is the RpcType in string form; e.g.
     *           'EMPTY_CALL' or 'UNARY_CALL'
     * }
     */
    public function __construct($data = NULL) {
        \GPBMetadata\Src\Proto\Grpc\Testing\Messages::initOnce();
        parent::__construct($data);
    }

    /**
     * The total number of RPCs have ever issued for each type.
     * Deprecated: use stats_per_method.rpcs_started instead.
     *
     * Generated from protobuf field <code>map<string, int32> num_rpcs_started_by_method = 1 [deprecated = true];</code>
     * @return \Google\Protobuf\Internal\MapField
     * @deprecated
     */
    public function getNumRpcsStartedByMethod()
    {
        @trigger_error('num_rpcs_started_by_method is deprecated.', E_USER_DEPRECATED);
        return $this->num_rpcs_started_by_method;
    }

    /**
     * The total number of RPCs have ever issued for each type.
     * Deprecated: use stats_per_method.rpcs_started instead.
     *
     * Generated from protobuf field <code>map<string, int32> num_rpcs_started_by_method = 1 [deprecated = true];</code>
     * @param array|\Google\Protobuf\Internal\MapField $var
     * @return $this
     * @deprecated
     */
    public function setNumRpcsStartedByMethod($var)
    {
        @trigger_error('num_rpcs_started_by_method is deprecated.', E_USER_DEPRECATED);
        $arr = GPBUtil::checkMapField($var, \Google\Protobuf\Internal\GPBType::STRING, \Google\Protobuf\Internal\GPBType::INT32);
        $this->num_rpcs_started_by_method = $arr;

        return $this;
    }

    /**
     * The total number of RPCs have ever completed successfully for each type.
     * Deprecated: use stats_per_method.result instead.
     *
     * Generated from protobuf field <code>map<string, int32> num_rpcs_succeeded_by_method = 2 [deprecated = true];</code>
     * @return \Google\Protobuf\Internal\MapField
     * @deprecated
     */
    public function getNumRpcsSucceededByMethod()
    {
        @trigger_error('num_rpcs_succeeded_by_method is deprecated.', E_USER_DEPRECATED);
        return $this->num_rpcs_succeeded_by_method;
    }

    /**
     * The total number of RPCs have ever completed successfully for each type.
     * Deprecated: use stats_per_method.result instead.
     *
     * Generated from protobuf field <code>map<string, int32> num_rpcs_succeeded_by_method = 2 [deprecated = true];</code>
     * @param array|\Google\Protobuf\Internal\MapField $var
     * @return $this
     * @deprecated
     */
    public function setNumRpcsSucceededByMethod($var)
    {
        @trigger_error('num_rpcs_succeeded_by_method is deprecated.', E_USER_DEPRECATED);
        $arr = GPBUtil::checkMapField($var, \Google\Protobuf\Internal\GPBType::STRING, \Google\Protobuf\Internal\GPBType::INT32);
        $this->num_rpcs_succeeded_by_method = $arr;

        return $this;
    }

    /**
     * The total number of RPCs have ever failed for each type.
     * Deprecated: use stats_per_method.result instead.
     *
     * Generated from protobuf field <code>map<string, int32> num_rpcs_failed_by_method = 3 [deprecated = true];</code>
     * @return \Google\Protobuf\Internal\MapField
     * @deprecated
     */
    public function getNumRpcsFailedByMethod()
    {
        @trigger_error('num_rpcs_failed_by_method is deprecated.', E_USER_DEPRECATED);
        return $this->num_rpcs_failed_by_method;
    }

    /**
     * The total number of RPCs have ever failed for each type.
     * Deprecated: use stats_per_method.result instead.
     *
     * Generated from protobuf field <code>map<string, int32> num_rpcs_failed_by_method = 3 [deprecated = true];</code>
     * @param array|\Google\Protobuf\Internal\MapField $var
     * @return $this
     * @deprecated
     */
    public function setNumRpcsFailedByMethod($var)
    {
        @trigger_error('num_rpcs_failed_by_method is deprecated.', E_USER_DEPRECATED);
        $arr = GPBUtil::checkMapField($var, \Google\Protobuf\Internal\GPBType::STRING, \Google\Protobuf\Internal\GPBType::INT32);
        $this->num_rpcs_failed_by_method = $arr;

        return $this;
    }

    /**
     * Per-method RPC statistics.  The key is the RpcType in string form; e.g.
     * 'EMPTY_CALL' or 'UNARY_CALL'
     *
     * Generated from protobuf field <code>map<string, .grpc.testing.LoadBalancerAccumulatedStatsResponse.MethodStats> stats_per_method = 4;</code>
     * @return \Google\Protobuf\Internal\MapField
     */
    public function getStatsPerMethod()
    {
        return $this->stats_per_method;
    }

    /**
     * Per-method RPC statistics.  The key is the RpcType in string form; e.g.
     * 'EMPTY_CALL' or 'UNARY_CALL'
     *
     * Generated from protobuf field <code>map<string, .grpc.testing.LoadBalancerAccumulatedStatsResponse.MethodStats> stats_per_method = 4;</code>
     * @param array|\Google\Protobuf\Internal\MapField $var
     * @return $this
     */
    public function setStatsPerMethod($var)
    {
        $arr = GPBUtil::checkMapField($var, \Google\Protobuf\Internal\GPBType::STRING, \Google\Protobuf\Internal\GPBType::MESSAGE, \Grpc\Testing\LoadBalancerAccumulatedStatsResponse\MethodStats::class);
        $this->stats_per_method = $arr;

        return $this;
    }

}

