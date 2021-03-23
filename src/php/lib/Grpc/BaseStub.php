<?php
/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

namespace Grpc;

/**
 * Base class for generated client stubs. Stub methods are expected to call
 * _simpleRequest or _streamRequest and return the result.
 */
class BaseStub
{
    private $hostname;
    private $hostname_override;
    private $channel;
    private $call_invoker;

    // a callback function
    private $update_metadata;

    /**
     * @param string  $hostname
     * @param array   $opts
     *  - 'update_metadata': (optional) a callback function which takes in a
     * metadata array, and returns an updated metadata array
     *  - 'grpc.primary_user_agent': (optional) a user-agent string
     * @param Channel|InterceptorChannel $channel An already created Channel or InterceptorChannel object (optional)
     */
    public function __construct($hostname, $opts, $channel = null)
    {
        if (!method_exists('ChannelCredentials', 'isDefaultRootsPemSet') ||
            !ChannelCredentials::isDefaultRootsPemSet()) {
            $ssl_roots = file_get_contents(
                dirname(__FILE__).'/../../../../etc/roots.pem'
            );
            ChannelCredentials::setDefaultRootsPem($ssl_roots);
        }

        $this->hostname = $hostname;
        $this->update_metadata = null;
        if (isset($opts['update_metadata'])) {
            if (is_callable($opts['update_metadata'])) {
                $this->update_metadata = $opts['update_metadata'];
            }
            unset($opts['update_metadata']);
        }
        if (!empty($opts['grpc.ssl_target_name_override'])) {
            $this->hostname_override = $opts['grpc.ssl_target_name_override'];
        }
        if (isset($opts['grpc_call_invoker'])) {
            $this->call_invoker = $opts['grpc_call_invoker'];
            unset($opts['grpc_call_invoker']);
            $channel_opts = $this->updateOpts($opts);
            // If the grpc_call_invoker is defined, use the channel created by the call invoker.
            $this->channel = $this->call_invoker->createChannelFactory($hostname, $channel_opts);
            return;
        }
        $this->call_invoker = new DefaultCallInvoker();
        if ($channel) {
            if (!is_a($channel, 'Grpc\Channel') &&
                !is_a($channel, 'Grpc\Internal\InterceptorChannel')) {
                throw new \Exception('The channel argument is not a Channel object '.
                    'or an InterceptorChannel object created by '.
                    'Interceptor::intercept($channel, Interceptor|Interceptor[] $interceptors)');
            }
            $this->channel = $channel;
            return;
        }

        $this->channel = static::getDefaultChannel($hostname, $opts);
    }

    private static function updateOpts($opts) {
        if (!file_exists($composerFile = __DIR__.'/../../composer.json')) {
            // for grpc/grpc-php subpackage
            $composerFile = __DIR__.'/../composer.json';
        }
        $package_config = json_decode(file_get_contents($composerFile), true);
        if (!empty($opts['grpc.primary_user_agent'])) {
            $opts['grpc.primary_user_agent'] .= ' ';
        } else {
            $opts['grpc.primary_user_agent'] = '';
        }
        $opts['grpc.primary_user_agent'] .=
            'grpc-php/'.$package_config['version'];
        if (!array_key_exists('credentials', $opts)) {
            throw new \Exception("The opts['credentials'] key is now ".
                'required. Please see one of the '.
                'ChannelCredentials::create methods');
        }
        return $opts;
    }

    /**
     * Creates and returns the default Channel
     *
     * @param array $opts Channel constructor options
     *
     * @return Channel The channel
     */
    public static function getDefaultChannel($hostname, array $opts)
    {
        $channel_opts = self::updateOpts($opts);
        return new Channel($hostname, $opts);
    }

    /**
     * @return string The URI of the endpoint
     */
    public function getTarget()
    {
        return $this->channel->getTarget();
    }

    /**
     * @param bool $try_to_connect (optional)
     *
     * @return int The grpc connectivity state
     */
    public function getConnectivityState($try_to_connect = false)
    {
        return $this->channel->getConnectivityState($try_to_connect);
    }

