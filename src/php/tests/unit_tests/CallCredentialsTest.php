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

use Grpc\Call;
use Grpc\CallCredentials;
use Grpc\Channel;
use Grpc\ChannelCredentials;
use Grpc\Server;
use Grpc\ServerCredentials;
use Grpc\Timeval;
use PHPUnit\Framework\TestCase;
use const Grpc\OP_RECV_CLOSE_ON_SERVER;
use const Grpc\OP_RECV_INITIAL_METADATA;
use const Grpc\OP_RECV_STATUS_ON_CLIENT;
use const Grpc\OP_SEND_CLOSE_FROM_CLIENT;
use const Grpc\OP_SEND_INITIAL_METADATA;
use const Grpc\OP_SEND_STATUS_FROM_SERVER;
use const Grpc\STATUS_INVALID_ARGUMENT;

class CallCredentialsTest extends TestCase
{
    /**
     * @var ChannelCredentials
     */
    private $credentials;
    /**
     * @var CallCredentials
     */
    private $call_credentials;
    /**
     * @var Server
     */
    private $server;
    /**
     * @var bool
     */
    private $port;
    /**
     * @var string
     */
    private $host_override;
    /**
     * @var Channel
     */
    private $channel;

    public function setUp(): void
    {
        $this->credentials = ChannelCredentials::createSsl(
            file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
        $this->call_credentials = CallCredentials::createFromPlugin(
            [$this, 'callbackFunc']);
        $this->credentials = ChannelCredentials::createComposite(
            $this->credentials,
            $this->call_credentials
        );
        $server_credentials = ServerCredentials::createSsl(
            null,
            file_get_contents(dirname(__FILE__).'/../data/server1.key'),
            file_get_contents(dirname(__FILE__).'/../data/server1.pem'));
        $this->server = new Server();
        $this->port = $this->server->addSecureHttp2Port('0.0.0.0:0',
                                              $server_credentials);
        $this->server->start();
        $this->host_override = 'foo.test.google.fr';
        $this->channel = new Channel(
            'localhost:'.$this->port,
            [
            'force_new' => true,
            'grpc.ssl_target_name_override' => $this->host_override,
            'grpc.default_authority' => $this->host_override,
            'credentials' => $this->credentials,
            ]
        );
    }

    public function tearDown(): void
    {
        unset($this->channel);
        unset($this->server);
    }

    public function callbackFunc($context)
    {
        $this->assertTrue(is_string($context->service_url));
        $this->assertTrue(is_string($context->method_name));

        return ['k1' => ['v1'], 'k2' => ['v2']];
    }

    public function testCreateFromPlugin()
    {
        $deadline = Timeval::infFuture();
        $status_text = 'xyz';
        $call = new Call($this->channel,
                              '/abc/phony_method',
                              $deadline,
                              $this->host_override);

        $event = $call->startBatch([
            OP_SEND_INITIAL_METADATA => [],
            OP_SEND_CLOSE_FROM_CLIENT => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_close);

        $event = $this->server->requestCall();

        $this->assertTrue(is_array($event->metadata));
        $metadata = $event->metadata;
        $this->assertTrue(array_key_exists('k1', $metadata));
        $this->assertTrue(array_key_exists('k2', $metadata));
        $this->assertSame($metadata['k1'], ['v1']);
        $this->assertSame($metadata['k2'], ['v2']);

        $this->assertSame('/abc/phony_method', $event->method);
        $server_call = $event->call;

        $event = $server_call->startBatch([
            OP_SEND_INITIAL_METADATA => [],
            OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code' => STATUS_INVALID_ARGUMENT,
                'details' => $status_text,
            ],
            OP_RECV_CLOSE_ON_SERVER => true,
        ]);

        $this->assertTrue($event->send_metadata);
        $this->assertTrue($event->send_status);
        $this->assertFalse($event->cancelled);

        $event = $call->startBatch([
            OP_RECV_INITIAL_METADATA => true,
            OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        $this->assertSame([], $event->metadata);
        $status = $event->status;
        $this->assertSame([], $status->metadata);
        $this->assertSame(STATUS_INVALID_ARGUMENT, $status->code);
        $this->assertSame($status_text, $status->details);

        unset($call);
        unset($server_call);
    }

    public function callbackFunc2($context)
    {
        return [];
    }

    public function testCreateComposite()
    {
        $call_credentials2 = CallCredentials::createFromPlugin(
            [$this, 'callbackFunc2']);
        $call_credentials3 = CallCredentials::createComposite(
            $this->call_credentials,
            $call_credentials2
        );
        $this->assertSame(CallCredentials::class,
                          get_class($call_credentials3));
    }

    public function testCreateFromPluginInvalidParam()
    {
        $this->expectException(InvalidArgumentException::class);
        $call_credentials = CallCredentials::createFromPlugin(
            'callbackFunc'
        );
    }

    public function testCreateCompositeInvalidParam()
    {
        $this->expectException(InvalidArgumentException::class);
        $call_credentials3 = CallCredentials::createComposite(
            $this->call_credentials,
            $this->credentials
        );
    }
}
