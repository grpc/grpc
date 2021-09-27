<?php
/*
 *
 * Copyright 2018 gRPC authors.
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
/**
 * Interface exported by the server.
 */
require_once(dirname(__FILE__).'/../../lib/Grpc/BaseStub.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/AbstractCall.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/UnaryCall.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/ClientStreamingCall.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/Interceptor.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/CallInvoker.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/Internal/InterceptorChannel.php');

class SimpleRequest
{
    private $data;
    public function __construct($data)
    {
        $this->data = $data;
    }
    public function setData($data)
    {
        $this->data = $data;
    }
    public function serializeToString()
    {
        return $this->data;
    }
}

class InterceptorClient extends Grpc\BaseStub
{

    /**
     * @param string $hostname hostname
     * @param array $opts channel options
     * @param Channel|InterceptorChannel $channel (optional) re-use channel object
     */
    public function __construct($hostname, $opts, $channel = null)
    {
        parent::__construct($hostname, $opts, $channel);
    }

    /**
     * A simple RPC.
     * @param SimpleRequest $argument input argument
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function UnaryCall(
        SimpleRequest $argument,
        $metadata = [],
        $options = []
    ) {
        return $this->_simpleRequest(
            '/phony_method',
            $argument,
            [],
            $metadata,
            $options
        );
    }

    /**
     * A client-to-server streaming RPC.
     * @param array $metadata metadata
     * @param array $options call options
     */
    public function StreamCall(
        $metadata = [],
        $options = []
    ) {
        return $this->_clientStreamRequest('/phony_method', [], $metadata, $options);
    }
}


class ChangeMetadataInterceptor extends Grpc\Interceptor
{
    public function interceptUnaryUnary($method,
                                        $argument,
                                        $deserialize,
                                        $continuation,
                                        array $metadata = [],
                                        array $options = [])
    {
        $metadata["foo"] = array('interceptor_from_unary_request');
        return $continuation($method, $argument, $deserialize, $metadata, $options);
    }
    public function interceptStreamUnary($method,
                                         $deserialize,
                                         $continuation,
                                         array $metadata = [],
                                         array $options = [])
    {
        $metadata["foo"] = array('interceptor_from_stream_request');
        return $continuation($method, $deserialize, $metadata, $options);
    }
}

class ChangeMetadataInterceptor2 extends Grpc\Interceptor
{
    public function interceptUnaryUnary($method,
                                        $argument,
                                        $deserialize,
                                        $continuation,
                                        array $metadata = [],
                                        array $options = [])
    {
        if (array_key_exists('foo', $metadata)) {
            $metadata['bar'] = array('ChangeMetadataInterceptor should be executed first');
        } else {
            $metadata["bar"] = array('interceptor_from_unary_request');
        }
        return $continuation($method, $argument, $deserialize, $metadata, $options);
    }
    public function interceptStreamUnary($method,
                                         $deserialize,
                                         $continuation,
                                         array $metadata = [],
                                         array $options = [])
    {
        if (array_key_exists('foo', $metadata)) {
            $metadata['bar'] = array('ChangeMetadataInterceptor should be executed first');
        } else {
            $metadata["bar"] = array('interceptor_from_stream_request');
        }
        return $continuation($method, $deserialize, $metadata, $options);
    }
}

class ChangeRequestCall
{
    private $call;

    public function __construct($call)
    {
        $this->call = $call;
    }
    public function getCall()
    {
        return $this->call;
    }

    public function write($request)
    {
        $request->setData('intercepted_stream_request');
        $this->getCall()->write($request);
    }

    public function wait()
    {
        return $this->getCall()->wait();
    }
}

class ChangeRequestInterceptor extends Grpc\Interceptor
{
    public function interceptUnaryUnary($method,
                                        $argument,
                                        $deserialize,
                                        $continuation,
                                        array $metadata = [],
                                        array $options = [])
    {
        $argument->setData('intercepted_unary_request');
        return $continuation($method, $argument, $deserialize, $metadata, $options);
    }
    public function interceptStreamUnary($method,
                                         $deserialize,
                                         $continuation,
                                         array $metadata = [],
                                         array $options = [])
    {
        return new ChangeRequestCall(
            $continuation($method, $deserialize, $metadata, $options)
        );
    }
}

