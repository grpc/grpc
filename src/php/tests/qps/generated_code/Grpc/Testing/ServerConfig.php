<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: test/proto/control.proto

namespace Grpc\Testing;

use Google\Protobuf\Internal\GPBType;
use Google\Protobuf\Internal\RepeatedField;
use Google\Protobuf\Internal\GPBUtil;

/**
 * Generated from protobuf message <code>grpc.testing.ServerConfig</code>
 */
class ServerConfig extends \Google\Protobuf\Internal\Message
{
    /**
     * Generated from protobuf field <code>.grpc.testing.ServerType server_type = 1;</code>
     */
    private $server_type = 0;
    /**
     * Generated from protobuf field <code>.grpc.testing.SecurityParams security_params = 2;</code>
     */
    private $security_params = null;
    /**
     * Port on which to listen. Zero means pick unused port.
     *
     * Generated from protobuf field <code>int32 port = 4;</code>
     */
    private $port = 0;
    /**
     * Only for async server. Number of threads used to serve the requests.
     *
     * Generated from protobuf field <code>int32 async_server_threads = 7;</code>
     */
    private $async_server_threads = 0;
    /**
     * Specify the number of cores to limit server to, if desired
     *
     * Generated from protobuf field <code>int32 core_limit = 8;</code>
     */
    private $core_limit = 0;
    /**
     * payload config, used in generic server.
     * Note this must NOT be used in proto (non-generic) servers. For proto servers,
     * 'response sizes' must be configured from the 'response_size' field of the
     * 'SimpleRequest' objects in RPC requests.
     *
     * Generated from protobuf field <code>.grpc.testing.PayloadConfig payload_config = 9;</code>
     */
    private $payload_config = null;
    /**
     * Specify the cores we should run the server on, if desired
     *
     * Generated from protobuf field <code>repeated int32 core_list = 10;</code>
     */
    private $core_list;
    /**
     * If we use an OTHER_SERVER client_type, this string gives more detail
     *
     * Generated from protobuf field <code>string other_server_api = 11;</code>
     */
    private $other_server_api = '';
    /**
     * Number of threads that share each completion queue
     *
     * Generated from protobuf field <code>int32 threads_per_cq = 12;</code>
     */
    private $threads_per_cq = 0;
    /**
     * Buffer pool size (no buffer pool specified if unset)
     *
     * Generated from protobuf field <code>int32 resource_quota_size = 1001;</code>
     */
    private $resource_quota_size = 0;
    /**
     * Generated from protobuf field <code>repeated .grpc.testing.ChannelArg channel_args = 1002;</code>
     */
    private $channel_args;