    /**
     * @param int $timeout in microseconds
     *
     * @return bool true if channel is ready
     * @throw Exception if channel is in FATAL_ERROR state
     */
    public function waitForReady($timeout)
    {
        $new_state = $this->getConnectivityState(true);
        if ($this->_checkConnectivityState($new_state)) {
            return true;
        }

        $now = Timeval::now();
        $delta = new Timeval($timeout);
        $deadline = $now->add($delta);

        while ($this->channel->watchConnectivityState($new_state, $deadline)) {
            // state has changed before deadline
            $new_state = $this->getConnectivityState();
            if ($this->_checkConnectivityState($new_state)) {
                return true;
            }
        }
        // deadline has passed
        $new_state = $this->getConnectivityState();

        return $this->_checkConnectivityState($new_state);
    }

    /**
     * Close the communication channel associated with this stub.
     */
    public function close()
    {
        $this->channel->close();
    }

    /**
     * @param $new_state Connect state
     *
     * @return bool true if state is CHANNEL_READY
     * @throw Exception if state is CHANNEL_FATAL_FAILURE
     */
    private function _checkConnectivityState($new_state)
    {
        if ($new_state == \Grpc\CHANNEL_READY) {
            return true;
        }
        if ($new_state == \Grpc\CHANNEL_FATAL_FAILURE) {
            throw new \Exception('Failed to connect to server');
        }

        return false;
    }

    /**
     * constructs the auth uri for the jwt.
     *
     * @param string $method The method string
     *
     * @return string The URL string
     */
    private function _get_jwt_aud_uri($method)
    {
        // TODO(jtattermusch): This is not the correct implementation
        // of extracting JWT "aud" claim. We should rely on
        // grpc_metadata_credentials_plugin which
        // also provides the correct value of "aud" claim
        // in the grpc_auth_metadata_context.service_url field.
        // Trying to do the construction of "aud" field ourselves
        // is bad.
        $last_slash_idx = strrpos($method, '/');
        if ($last_slash_idx === false) {
            throw new \InvalidArgumentException(
                'service name must have a slash'
            );
        }
        $service_name = substr($method, 0, $last_slash_idx);

        if ($this->hostname_override) {
            $hostname = $this->hostname_override;
        } else {
            $hostname = $this->hostname;
        }

        // Remove the port if it is 443
        // See https://github.com/grpc/grpc/blob/07c9f7a36b2a0d34fcffebc85649cf3b8c339b5d/src/core/lib/security/transport/client_auth_filter.cc#L205
        if ((strlen($hostname) > 4) && (substr($hostname, -4) === ":443")) {
            $hostname = substr($hostname, 0, -4);
        }

        return 'https://'.$hostname.$service_name;
    }

    /**
     * validate and normalize the metadata array.
     *
     * @param array $metadata The metadata map
     *
     * @return array $metadata Validated and key-normalized metadata map
     * @throw InvalidArgumentException if key contains invalid characters
     */
    private function _validate_and_normalize_metadata($metadata)
    {
        $metadata_copy = [];
        foreach ($metadata as $key => $value) {
            if (!preg_match('/^[.A-Za-z\d_-]+$/', $key)) {
                throw new \InvalidArgumentException(
                    'Metadata keys must be nonempty strings containing only '.
                    'alphanumeric characters, hyphens, underscores and dots'
                );
            }
            $metadata_copy[strtolower($key)] = $value;
        }

        return $metadata_copy;
    }

    /**
     * Create a function which can be used to create UnaryCall
     *
     * @param Channel|InterceptorChannel   $channel
     * @param callable $deserialize A function that deserializes the response
     *
     * @return \Closure
     */
    private function _GrpcUnaryUnary($channel)
    {
        return function ($method,
                         $argument,
                         $deserialize,
                         array $metadata = [],
                         array $options = []) use ($channel) {
            $call = $this->call_invoker->UnaryCall(
                $channel,
                $method,
                $deserialize,
                $options
            );
            $jwt_aud_uri = $this->_get_jwt_aud_uri($method);
            if (is_callable($this->update_metadata)) {
                $metadata = call_user_func(
                    $this->update_metadata,
                    $metadata,
                    $jwt_aud_uri
                );
            }
            $metadata = $this->_validate_and_normalize_metadata(
                $metadata
            );
            $call->start($argument, $metadata, $options);
            return $call;
        };
    }