class StopCallInterceptor extends Grpc\Interceptor
{
    public function interceptUnaryUnary($method,
                                        $argument,
                                        $deserialize,
                                        $continuation,
                                        array $metadata = [],
                                        array $options = [])
    {
        $metadata["foo"] = array('interceptor_from_request_response');
    }
    public function interceptStreamUnary($method,
                                         $deserialize,
                                         $continuation,
                                         array $metadata = [],
                                         array $options = [])
    {
        $metadata["foo"] = array('interceptor_from_request_response');
    }
}

class InterceptorTest extends \PHPUnit\Framework\TestCase
{
    public function setUp(): void
    {
        $this->server = new Grpc\Server([]);
        $this->port = $this->server->addHttp2Port('0.0.0.0:0');
        $this->channel = new Grpc\Channel('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure()]);
        $this->server->start();
    }

    public function tearDown(): void
    {
        $this->channel->close();
        unset($this->server);
    }


    public function testClientChangeMetadataOneInterceptor()
    {
        $req_text = 'client_request';
        $channel_matadata_interceptor = new ChangeMetadataInterceptor();
        $intercept_channel = Grpc\Interceptor::intercept($this->channel, $channel_matadata_interceptor);
        $client = new InterceptorClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ], $intercept_channel);
        $req = new SimpleRequest($req_text);
        $unary_call = $client->UnaryCall($req);
        $event = $this->server->requestCall();
        $this->assertSame('/phony_method', $event->method);
        $this->assertSame(['interceptor_from_unary_request'], $event->metadata['foo']);

        $stream_call = $client->StreamCall();
        $stream_call->write($req);
        $event = $this->server->requestCall();
        $this->assertSame('/phony_method', $event->method);
        $this->assertSame(['interceptor_from_stream_request'], $event->metadata['foo']);

