<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/stats.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBUtil;

/**
 * Generated from protobuf message <code>grpc.testing.ClientStats</code>.
 */
class ClientStats extends \Google\Protobuf\Internal\Message
{
    /**
     * Latency histogram. Data points are in nanoseconds.
     *
     * Generated from protobuf field <code>.grpc.testing.HistogramData latencies = 1;</code>
     */
    private $latencies = null;
    /**
     * See ServerStats for details.
     *
     * Generated from protobuf field <code>double time_elapsed = 2;</code>
     */
    private $time_elapsed = 0.0;
    /**
     * Generated from protobuf field <code>double time_user = 3;</code>.
     */
    private $time_user = 0.0;
    /**
     * Generated from protobuf field <code>double time_system = 4;</code>.
     */
    private $time_system = 0.0;
    /**
     * Number of failed requests (one row per status code seen).
     *
     * Generated from protobuf field <code>repeated .grpc.testing.RequestResultCount request_results = 5;</code>
     */
    private $request_results;
    /**
     * Number of polls called inside completion queue.
     *
     * Generated from protobuf field <code>uint64 cq_poll_count = 6;</code>
     */
    private $cq_poll_count = 0;
    /**
     * Core library stats.
     *
     * Generated from protobuf field <code>.grpc.core.Stats core_stats = 7;</code>
     */
    private $core_stats = null;

    public function __construct()
    {
        \GPBMetadata\Src\Proto\Grpc\Testing\Stats::initOnce();
        parent::__construct();
    }

    /**
     * Latency histogram. Data points are in nanoseconds.
     *
     * Generated from protobuf field <code>.grpc.testing.HistogramData latencies = 1;</code>
     *
     * @return \Grpc\Testing\HistogramData
     */
    public function getLatencies()
    {
        return $this->latencies;
    }

    /**
     * Latency histogram. Data points are in nanoseconds.
     *
     * Generated from protobuf field <code>.grpc.testing.HistogramData latencies = 1;</code>
     *
     * @param \Grpc\Testing\HistogramData $var
     *
     * @return $this
     */
    public function setLatencies($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\HistogramData::class);
        $this->latencies = $var;

        return $this;
    }

    /**
     * See ServerStats for details.
     *
     * Generated from protobuf field <code>double time_elapsed = 2;</code>
     *
     * @return float
     */
    public function getTimeElapsed()
    {
        return $this->time_elapsed;
    }

    /**
     * See ServerStats for details.
     *
     * Generated from protobuf field <code>double time_elapsed = 2;</code>
     *
     * @param float $var
     *
     * @return $this
     */
    public function setTimeElapsed($var)
    {
        GPBUtil::checkDouble($var);
        $this->time_elapsed = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>double time_user = 3;</code>.
     *
     * @return float
     */
    public function getTimeUser()
    {
        return $this->time_user;
    }

    /**
     * Generated from protobuf field <code>double time_user = 3;</code>.
     *
     * @param float $var
     *
     * @return $this
     */
    public function setTimeUser($var)
    {
        GPBUtil::checkDouble($var);
        $this->time_user = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>double time_system = 4;</code>.
     *
     * @return float
     */
    public function getTimeSystem()
    {
        return $this->time_system;
    }

    /**
     * Generated from protobuf field <code>double time_system = 4;</code>.
     *
     * @param float $var
     *
     * @return $this
     */
    public function setTimeSystem($var)
    {
        GPBUtil::checkDouble($var);
        $this->time_system = $var;

        return $this;
    }

    /**
     * Number of failed requests (one row per status code seen).
     *
     * Generated from protobuf field <code>repeated .grpc.testing.RequestResultCount request_results = 5;</code>
     *
     * @return \Google\Protobuf\Internal\RepeatedField
     */
    public function getRequestResults()
    {
        return $this->request_results;
    }

    /**
     * Number of failed requests (one row per status code seen).
     *
     * Generated from protobuf field <code>repeated .grpc.testing.RequestResultCount request_results = 5;</code>
     *
     * @param \Grpc\Testing\RequestResultCount[]|\Google\Protobuf\Internal\RepeatedField $var
     *
     * @return $this
     */
    public function setRequestResults($var)
    {
        $arr = GPBUtil::checkRepeatedField($var, \Google\Protobuf\Internal\GPBType::MESSAGE, \Grpc\Testing\RequestResultCount::class);
        $this->request_results = $arr;

        return $this;
    }

    /**
     * Number of polls called inside completion queue.
     *
     * Generated from protobuf field <code>uint64 cq_poll_count = 6;</code>
     *
     * @return int|string
     */
    public function getCqPollCount()
    {
        return $this->cq_poll_count;
    }

    /**
     * Number of polls called inside completion queue.
     *
     * Generated from protobuf field <code>uint64 cq_poll_count = 6;</code>
     *
     * @param int|string $var
     *
     * @return $this
     */
    public function setCqPollCount($var)
    {
        GPBUtil::checkUint64($var);
        $this->cq_poll_count = $var;

        return $this;
    }

    /**
     * Core library stats.
     *
     * Generated from protobuf field <code>.grpc.core.Stats core_stats = 7;</code>
     *
     * @return \Grpc\Core\Stats
     */
    public function getCoreStats()
    {
        return $this->core_stats;
    }

    /**
     * Core library stats.
     *
     * Generated from protobuf field <code>.grpc.core.Stats core_stats = 7;</code>
     *
     * @param \Grpc\Core\Stats $var
     *
     * @return $this
     */
    public function setCoreStats($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Core\Stats::class);
        $this->core_stats = $var;

        return $this;
    }
}
