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

use Grpc\BaseStub;
use Grpc\Channel;
use Grpc\ChannelCredentials;
use Grpc\Interceptor;
use Grpc\Internal\InterceptorChannel;
use Grpc\Server;
use Grpc\Timeval;
use PHPUnit\Framework\TestCase;
use const Grpc\OP_RECV_CLOSE_ON_SERVER;
use const Grpc\OP_RECV_MESSAGE;
use const Grpc\OP_SEND_INITIAL_METADATA;
use const Grpc\OP_SEND_STATUS_FROM_SERVER;
use const Grpc\STATUS_OK;

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

class InterceptorClient extends BaseStub
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


class ChangeMetadataInterceptor extends Interceptor
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

class ChangeMetadataInterceptor2 extends Interceptor
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

class ChangeRequestInterceptor extends Interceptor
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

class StopCallInterceptor extends Interceptor
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

class InterceptorTest extends TestCase
{
    private $server;
    private $port;
    private $channel;

    public function setUp(): void
    {
        $this->server = new Server([]);
        $this->port = $this->server->addHttp2Port('0.0.0.0:0');
        $this->channel = new Channel('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => ChannelCredentials::createInsecure()]);
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
        $intercept_channel = Interceptor::intercept($this->channel, $channel_matadata_interceptor);
        $client = new InterceptorClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => ChannelCredentials::createInsecure(),
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
        $intercept_channel1 = Interceptor::intercept($this->channel, $channel_matadata_interceptor);
        $intercept_channel2 = Interceptor::intercept($intercept_channel1, $channel_matadata_intercepto2);
        $client = new InterceptorClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => ChannelCredentials::createInsecure(),
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
        $intercept_channel3 = Interceptor::intercept($this->channel,
            [$channel_matadata_intercepto2, $channel_matadata_interceptor]);
        $client = new InterceptorClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => ChannelCredentials::createInsecure(),
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
        $intercept_channel = Interceptor::intercept($this->channel,
            $change_request_interceptor);
        $client = new InterceptorClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => ChannelCredentials::createInsecure(),
        ], $intercept_channel);

        $req = new SimpleRequest($req_text);
        $unary_call = $client->UnaryCall($req);

        $event = $this->server->requestCall();
        $this->assertSame('/phony_method', $event->method);
        $server_call = $event->call;
        $event = $server_call->startBatch([
            OP_RECV_MESSAGE => true,
        ]);
        $this->assertSame('intercepted_unary_request', $event->message);
        $server_call->startBatch([
            OP_SEND_INITIAL_METADATA => [],
            OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => STATUS_OK,
                'details' => '',
            ],
            OP_RECV_CLOSE_ON_SERVER => true,
        ]);

        $stream_call = $client->StreamCall();
        $stream_call->write($req);
        $event = $this->server->requestCall();
        $this->assertSame('/phony_method', $event->method);
        $server_call = $event->call;
        $event = $server_call->startBatch([
            OP_RECV_MESSAGE => true,
        ]);
        $this->assertSame('intercepted_stream_request', $event->message);
        $server_call->startBatch([
            OP_SEND_INITIAL_METADATA => [],
            OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => STATUS_OK,
                'details' => '',
            ],
            OP_RECV_CLOSE_ON_SERVER => true,
        ]);

        unset($unary_call);
        unset($stream_call);
        unset($server_call);
    }

    public function testClientChangeStopCallInterceptor()
    {
        $req_text = 'client_request';
        $channel_request_interceptor = new StopCallInterceptor();
        $intercept_channel = Interceptor::intercept($this->channel,
            $channel_request_interceptor);
        $client = new InterceptorClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => ChannelCredentials::createInsecure(),
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
        $channel = new Channel(
            'localhost:0',
            [
                'force_new' => true,
                'credentials' => ChannelCredentials::createInsecure()
            ]
        );
        $interceptor_channel = Interceptor::intercept($channel, new Interceptor());
        $state = $interceptor_channel->getConnectivityState();
        $this->assertEquals(0, $state);
        $channel->close();
    }

    public function testInterceptorChannelWatchConnectivityState()
    {
        $channel = new Channel(
            'localhost:0',
            [
                'force_new' => true,
                'credentials' => ChannelCredentials::createInsecure()
            ]
        );
        $interceptor_channel = Interceptor::intercept($channel, new Interceptor());
        $now = Timeval::now();
        $deadline = $now->add(new Timeval(100*1000));
        $state = $interceptor_channel->watchConnectivityState(1, $deadline);
        $this->assertTrue($state);
        unset($time);
        unset($deadline);
        $channel->close();
    }

    public function testInterceptorChannelClose()
    {
        $channel = new Channel(
            'localhost:0',
            [
                'force_new' => true,
                'credentials' => ChannelCredentials::createInsecure()
            ]
        );
        $interceptor_channel = Interceptor::intercept($channel, new Interceptor());
        $this->assertNotNull($interceptor_channel);
        $channel->close();
    }

    public function testInterceptorChannelGetTarget()
    {
        $channel = new Channel(
            'localhost:8888',
            [
                'force_new' => true,
                'credentials' => ChannelCredentials::createInsecure()
            ]
        );
        $interceptor_channel = Interceptor::intercept($channel, new Interceptor());
        $target = $interceptor_channel->getTarget();
        $this->assertTrue(is_string($target));
        $channel->close();
    }
}