        unset($unary_call);
        unset($stream_call);
        unset($server_call);
    }

    public function testClientChangeMetadataTwoInterceptor()
    {
        $req_text = 'client_request';
        $channel_matadata_interceptor = new ChangeMetadataInterceptor();
        $channel_matadata_intercepto2 = new ChangeMetadataInterceptor2();
        // test intercept separately.
        $intercept_channel1 = Grpc\Interceptor::intercept($this->channel, $channel_matadata_interceptor);
        $intercept_channel2 = Grpc\Interceptor::intercept($intercept_channel1, $channel_matadata_intercepto2);
        $client = new InterceptorClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ], $intercept_channel2);

        $req = new SimpleRequest($req_text);
        $unary_call = $client->UnaryCall($req);
        $event = $this->server->requestCall();
        $this->assertSame('/phony_method', $event->method);
        $this->assertSame(['interceptor_from_unary_request'], $event->metadata['foo']);
        $this->assertSame(['interceptor_from_unary_request'], $event->metadata['bar']);

        $stream_call = $client->StreamCall();
        $stream_call->write($req);
        $event = $this->server->requestCall();
        $this->assertSame('/phony_method', $event->method);
        $this->assertSame(['interceptor_from_stream_request'], $event->metadata['foo']);
        $this->assertSame(['interceptor_from_stream_request'], $event->metadata['bar']);

        unset($unary_call);
        unset($stream_call);
        unset($server_call);

        // test intercept by array.
        $intercept_channel3 = Grpc\Interceptor::intercept($this->channel,
            [$channel_matadata_intercepto2, $channel_matadata_interceptor]);
        $client = new InterceptorClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ], $intercept_channel3);

        $req = new SimpleRequest($req_text);
        $unary_call = $client->UnaryCall($req);
        $event = $this->server->requestCall();
        $this->assertSame('/phony_method', $event->method);
        $this->assertSame(['interceptor_from_unary_request'], $event->metadata['foo']);
        $this->assertSame(['interceptor_from_unary_request'], $event->metadata['bar']);

        $stream_call = $client->StreamCall();
        $stream_call->write($req);
        $event = $this->server->requestCall();
        $this->assertSame('/phony_method', $event->method);
        $this->assertSame(['interceptor_from_stream_request'], $event->metadata['foo']);
        $this->assertSame(['interceptor_from_stream_request'], $event->metadata['bar']);

        unset($unary_call);
        unset($stream_call);
        unset($server_call);
    }

    public function testClientChangeRequestInterceptor()
    {
        $req_text = 'client_request';
        $change_request_interceptor = new ChangeRequestInterceptor();
        $intercept_channel = Grpc\Interceptor::intercept($this->channel,
            $change_request_interceptor);
        $client = new InterceptorClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ], $intercept_channel);

        $req = new SimpleRequest($req_text);
        $unary_call = $client->UnaryCall($req);

        $event = $this->server->requestCall();
        $this->assertSame('/phony_method', $event->method);
        $server_call = $event->call;
        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
                'details' => '',
            ],
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);
        $this->assertSame('intercepted_unary_request', $event->message);

        $stream_call = $client->StreamCall();
        $stream_call->write($req);
        $event = $this->server->requestCall();
        $this->assertSame('/phony_method', $event->method);
        $server_call = $event->call;
        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
                'details' => '',
            ],
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);
        $this->assertSame('intercepted_stream_request', $event->message);

        unset($unary_call);
        unset($stream_call);
        unset($server_call);
    }

    public function testClientChangeStopCallInterceptor()
    {
        $req_text = 'client_request';
        $channel_request_interceptor = new StopCallInterceptor();
        $intercept_channel = Grpc\Interceptor::intercept($this->channel,
            $channel_request_interceptor);
        $client = new InterceptorClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ], $intercept_channel);

        $req = new SimpleRequest($req_text);
        $unary_call = $client->UnaryCall($req);
        $this->assertNull($unary_call);


        $stream_call = $client->StreamCall();
        $this->assertNull($stream_call);

        unset($unary_call);
        unset($stream_call);
        unset($server_call);
    }

    public function testGetInterceptorChannelConnectivityState()
    {
        $channel = new Grpc\Channel(
            'localhost:0',
            [
                'force_new' => true,
                'credentials' => Grpc\ChannelCredentials::createInsecure()
            ]
        );
        $interceptor_channel = Grpc\Interceptor::intercept($channel, new Grpc\Interceptor());
        $state = $interceptor_channel->getConnectivityState();
        $this->assertEquals(0, $state);
        $channel->close();
    }

    public function testInterceptorChannelWatchConnectivityState()
    {
        $channel = new Grpc\Channel(
            'localhost:0',
            [
                'force_new' => true,
                'credentials' => Grpc\ChannelCredentials::createInsecure()
            ]
        );
        $interceptor_channel = Grpc\Interceptor::intercept($channel, new Grpc\Interceptor());
        $now = Grpc\Timeval::now();
        $deadline = $now->add(new Grpc\Timeval(100*1000));
        $state = $interceptor_channel->watchConnectivityState(1, $deadline);
        $this->assertTrue($state);
        unset($time);
        unset($deadline);
        $channel->close();
    }

    public function testInterceptorChannelClose()
    {
        $channel = new Grpc\Channel(
            'localhost:0',
            [
                'force_new' => true,
                'credentials' => Grpc\ChannelCredentials::createInsecure()
            ]
        );
        $interceptor_channel = Grpc\Interceptor::intercept($channel, new Grpc\Interceptor());
        $this->assertNotNull($interceptor_channel);
        $channel->close();
    }

    public function testInterceptorChannelGetTarget()
    {
        $channel = new Grpc\Channel(
            'localhost:8888',
            [
                'force_new' => true,
                'credentials' => Grpc\ChannelCredentials::createInsecure()
            ]
        );
        $interceptor_channel = Grpc\Interceptor::intercept($channel, new Grpc\Interceptor());
        $target = $interceptor_channel->getTarget();
        $this->assertTrue(is_string($target));
        $channel->close();
    }
}