    /**
     * Create a function which can be used to create ServerStreamingCall
     *
     * @param Channel|InterceptorChannel   $channel
     * @param callable $deserialize A function that deserializes the response
     *
     * @return \Closure
     */
    private function _GrpcStreamUnary($channel)
    {
        return function ($method,
                         $deserialize,
                         array $metadata = [],
                         array $options = []) use ($channel) {
            $call = $this->call_invoker->ClientStreamingCall(
                $channel,
                $method,
                $deserialize,
                $options
            );
            $jwt_aud_uri = $this->_get_jwt_aud_uri($method);
            if (is_callable($this->update_metadata)) {
                $metadata = call_user_func(
                    $this->update_metadata,
                    $metadata,
                    $jwt_aud_uri
                );
            }
            $metadata = $this->_validate_and_normalize_metadata(
                $metadata
            );
            $call->start($metadata);
            return $call;
        };
    }

    /**
     * Create a function which can be used to create ClientStreamingCall
     *
     * @param Channel|InterceptorChannel   $channel
     * @param callable $deserialize A function that deserializes the response
     *
     * @return \Closure
     */
    private function _GrpcUnaryStream($channel)
    {
        return function ($method,
                         $argument,
                         $deserialize,
                         array $metadata = [],
                         array $options = []) use ($channel) {
            $call = $this->call_invoker->ServerStreamingCall(
                $channel,
                $method,
                $deserialize,
                $options
            );
            $jwt_aud_uri = $this->_get_jwt_aud_uri($method);
            if (is_callable($this->update_metadata)) {
                $metadata = call_user_func(
                    $this->update_metadata,
                    $metadata,
                    $jwt_aud_uri
                );
            }
            $metadata = $this->_validate_and_normalize_metadata(
                $metadata
            );
            $call->start($argument, $metadata, $options);
            return $call;
        };
    }

    /**
     * Create a function which can be used to create BidiStreamingCall
     *
     * @param Channel|InterceptorChannel   $channel
     * @param callable $deserialize A function that deserializes the response
     *
     * @return \Closure
     */
    private function _GrpcStreamStream($channel)
    {
        return function ($method,
                         $deserialize,
                         array $metadata = [],
                         array $options = []) use ($channel) {
            $call = $this->call_invoker->BidiStreamingCall(
                $channel,
                $method,
                $deserialize,
                $options
            );
            $jwt_aud_uri = $this->_get_jwt_aud_uri($method);
            if (is_callable($this->update_metadata)) {
                $metadata = call_user_func(
                    $this->update_metadata,
                    $metadata,
                    $jwt_aud_uri
                );
            }
            $metadata = $this->_validate_and_normalize_metadata(
                $metadata
            );
            $call->start($metadata);

            return $call;
        };
    }

    /**
     * Create a function which can be used to create UnaryCall
     *
     * @param Channel|InterceptorChannel   $channel
     * @param callable $deserialize A function that deserializes the response
     *
     * @return \Closure
     */
    private function _UnaryUnaryCallFactory($channel)
    {
        if (is_a($channel, 'Grpc\Internal\InterceptorChannel')) {
            return function ($method,
                             $argument,
                             $deserialize,
                             array $metadata = [],
                             array $options = []) use ($channel) {
                return $channel->getInterceptor()->interceptUnaryUnary(
                    $method,
                    $argument,
                    $deserialize,
                    $this->_UnaryUnaryCallFactory($channel->getNext()),
                    $metadata,
                    $options
                );
            };
        }
        return $this->_GrpcUnaryUnary($channel);
    }

    /**
     * Create a function which can be used to create ServerStreamingCall
     *
     * @param Channel|InterceptorChannel   $channel
     * @param callable $deserialize A function that deserializes the response
     *
     * @return \Closure
     */
    private function _UnaryStreamCallFactory($channel)
    {
        if (is_a($channel, 'Grpc\Internal\InterceptorChannel')) {
            return function ($method,
                             $argument,
                             $deserialize,
                             array $metadata = [],
                             array $options = []) use ($channel) {
                return $channel->getInterceptor()->interceptUnaryStream(
                    $method,
                    $argument,
                    $deserialize,
                    $this->_UnaryStreamCallFactory($channel->getNext()),
                    $metadata,
                    $options
                );
            };
        }
        return $this->_GrpcUnaryStream($channel);
    }

