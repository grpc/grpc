<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/stats.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * <pre>
 * Histogram params based on grpc/support/histogram.c
 * </pre>
 *
 * Protobuf type <code>grpc.testing.HistogramParams</code>
 */
class HistogramParams extends \Google\Protobuf\Internal\Message
{
    /**
     * <pre>
     * first bucket is [0, 1 + resolution)
     * </pre>
     *
     * <code>double resolution = 1;</code>
     */
    private $resolution = 0.0;
    /**
     * <pre>
     * use enough buckets to allow this value
     * </pre>
     *
     * <code>double max_possible = 2;</code>
     */
    private $max_possible = 0.0;

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\Stats::initOnce();
        parent::__construct();
    }

    /**
     * <pre>
     * first bucket is [0, 1 + resolution)
     * </pre>
     *
     * <code>double resolution = 1;</code>
     */
    public function getResolution()
    {
        return $this->resolution;
    }

    /**
     * <pre>
     * first bucket is [0, 1 + resolution)
     * </pre>
     *
     * <code>double resolution = 1;</code>
     */
    public function setResolution($var)
    {
        GPBUtil::checkDouble($var);
        $this->resolution = $var;
    }

    /**
     * <pre>
     * use enough buckets to allow this value
     * </pre>
     *
     * <code>double max_possible = 2;</code>
     */
    public function getMaxPossible()
    {
        return $this->max_possible;
    }

    /**
     * <pre>
     * use enough buckets to allow this value
     * </pre>
     *
     * <code>double max_possible = 2;</code>
     */
    public function setMaxPossible($var)
    {
        GPBUtil::checkDouble($var);
        $this->max_possible = $var;
    }

}

