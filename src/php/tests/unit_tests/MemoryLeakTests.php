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

// ====================== END OF ChannelTest ==================
function callbackFunc($context)
{
  return [];
}
function callbackFunc2($context)
{
  return ["k1" => "v1"];
}
function waitUntilNotIdle($channel) {
  for ($i = 0; $i < 10; $i++) {
    $now = Grpc\Timeval::now();
    $deadline = $now->add(new Grpc\Timeval(1000));
    if ($channel->watchConnectivityState(GRPC\CHANNEL_IDLE,
      $deadline)) {
      return true;
    }
  }
}
//$channel = new Grpc\Channel('localhost:0', 'invalid');

// setUp()
$channel = new Grpc\Channel('localhost:0',
      ['credentials' => Grpc\ChannelCredentials::createInsecure()]);

// testGetConnectivityState
$state = $channel->getConnectivityState();
// testGetConnectivityStateWithInt()
$state = $channel->getConnectivityState(123);
// testGetConnectivityStateWithString()
$state = $channel->getConnectivityState('hello');
// testGetConnectivityStateWithBool()
$state = $channel->getConnectivityState(true);
$channel->close();
// testGetTarget()
$channel = new Grpc\Channel('localhost:8888',
      ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
$target = $channel->getTarget();
$channel->close();
// testWatchConnectivityState()
$channel = new Grpc\Channel('localhost:0',
      ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
$now = Grpc\Timeval::now();
$deadline = $now->add(new Grpc\Timeval(100*1000));  // 100ms
$state = $channel->watchConnectivityState(1, $deadline);
unset($now);
unset($deadline);
// testClose()
$channel->close();
// testInvalidConstructorWithNull()
try {
  $channel = new Grpc\Channel();
}
catch (\Exception $e) {
}
// testInvalidConstructorWith()
try {
  $channel = new Grpc\Channel('localhost:0', 'invalid');
}
catch (\Exception $e) {
}
// testInvalidCredentials()
try {
  $channel = new Grpc\Channel('localhost:0',
      ['credentials' => new Grpc\Timeval(100)]);
}
catch (\Exception $e) {
}
// testInvalidOptionsArray()
try {
  $channel = new Grpc\Channel('localhost:0',
      ['abc' => []]);
}
catch (\Exception $e) {
}
// testInvalidGetConnectivityStateWithArray()
$channel = new Grpc\Channel('localhost:0',
      ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
try {
  $channel->getConnectivityState([]);
} catch (\Exception $e) {
}
// testInvalidWatchConnectivityState()
try {
  $channel->watchConnectivityState([]);
} catch (\Exception $e) {
}
// testInvalidWatchConnectivityState2()
try {
  $channel->watchConnectivityState(1, 'hi');
} catch (\Exception $e) {
}
$channel->close();
// testPersistentChannelSameHost()
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:1', []);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$channel1->getConnectivityState();
$channel2->getConnectivityState();
$channel1->close();
$channel2->close();
// testPersistentChannelDifferentHost()
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:2', []);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$channel1->close();
$channel2->close();
// testPersistentChannelSameArgs()
$channel1 = new Grpc\Channel('localhost:1', ["abc" => "def"]);
$channel2 = new Grpc\Channel('localhost:1', ["abc" => "def"]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$channel1->close();
$channel2->close();
// testPersistentChannelDifferentArgs()
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:1', ["abc" => "def"]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$channel1->close();
$channel2->close();
// testPersistentChannelSameChannelCredentials()
$creds1 = Grpc\ChannelCredentials::createSsl();
$creds2 = Grpc\ChannelCredentials::createSsl();
$channel1 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds1]);
$channel2 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds2]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$channel1->close();
$channel2->close();
// testPersistentChannelDifferentChannelCredentials()
$creds1 = Grpc\ChannelCredentials::createSsl();
$creds2 = Grpc\ChannelCredentials::createSsl(
      file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
$channel1 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds1]);
$channel2 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds2]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$channel1->close();
$channel2->close();
// testPersistentChannelSameChannelCredentialsRootCerts()
$creds1 = Grpc\ChannelCredentials::createSsl(
      file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
$creds2 = Grpc\ChannelCredentials::createSsl(
      file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
$channel1 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds1]);
$channel2 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds2]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$channel1->close();
$channel2->close();
// testPersistentChannelDifferentSecureChannelCredentials()
$creds1 = Grpc\ChannelCredentials::createSsl();
$creds2 = Grpc\ChannelCredentials::createInsecure();
$channel1 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds1]);
$channel2 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds2]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$channel1->close();
$channel2->close();
// testPersistentChannelSharedChannelClose()
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:1', []);
$channel1->close();
try {
    $state = $channel2->getConnectivityState();
}
catch (\Exception $e) {
}
// testPersistentChannelCreateAfterClose()
$channel1 = new Grpc\Channel('localhost:1', []);
$channel1->close();
$channel2 = new Grpc\Channel('localhost:1', []);
$state = $channel2->getConnectivityState();
$channel2->close();
// testPersistentChannelSharedMoreThanTwo()
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:1', []);
$channel3 = new Grpc\Channel('localhost:1', []);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$state = $channel3->getConnectivityState();
$channel1->close();
// testPersistentChannelWithCallCredentials()
$creds = Grpc\ChannelCredentials::createSsl();
$callCreds = Grpc\CallCredentials::createFromPlugin(
    'callbackFunc');