    /**
     * Create a function which can be used to create ClientStreamingCall
     *
     * @param Channel|InterceptorChannel   $channel
     * @param callable $deserialize A function that deserializes the response
     *
     * @return \Closure
     */
    private function _StreamUnaryCallFactory($channel)
    {
        if (is_a($channel, 'Grpc\Internal\InterceptorChannel')) {
            return function ($method,
                             $deserialize,
                             array $metadata = [],
                             array $options = []) use ($channel) {
                return $channel->getInterceptor()->interceptStreamUnary(
                    $method,
                    $deserialize,
                    $this->_StreamUnaryCallFactory($channel->getNext()),
                    $metadata,
                    $options
                );
            };
        }
        return $this->_GrpcStreamUnary($channel);
    }

    /**
     * Create a function which can be used to create BidiStreamingCall
     *
     * @param Channel|InterceptorChannel   $channel
     * @param callable $deserialize A function that deserializes the response
     *
     * @return \Closure
     */
    private function _StreamStreamCallFactory($channel)
    {
        if (is_a($channel, 'Grpc\Internal\InterceptorChannel')) {
            return function ($method,
                             $deserialize,
                             array $metadata = [],
                             array $options = []) use ($channel) {
                return $channel->getInterceptor()->interceptStreamStream(
                    $method,
                    $deserialize,
                    $this->_StreamStreamCallFactory($channel->getNext()),
                    $metadata,
                    $options
                );
            };
        }
        return $this->_GrpcStreamStream($channel);
    }

    /* This class is intended to be subclassed by generated code, so
     * all functions begin with "_" to avoid name collisions. */
    /**
     * Call a remote method that takes a single argument and has a
     * single output.
     *
     * @param string   $method      The name of the method to call
     * @param mixed    $argument    The argument to the method
     * @param callable $deserialize A function that deserializes the response
     * @param array    $metadata    A metadata map to send to the server
     *                              (optional)
     * @param array    $options     An array of options (optional)
     *
     * @return UnaryCall The active call object
     */
    protected function _simpleRequest(
        $method,
        $argument,
        $deserialize,
        array $metadata = [],
        array $options = []
    ) {
        $call_factory = $this->_UnaryUnaryCallFactory($this->channel);
        $call = $call_factory($method, $argument, $deserialize, $metadata, $options);
        return $call;
    }

    /**
     * Call a remote method that takes a stream of arguments and has a single
     * output.
     *
     * @param string   $method      The name of the method to call
     * @param callable $deserialize A function that deserializes the response
     * @param array    $metadata    A metadata map to send to the server
     *                              (optional)
     * @param array    $options     An array of options (optional)
     *
     * @return ClientStreamingCall The active call object
     */
    protected function _clientStreamRequest(
        $method,
        $deserialize,
        array $metadata = [],
        array $options = []
    ) {
        $call_factory = $this->_StreamUnaryCallFactory($this->channel);
        $call = $call_factory($method, $deserialize, $metadata, $options);
        return $call;
    }

    /**
     * Call a remote method that takes a single argument and returns a stream
     * of responses.
     *
     * @param string   $method      The name of the method to call
     * @param mixed    $argument    The argument to the method
     * @param callable $deserialize A function that deserializes the responses
     * @param array    $metadata    A metadata map to send to the server
     *                              (optional)
     * @param array    $options     An array of options (optional)
     *
     * @return ServerStreamingCall The active call object
     */
    protected function _serverStreamRequest(
        $method,
        $argument,
        $deserialize,
        array $metadata = [],
        array $options = []
    ) {
        $call_factory = $this->_UnaryStreamCallFactory($this->channel);
        $call = $call_factory($method, $argument, $deserialize, $metadata, $options);
        return $call;
    }

    /**
     * Call a remote method with messages streaming in both directions.
     *
     * @param string   $method      The name of the method to call
     * @param callable $deserialize A function that deserializes the responses
     * @param array    $metadata    A metadata map to send to the server
     *                              (optional)
     * @param array    $options     An array of options (optional)
     *
     * @return BidiStreamingCall The active call object
     */
    protected function _bidiRequest(
        $method,
        $deserialize,
        array $metadata = [],
        array $options = []
    ) {
        $call_factory = $this->_StreamStreamCallFactory($this->channel);
        $call = $call_factory($method, $deserialize, $metadata, $options);
        return $call;
    }
}