    public function __construct() {
        \GPBMetadata\Src\Proto\Grpc\Testing\Control::initOnce();
        parent::__construct();
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.ServerType server_type = 1;</code>
     * @return int
     */
    public function getServerType()
    {
        return $this->server_type;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.ServerType server_type = 1;</code>
     * @param int $var
     * @return $this
     */
    public function setServerType($var)
    {
        GPBUtil::checkEnum($var, \Grpc\Testing\ServerType::class);
        $this->server_type = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.SecurityParams security_params = 2;</code>
     * @return \Grpc\Testing\SecurityParams
     */
    public function getSecurityParams()
    {
        return $this->security_params;
    }

    /**
     * Generated from protobuf field <code>.grpc.testing.SecurityParams security_params = 2;</code>
     * @param \Grpc\Testing\SecurityParams $var
     * @return $this
     */
    public function setSecurityParams($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\SecurityParams::class);
        $this->security_params = $var;

        return $this;
    }

    /**
     * Port on which to listen. Zero means pick unused port.
     *
     * Generated from protobuf field <code>int32 port = 4;</code>
     * @return int
     */
    public function getPort()
    {
        return $this->port;
    }

    /**
     * Port on which to listen. Zero means pick unused port.
     *
     * Generated from protobuf field <code>int32 port = 4;</code>
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
     * Only for async server. Number of threads used to serve the requests.
     *
     * Generated from protobuf field <code>int32 async_server_threads = 7;</code>
     * @return int
     */
    public function getAsyncServerThreads()
    {
        return $this->async_server_threads;
    }

    /**
     * Only for async server. Number of threads used to serve the requests.
     *
     * Generated from protobuf field <code>int32 async_server_threads = 7;</code>
     * @param int $var
     * @return $this
     */
    public function setAsyncServerThreads($var)
    {
        GPBUtil::checkInt32($var);
        $this->async_server_threads = $var;

        return $this;
    }

    /**
     * Specify the number of cores to limit server to, if desired
     *
     * Generated from protobuf field <code>int32 core_limit = 8;</code>
     * @return int
     */
    public function getCoreLimit()
    {
        return $this->core_limit;
    }

    /**
     * Specify the number of cores to limit server to, if desired
     *
     * Generated from protobuf field <code>int32 core_limit = 8;</code>
     * @param int $var
     * @return $this
     */
    public function setCoreLimit($var)
    {
        GPBUtil::checkInt32($var);
        $this->core_limit = $var;

        return $this;
    }

    /**
     * payload config, used in generic server.
     * Note this must NOT be used in proto (non-generic) servers. For proto servers,
     * 'response sizes' must be configured from the 'response_size' field of the
     * 'SimpleRequest' objects in RPC requests.
     *
     * Generated from protobuf field <code>.grpc.testing.PayloadConfig payload_config = 9;</code>
     * @return \Grpc\Testing\PayloadConfig
     */
    public function getPayloadConfig()
    {
        return $this->payload_config;
    }

    /**
     * payload config, used in generic server.
     * Note this must NOT be used in proto (non-generic) servers. For proto servers,
     * 'response sizes' must be configured from the 'response_size' field of the
     * 'SimpleRequest' objects in RPC requests.
     *
     * Generated from protobuf field <code>.grpc.testing.PayloadConfig payload_config = 9;</code>
     * @param \Grpc\Testing\PayloadConfig $var
     * @return $this
     */
    public function setPayloadConfig($var)
    {
        GPBUtil::checkMessage($var, \Grpc\Testing\PayloadConfig::class);
        $this->payload_config = $var;

        return $this;
    }

    /**
     * Specify the cores we should run the server on, if desired
     *
     * Generated from protobuf field <code>repeated int32 core_list = 10;</code>
     * @return \Google\Protobuf\Internal\RepeatedField
     */
    public function getCoreList()
    {
        return $this->core_list;
    }

    /**
     * Specify the cores we should run the server on, if desired
     *
     * Generated from protobuf field <code>repeated int32 core_list = 10;</code>
     * @param int[]|\Google\Protobuf\Internal\RepeatedField $var
     * @return $this
     */
    public function setCoreList($var)
    {
        $arr = GPBUtil::checkRepeatedField($var, \Google\Protobuf\Internal\GPBType::INT32);
        $this->core_list = $arr;

        return $this;
    }

    /**
     * If we use an OTHER_SERVER client_type, this string gives more detail
     *
     * Generated from protobuf field <code>string other_server_api = 11;</code>
     * @return string
     */
    public function getOtherServerApi()
    {
        return $this->other_server_api;
    }

    /**
     * If we use an OTHER_SERVER client_type, this string gives more detail
     *
     * Generated from protobuf field <code>string other_server_api = 11;</code>
     * @param string $var
     * @return $this
     */
    public function setOtherServerApi($var)
    {
        GPBUtil::checkString($var, True);
        $this->other_server_api = $var;

        return $this;
    }

    /**
     * Number of threads that share each completion queue
     *
     * Generated from protobuf field <code>int32 threads_per_cq = 12;</code>
     * @return int
     */
    public function getThreadsPerCq()
    {
        return $this->threads_per_cq;
    }

    /**
     * Number of threads that share each completion queue
     *
     * Generated from protobuf field <code>int32 threads_per_cq = 12;</code>
     * @param int $var
     * @return $this
     */
    public function setThreadsPerCq($var)
    {
        GPBUtil::checkInt32($var);
        $this->threads_per_cq = $var;

        return $this;
    }

    /**
     * Buffer pool size (no buffer pool specified if unset)
     *
     * Generated from protobuf field <code>int32 resource_quota_size = 1001;</code>
     * @return int
     */
    public function getResourceQuotaSize()
    {
        return $this->resource_quota_size;
    }

    /**
     * Buffer pool size (no buffer pool specified if unset)
     *
     * Generated from protobuf field <code>int32 resource_quota_size = 1001;</code>
     * @param int $var
     * @return $this
     */
    public function setResourceQuotaSize($var)
    {
        GPBUtil::checkInt32($var);
        $this->resource_quota_size = $var;

        return $this;
    }

    /**
     * Generated from protobuf field <code>repeated .grpc.testing.ChannelArg channel_args = 1002;</code>
     * @return \Google\Protobuf\Internal\RepeatedField
     */
    public function getChannelArgs()
    {
        return $this->channel_args;
    }

    /**
     * Generated from protobuf field <code>repeated .grpc.testing.ChannelArg channel_args = 1002;</code>
     * @param \Grpc\Testing\ChannelArg[]|\Google\Protobuf\Internal\RepeatedField $var
     * @return $this
     */
    public function setChannelArgs($var)
    {
        $arr = GPBUtil::checkRepeatedField($var, \Google\Protobuf\Internal\GPBType::MESSAGE, \Grpc\Testing\ChannelArg::class);
        $this->channel_args = $arr;

        return $this;
    }

}