$credsWithCallCreds = Grpc\ChannelCredentials::createComposite(
      $creds, $callCreds);
$channel1 = new Grpc\Channel('localhost:1',
      ["credentials" =>
        $credsWithCallCreds]);
$channel2 = new Grpc\Channel('localhost:1',
      ["credentials" =>
        $credsWithCallCreds]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$channel1->close();
$channel2->close();
//testPersistentChannelWithDifferentCallCredentials()
$callCreds1 = Grpc\CallCredentials::createFromPlugin(
      'callbackFunc');
$callCreds2 = Grpc\CallCredentials::createFromPlugin(
      'callbackFunc2');
$creds1 = Grpc\ChannelCredentials::createSsl();
$creds2 = Grpc\ChannelCredentials::createComposite(
      $creds1, $callCreds1);
$creds3 = Grpc\ChannelCredentials::createComposite(
      $creds1, $callCreds2);
$channel1 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds1]);
$channel2 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds2]);
$channel3 = new Grpc\Channel('localhost:1',
      ["credentials" => $creds3]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$state = $channel3->getConnectivityState();
$channel1->close();
$channel2->close();
$channel3->close();
// testPersistentChannelForceNew()
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:1',
      ["force_new" => true]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$channel1->close();
$channel2->close();
// testPersistentChannelForceNewOldChannelIdle()
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:1',
      ["force_new" => true]);
$channel3 = new Grpc\Channel('localhost:1', []);
$state = $channel2->getConnectivityState(true);
waitUntilNotIdle($channel2);
$state = $channel1->getConnectivityState();
$state = $channel2->getConnectivityState();
$state = $channel3->getConnectivityState();
$channel1->close();
$channel2->close();
// testPersistentChannelForceNewNewChannelClose()
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:1',
      ["force_new" => true]);
$channel3 = new Grpc\Channel('localhost:1', []);
$channel1->close();
$state = $channel2->getConnectivityState();
try {
  $state = $channel3->getConnectivityState();
}
catch (\Exception $e) {
}
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:1',
      ["force_new" => true]);
$channel3 = new Grpc\Channel('localhost:1', []);
$channel2->close();
$state = $channel1->getConnectivityState();
$state = $channel1->getConnectivityState(true);
$state = $channel1->getConnectivityState();
$channel1->close();

// ====================== BEGIN OF ChannelTest ==================
// testCreateSslWith3Null()
$channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
    null);
// testCreateSslWith3NullString()
$channel_credentials = Grpc\ChannelCredentials::createSsl('', '', '');
// testCreateInsecure()
$channel_credentials = Grpc\ChannelCredentials::createInsecure();
// testInvalidCreateSsl()
try {
  $channel_credentials = Grpc\ChannelCredentials::createSsl([]);
}
catch (\Exception $e) {
}
try {
  $channel_credentials = Grpc\ChannelCredentials::createComposite(
    'something', 'something');
}
catch (\Exception $e) {
}
// ====================== BEGIN OF CallTest ==================
$server = new Grpc\Server([]);
$port = $server->addHttp2Port('0.0.0.0:0');
// setUp()
$channel = new Grpc\Channel('localhost:'.$port, []);
$call = new Grpc\Call($channel,
    '/foo',
    Grpc\Timeval::infFuture());
