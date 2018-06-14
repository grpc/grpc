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
class EndToEndTest extends PHPUnit_Framework_TestCase
{
    public function setUp()
    {
        $this->server = new Grpc\Server([]);
        $this->port = $this->server->addHttp2Port('0.0.0.0:0');
        $this->channel = new Grpc\Channel('localhost:'.$this->port, [
            "force_new" => true,
        ]);
        $this->server->start();
    }

    public function tearDown()
    {
        $this->channel->close();
        unset($this->server);
    }

    public function testSimpleRequestBody()
    {
        $deadline = Grpc\Timeval::infFuture();
        $status_text = 'xyz';
        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
                'details' => $status_text,
            ],
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_status);
        $this->assertFalse($event->cancelled);

        $event = $call->startBatch([
            Grpc\OP_RECV_INITIAL_METADATA => true,
            Grpc\OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        $status = $event->status;
        $this->assertSame([], $status->metadata);
        $this->assertSame(Grpc\STATUS_OK, $status->code);
        $this->assertSame($status_text, $status->details);

        unset($call);
        unset($server_call);
    }

    public function testMessageWriteFlags()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'message_write_flags_test';
        $status_text = 'xyz';
        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_MESSAGE => ['message' => $req_text,
                                     'flags' => Grpc\WRITE_NO_COMPRESS, ],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
                'details' => $status_text,
            ],
        ]);

        $event = $call->startBatch([
            Grpc\OP_RECV_INITIAL_METADATA => true,
            Grpc\OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        $status = $event->status;
        $this->assertSame([], $status->metadata);
        $this->assertSame(Grpc\STATUS_OK, $status->code);
        $this->assertSame($status_text, $status->details);

        unset($call);
        unset($server_call);
    }

    public function testClientServerFullRequestResponse()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);
        $this->assertTrue($event->send_message);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_MESSAGE => ['message' => $reply_text],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
                'details' => $status_text,
            ],
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_status);
        $this->assertTrue($event->send_message);
        $this->assertFalse($event->cancelled);
        $this->assertSame($req_text, $event->message);

        $event = $call->startBatch([
            Grpc\OP_RECV_INITIAL_METADATA => true,
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        $this->assertSame([], $event->metadata);
        $this->assertSame($reply_text, $event->message);
        $status = $event->status;
        $this->assertSame([], $status->metadata);
        $this->assertSame(Grpc\STATUS_OK, $status->code);
        $this->assertSame($status_text, $status->details);

        unset($call);
        unset($server_call);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidClientMessageArray()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => 'invalid',
        ]);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidClientMessageString()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => ['message' => 0],
        ]);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidClientMessageFlags()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => ['message' => 'abc',
                                     'flags' => 'invalid',
                                     ],
        ]);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidServerStatusMetadata()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);
        $this->assertTrue($event->send_message);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_MESSAGE => ['message' => $reply_text],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => 'invalid',
                'code' => Grpc\STATUS_OK,
                'details' => $status_text,
            ],
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidServerStatusCode()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);
        $this->assertTrue($event->send_message);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_MESSAGE => ['message' => $reply_text],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => 'invalid',
                'details' => $status_text,
            ],
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testMissingServerStatusCode()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);
        $this->assertTrue($event->send_message);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_MESSAGE => ['message' => $reply_text],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'details' => $status_text,
            ],
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidServerStatusDetails()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);
        $this->assertTrue($event->send_message);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_MESSAGE => ['message' => $reply_text],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
                'details' => 0,
            ],
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testMissingServerStatusDetails()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);
        $this->assertTrue($event->send_message);

        $event = $this->server->requestCall();
        $this->assertSame('dummy_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_MESSAGE => ['message' => $reply_text],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
            ],
            Grpc\OP_RECV_MESSAGE => true,
            Grpc\OP_RECV_CLOSE_ON_SERVER => true,
        ]);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testInvalidStartBatchKey()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            9999999 => [],
        ]);
    }

    /**
     * @expectedException LogicException
     */
    public function testInvalidStartBatch()
    {
        $deadline = Grpc\Timeval::infFuture();
        $req_text = 'client_server_full_request_response';
        $reply_text = 'reply:client_server_full_request_response';
        $status_text = 'status:client_server_full_response_text';

        $call = new Grpc\Call($this->channel,
                              'dummy_method',
                              $deadline);

        $event = $call->startBatch([
            Grpc\OP_SEND_INITIAL_METADATA => [],
            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
            Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => Grpc\STATUS_OK,
                'details' => 'abc',
            ],
        ]);
    }

    public function testGetTarget()
    {
        $this->assertTrue(is_string($this->channel->getTarget()));
    }

    public function testGetConnectivityState()
    {
        $this->assertTrue($this->channel->getConnectivityState() ==
                          Grpc\CHANNEL_IDLE);
    }

    public function testWatchConnectivityStateFailed()
    {
        $idle_state = $this->channel->getConnectivityState();
        $this->assertTrue($idle_state == Grpc\CHANNEL_IDLE);

        $now = Grpc\Timeval::now();
        $delta = new Grpc\Timeval(50000); // should timeout
        $deadline = $now->add($delta);

        $this->assertFalse($this->channel->watchConnectivityState(
        $idle_state, $deadline));
    }

    public function testWatchConnectivityStateSuccess()
    {
        $idle_state = $this->channel->getConnectivityState(true);
        $this->assertTrue($idle_state == Grpc\CHANNEL_IDLE);

        $now = Grpc\Timeval::now();
        $delta = new Grpc\Timeval(3000000); // should finish well before
        $deadline = $now->add($delta);

        $this->assertTrue($this->channel->watchConnectivityState(
        $idle_state, $deadline));

        $new_state = $this->channel->getConnectivityState();
        $this->assertTrue($idle_state != $new_state);
    }

    public function testWatchConnectivityStateDoNothing()
    {
        $idle_state = $this->channel->getConnectivityState();
        $this->assertTrue($idle_state == Grpc\CHANNEL_IDLE);

        $now = Grpc\Timeval::now();
        $delta = new Grpc\Timeval(50000);
        $deadline = $now->add($delta);

        $this->assertFalse($this->channel->watchConnectivityState(
        $idle_state, $deadline));

        $new_state = $this->channel->getConnectivityState();
        $this->assertTrue($new_state == Grpc\CHANNEL_IDLE);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testGetConnectivityStateInvalidParam()
    {
        $this->assertTrue($this->channel->getConnectivityState(
            new Grpc\Timeval()));
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testWatchConnectivityStateInvalidParam()
    {
        $this->assertTrue($this->channel->watchConnectivityState(
            0, 1000));
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testChannelConstructorInvalidParam()
    {
        $this->channel = new Grpc\Channel('localhost:'.$this->port, null);
    }

    public function testClose()
    {
        $this->assertNull($this->channel->close());
    }
}
