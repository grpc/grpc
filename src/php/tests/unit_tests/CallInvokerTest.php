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
require_once(dirname(__FILE__).'/../../lib/Grpc/DefaultCallInvoker.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/Internal/InterceptorChannel.php');

class CallInvokerSimpleRequest
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

class CallInvokerClient extends Grpc\BaseStub
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
    CallInvokerSimpleRequest $argument,
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
}

class CallInvokerUpdateChannel implements \Grpc\CallInvoker
{
    private $channel;

    public function getChannel() {
        return $this->channel;
    }

    public function createChannelFactory($hostname, $opts) {
        $this->channel = new \Grpc\Channel('localhost:50050', $opts);
        return $this->channel;
    }

    public function UnaryCall($channel, $method, $deserialize, $options) {
        return new UnaryCall($channel, $method, $deserialize, $options);
    }

    public function ClientStreamingCall($channel, $method, $deserialize, $options) {
        return new ClientStreamingCall($channel, $method, $deserialize, $options);
    }

    public function ServerStreamingCall($channel, $method, $deserialize, $options) {
        return new ServerStreamingCall($channel, $method, $deserialize, $options);
    }

    public function BidiStreamingCall($channel, $method, $deserialize, $options) {
        return new BidiStreamingCall($channel, $method, $deserialize, $options);
    }
}


class CallInvokerChangeRequest implements \Grpc\CallInvoker
{
    private $channel;

    public function getChannel() {
        return $this->channel;
    }
    public function createChannelFactory($hostname, $opts) {
        $this->channel = new \Grpc\Channel($hostname, $opts);
        return $this->channel;
    }

    public function UnaryCall($channel, $method, $deserialize, $options) {
        return new CallInvokerChangeRequestCall($channel, $method, $deserialize, $options);
    }

    public function ClientStreamingCall($channel, $method, $deserialize, $options) {
        return new ClientStreamingCall($channel, $method, $deserialize, $options);
    }

    public function ServerStreamingCall($channel, $method, $deserialize, $options) {
        return new ServerStreamingCall($channel, $method, $deserialize, $options);
    }

    public function BidiStreamingCall($channel, $method, $deserialize, $options) {
        return new BidiStreamingCall($channel, $method, $deserialize, $options);
    }
}

class CallInvokerChangeRequestCall
{
    private $call;

    public function __construct($channel, $method, $deserialize, $options)
    {
        $this->call = new \Grpc\UnaryCall($channel, $method, $deserialize, $options);
    }

    public function start($argument, $metadata, $options) {
        $argument->setData('intercepted_unary_request');
        $this->call->start($argument, $metadata, $options);
    }

    public function wait()
    {
        return $this->call->wait();
    }
}

class CallInvokerTest extends \PHPUnit\Framework\TestCase
{
    public function setUp(): void
    {
        $this->server = new Grpc\Server([]);
        $this->port = $this->server->addHttp2Port('0.0.0.0:0');
        $this->server->start();
    }

    public function tearDown(): void
    {
        unset($this->server);
    }

    public function testCreateDefaultCallInvoker()
    {
        $call_invoker = new \Grpc\DefaultCallInvoker();
        $this->assertNotNull($call_invoker);
    }

    public function testCreateCallInvoker()
    {
        $call_invoker = new CallInvokerUpdateChannel();
        $this->assertNotNull($call_invoker);
    }

    public function testCallInvokerAccessChannel()
    {
        $call_invoker = new CallInvokerUpdateChannel();
        $stub = new \Grpc\BaseStub('localhost:50051',
          ['credentials' => \Grpc\ChannelCredentials::createInsecure(),
            'grpc_call_invoker' => $call_invoker]);
        $this->assertEquals($call_invoker->getChannel()->getTarget(), 'localhost:50050');
        $call_invoker->getChannel()->close();
    }

    public function testClientChangeRequestCallInvoker()
    {
        $req_text = 'client_request';
        $call_invoker = new CallInvokerChangeRequest();
        $client = new CallInvokerClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
            'grpc_call_invoker' => $call_invoker,
        ]);

        $req = new CallInvokerSimpleRequest($req_text);
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
        $call_invoker->getChannel()->close();
        unset($unary_call);
        unset($server_call);
    }
}