// testConstructor()
// testAddEmptyMetadata()
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => [],
];
$result = $call->startBatch($batch);
// testAddSingleMetadata()
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['key' => ['value']],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
$result = $call->startBatch($batch);
// testAddMultiValueMetadata()
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['key' => ['value1', 'value2']],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
$result = $call->startBatch($batch);
// testAddSingleAndMultiValueMetadata()
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['key1' => ['value1'],
      'key2' => ['value2',
        'value3', ], ],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
$result = $call->startBatch($batch);
// testGetPeer()
$call->getPeer();
// testCancel()
$call->cancel();
// testInvalidStartBatchKey()
$batch = [
    'invalid' => ['key1' => 'value1'],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
try {
  $result = $call->startBatch($batch);
} catch (\Exception $e) {}
// testInvalidMetadataStrKey()
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['Key' => ['value1', 'value2']],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
try{
  $result = $call->startBatch($batch);
} catch (\Exception $e) {}
// testInvalidMetadataIntKey()
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => [1 => ['value1', 'value2']],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
try{
  $result = $call->startBatch($batch);
} catch (\Exception $e) {}
// testInvalidMetadataInnerValue()
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['key1' => 'value1'],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
try {
  $result = $call->startBatch($batch);
} catch (\Exception $e) {}
// testInvalidConstuctor()
try {
  $call = new Grpc\Call();
} catch (\Exception $e) {}
// testInvalidConstuctor2()
try {
  $call = new Grpc\Call('hi', 'hi', 'hi');
} catch (\Exception $e) {}
// testInvalidSetCredentials()
try {
  $call->setCredentials('hi');
} catch (\Exception $e) {}
// testInvalidSetCredentials2()
try {
  $call->setCredentials([]);
} catch (\Exception $e) {}
// tearDown()
$channel->close();
// ====================== BEGIN OF CallCredentialsTest ==================
// testCreateSslWith3Null()
$channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
    null);
// testCreateSslWith3NullString()
$channel_credentials = Grpc\ChannelCredentials::createSsl('', '', '');
// testCreateInsecure()
$channel_credentials = Grpc\ChannelCredentials::createInsecure();
// testInvalidCreateSsl()
try {
  $channel_credentials = Grpc\ChannelCredentials::createSsl([]);
} catch (\Exception $e) {}
// testInvalidCreateComposite()
try {
  $channel_credentials = Grpc\ChannelCredentials::createComposite(
    'something', 'something');
} catch (\Exception $e) {}
// ====================== BEGIN OF CallCredentials2Test ==================
// setUp()
$credentials = Grpc\ChannelCredentials::createSsl(
    file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
$server_credentials = Grpc\ServerCredentials::createSsl(
    null,
    file_get_contents(dirname(__FILE__).'/../data/server1.key'),
    file_get_contents(dirname(__FILE__).'/../data/server1.pem'));
$server = new Grpc\Server();
$port = $server->addSecureHttp2Port('0.0.0.0:0',
    $server_credentials);
$server->start();
$host_override = 'foo.test.google.fr';
$channel = new Grpc\Channel(
    'localhost:'.$port,
    [
      'grpc.ssl_target_name_override' => $host_override,
      'grpc.default_authority' => $host_override,
      'credentials' => $credentials,
    ]
);
function callCredscallbackFunc($context)
{
  is_string($context->service_url);
  is_string($context->method_name);
  return ['k1' => ['v1'], 'k2' => ['v2']];
}
// testCreateFromPlugin()
$deadline = Grpc\Timeval::infFuture();
$status_text = 'xyz';
$call = new Grpc\Call($channel,
    '/abc/dummy_method',
    $deadline,
    $host_override);

$call_credentials = Grpc\CallCredentials::createFromPlugin(
    'callCredscallbackFunc');
$call->setCredentials($call_credentials);

$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
]);
$event = $server->requestCall();
$metadata = $event->metadata;
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
$event = $call->startBatch([
    Grpc\OP_RECV_INITIAL_METADATA => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
$status = $event->status;
unset($call);
unset($server_call);

function invalidKeyCallbackFunc($context)
{
  is_string($context->service_url);
  is_string($context->method_name);
  return ['K1' => ['v1']];
}

// testCallbackWithInvalidKey()
$deadline = Grpc\Timeval::infFuture();
$status_text = 'xyz';
$call = new Grpc\Call($channel,
    '/abc/dummy_method',
    $deadline,
    $host_override);

$call_credentials = Grpc\CallCredentials::createFromPlugin(
    'invalidKeyCallbackFunc');
$call->setCredentials($call_credentials);
$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);


function invalidReturnCallbackFunc($context)
{
  is_string($context->service_url);
  is_string($context->method_name);
  return 'a string';
}

// testCallbackWithInvalidReturnValue()
$deadline = Grpc\Timeval::infFuture();
$status_text = 'xyz';
$call = new Grpc\Call($channel,
    '/abc/dummy_method',
    $deadline,
    $host_override);

$call_credentials = Grpc\CallCredentials::createFromPlugin(
    'invalidReturnCallbackFunc');
$call->setCredentials($call_credentials);

$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
// tearDown()
unset($channel);
unset($server);
// ====================== BEGIN OF EndtoEndTest =================
// setUp()
$server = new Grpc\Server([]);
$port = $server->addHttp2Port('0.0.0.0:0');
$channel = new Grpc\Channel('localhost:'.$port, []);
$server->start();
// testSimpleRequestBody()
$deadline = Grpc\Timeval::infFuture();
$status_text = 'xyz';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
]);
$event = $server->requestCall();
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

$event = $call->startBatch([
    Grpc\OP_RECV_INITIAL_METADATA => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
$status = $event->status;
unset($call);
unset($server_call);
// testMessageWriteFlags()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'message_write_flags_test';
$status_text = 'xyz';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_MESSAGE => ['message' => $req_text,
      'flags' => Grpc\WRITE_NO_COMPRESS, ],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
]);
$event = $server->requestCall();
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
unset($call);
unset($server_call);
// testClientServerFullRequestResponse()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);

$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
]);
$event = $server->requestCall();
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
$event = $call->startBatch([
    Grpc\OP_RECV_INITIAL_METADATA => true,
    Grpc\OP_RECV_MESSAGE => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
unset($call);
unset($server_call);
// testInvalidClientMessageArray()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
try {
  $event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_SEND_MESSAGE => 'invalid',
  ]);
} catch (\Exception $e) {}
// testInvalidClientMessageString()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
try{
  $event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_SEND_MESSAGE => ['message' => 0],
  ]);
} catch (\Exception $e) {}
// testInvalidClientMessageFlags()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
try{
  $event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_SEND_MESSAGE => ['message' => 'abc',
      'flags' => 'invalid',
    ],
  ]);
} catch (\Exception $e) {}
// testInvalidServerStatusMetadata()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
]);
$event = $server->requestCall();
$server_call = $event->call;
try {
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
} catch (\Exception $e) {}
// testInvalidServerStatusCode()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
]);
$event = $server->requestCall();
$server_call = $event->call;
try {
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
} catch (\Exception $e) {}
// testMissingServerStatusCode()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
]);
$event = $server->requestCall();
$server_call = $event->call;
try {
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
} catch (\Exception $e) {}
// testInvalidServerStatusDetails()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
]);
$event = $server->requestCall();
$server_call = $event->call;
try {
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
} catch (\Exception $e) {}
// testMissingServerStatusDetails()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
]);
$event = $server->requestCall();
$server_call = $event->call;
try {
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
} catch (\Exception $e) {}
// testInvalidStartBatchKey()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
try {
  $event = $call->startBatch([
    9999999 => [],
  ]);
} catch (\Exception $e) {}
// testInvalidStartBatch()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
try {
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
} catch (\Exception $e) {}
// testGetTarget()
// testGetConnectivityState()
$channel->getConnectivityState();
// testWatchConnectivityStateFailed()
$idle_state = $channel->getConnectivityState();
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(50000); // should timeout
$deadline = $now->add($delta);
$channel->watchConnectivityState(
    $idle_state, $deadline);
// testWatchConnectivityStateSuccess()
$idle_state = $channel->getConnectivityState(true);
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(3000000); // should finish well before
$deadline = $now->add($delta);
$new_state = $channel->getConnectivityState();
// testWatchConnectivityStateDoNothing()
$idle_state = $channel->getConnectivityState();
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(50000);
$deadline = $now->add($delta);
$new_state = $channel->getConnectivityState();
// testGetConnectivityStateInvalidParam()
try {
  $channel->getConnectivityState(new Grpc\Timeval());
} catch (\Exception $e) {}
// testWatchConnectivityStateInvalidParam()
try {
  $channel->watchConnectivityState(0, 1000);
} catch (\Exception $e) {}
// testChannelConstructorInvalidParam()
try {
  $channel = new Grpc\Channel('localhost:'.$port, null);
} catch (\Exception $e) {}
// testClose()
$channel->close();

// ====================== BEGIN OF SecureEndtoEndTest =================
$credentials = Grpc\ChannelCredentials::createSsl(
    file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
$server_credentials = Grpc\ServerCredentials::createSsl(
    null,
    file_get_contents(dirname(__FILE__).'/../data/server1.key'),
    file_get_contents(dirname(__FILE__).'/../data/server1.pem'));
$server = new Grpc\Server();
$port = $server->addSecureHttp2Port('0.0.0.0:0',
    $server_credentials);
$server->start();
$host_override = 'foo.test.google.fr';
$channel = new Grpc\Channel(
    'localhost:'.$port,
    [
      'grpc.ssl_target_name_override' => $host_override,
      'grpc.default_authority' => $host_override,
      'credentials' => $credentials,
    ]
);
// testSimpleRequestBody()
$deadline = Grpc\Timeval::infFuture();
$status_text = 'xyz';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline,
    $host_override);

$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
]);
$event = $server->requestCall();
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
$event = $call->startBatch([
    Grpc\OP_RECV_INITIAL_METADATA => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
$status = $event->status;
unset($call);
unset($server_call);
// testMessageWriteFlags()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'message_write_flags_test';
$status_text = 'xyz';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline,
    $host_override);

$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_MESSAGE => ['message' => $req_text,
      'flags' => Grpc\WRITE_NO_COMPRESS, ],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
]);
$event = $server->requestCall();
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
unset($call);
unset($server_call);
// testClientServerFullRequestResponse()
$deadline = Grpc\Timeval::infFuture();
$req_text = 'client_server_full_request_response';
$reply_text = 'reply:client_server_full_request_response';
$status_text = 'status:client_server_full_response_text';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline,
    $host_override);
$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
    Grpc\OP_SEND_MESSAGE => ['message' => $req_text],
]);
$event = $server->requestCall();
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
$event = $call->startBatch([
    Grpc\OP_RECV_INITIAL_METADATA => true,
    Grpc\OP_RECV_MESSAGE => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
unset($call);
unset($server_call);
// tearDown()
$channel->close();
// ====================== BEGIN OF SecureEndtoEndTest =================
// testConstructorWithInt()
$time = new Grpc\Timeval(1234);
// testConstructorWithNegative()
$time = new Grpc\Timeval(-123);
// testConstructorWithZero()
$time = new Grpc\Timeval(0);
// testConstructorWithOct()
$time = new Grpc\Timeval(0123);
// testConstructorWithHex()
$time = new Grpc\Timeval(0x1A);
// testConstructorWithFloat()
$time = new Grpc\Timeval(123.456);
// testCompareSame()
$zero = Grpc\Timeval::zero();
// testPastIsLessThanZero()
$zero = Grpc\Timeval::zero();
$past = Grpc\Timeval::infPast();
// testFutureIsGreaterThanZero()
$zero = Grpc\Timeval::zero();
$future = Grpc\Timeval::infFuture();
// testNowIsBetweenZeroAndFuture()
$zero = Grpc\Timeval::zero();
$future = Grpc\Timeval::infFuture();
$now = Grpc\Timeval::now();
// testNowAndAdd()
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(1000);
$deadline = $now->add($delta);
// testNowAndSubtract()
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(1000);
$deadline = $now->subtract($delta);
// testAddAndSubtract()
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(1000);
$deadline = $now->add($delta);
$back_to_now = $deadline->subtract($delta);
// testSimilar()
$a = Grpc\Timeval::now();
$delta = new Grpc\Timeval(1000);
$b = $a->add($delta);
$thresh = new Grpc\Timeval(1100);
$thresh = new Grpc\Timeval(900);
// testSleepUntil()
$curr_microtime = microtime(true);
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(1000);
$deadline = $now->add($delta);
$deadline->sleepUntil();
$done_microtime = microtime(true);
// testConstructorInvalidParam()
try {
  $delta = new Grpc\Timeval('abc');
} catch (\Exception $e) {}
// testAddInvalidParam()
$a = Grpc\Timeval::now();
try {
  $a->add(1000);
} catch (\Exception $e) {}
// testSubtractInvalidParam()
$a = Grpc\Timeval::now();
try {
  $a->subtract(1000);
} catch (\Exception $e) {}
// testCompareInvalidParam()
try {
  $a = Grpc\Timeval::compare(1000, 1100);
} catch (\Exception $e) {}
// testSimilarInvalidParam()
try {
  $a = Grpc\Timeval::similar(1000, 1100, 1200);
} catch (\Exception $e) {}

unset($time);
