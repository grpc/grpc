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

//==============Channel Test====================

function callbackFunc($context)
{
  return [];
}

function callbackFunc2($context)
{
  return ["k1" => "v1"];
}

function assertConnecting($state)
{
  assert(($state == GRPC\CHANNEL_CONNECTING || $state == GRPC\CHANNEL_TRANSIENT_FAILURE) == true);
}

function waitUntilNotIdle($channel) {
  for ($i = 0; $i < 10; $i++) {
    $now = Grpc\Timeval::now();
    $deadline = $now->add(new Grpc\Timeval(10000));
    if ($channel->watchConnectivityState(GRPC\CHANNEL_IDLE,
      $deadline)) {
      return true;
    }
  }
  assert(true == false);
}

// Set up
$channel = new Grpc\Channel('localhost:0', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);

// Test InsecureCredentials
assert('Grpc\Channel' == get_class($channel));

// Test ConnectivityState
$state = $channel->getConnectivityState();
assert(0 == $state);

// Test GetConnectivityStateWithInt
$state = $channel->getConnectivityState(123);
assert(0 == $state);

// Test GetConnectivityStateWithString
$state = $channel->getConnectivityState('hello');
assert(0 == $state);

// Test GetConnectivityStateWithBool
$state = $channel->getConnectivityState(true);
assert(0 == $state);

$channel->close();

// Test GetTarget
$channel = new Grpc\Channel('localhost:8888', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
$target = $channel->getTarget();
assert(is_string($target) == true);
$channel->close();

// Test WatchConnectivityState
$channel = new Grpc\Channel('localhost:0', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
$now = Grpc\Timeval::now();
$deadline = $now->add(new Grpc\Timeval(100*1000));

$state = $channel->watchConnectivityState(1, $deadline);
assert($state == true);

unset($now);
unset($deadline);

$channel->close();

// Test InvalidConstructorWithNull
try {
  $channel = new Grpc\Channel();
  assert($channel == NULL);
}
catch (\Exception $e) {
}

// Test InvalidConstructorWith
try {
  $channel = new Grpc\Channel('localhost:0', 'invalid');
  assert($channel == NULL);
}
catch (\Exception $e) {
}

// Test InvalideCredentials
try {
  $channel = new Grpc\Channel('localhost:0', ['credentials' => new Grpc\Timeval(100)]);
}
catch (\Exception $e) {
}

// Test InvalidOptionsArrray
try {
  $channel = new Grpc\Channel('localhost:0', ['abc' => []]);
}
catch (\Exception $e) {
}

// Test InvalidGetConnectivityStateWithArray
$channel = new Grpc\Channel('localhost:0', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
try {
  $channel->getConnectivityState([]);
}
catch (\Exception $e) {
}

// Test InvalidWatchConnectivityState
try {
  $channel->watchConnectivityState([]);
}
catch (\Exception $e) {
}

// Test InvalidWatchConnectivityState2
try {
  $channel->watchConnectivityState(1, 'hi');
}
catch (\Exception $e) {
}

$channel->close();

// Test PersistentChannelSameHost
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:1', []);
$state = $channel1->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);
$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assertConnecting($state);

$channel1->close();
$channel2->close();

// Test PersistentChannelDifferentHost
$channel1 = new Grpc\Channel('localhost:1', ["grpc_target_persist_bound" => 3,]);
$channel2 = new Grpc\Channel('localhost:2', []);
$state = $channel1->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);
$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$channel1->close();
$channel2->close();

// Test PersistentChannelSameArgs
$channel1 = new Grpc\Channel('localhost:1', [
  "grpc_target_persist_bound" => 3,
  "abc" => "def",
  ]);
$channel2 = new Grpc\Channel('localhost:1', ["abc" => "def"]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assertConnecting($state);

$channel1->close();
$channel2->close();

// Test PersistentChannelDifferentArgs
$channel1 = new Grpc\Channel('localhost:1', []);
$channel2 = new Grpc\Channel('localhost:1', ["abc" => "def"]);
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$channel1->close();
$channel2->close();

// Test PersistentChannelSameChannelCredentials
$creds1 = Grpc\ChannelCredentials::createSsl();
$creds2 = Grpc\ChannelCredentials::createSsl();

$channel1 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds1,
                                     "grpc_target_persist_bound" => 3,
                                     ]);
$channel2 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds2]);

$state = $channel1->getConnectivityState(true);
print "state: ".$state."......................";
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assertConnecting($state);

$channel1->close();
$channel2->close();

// Test PersistentChannelDifferentChannelCredentials
$creds1 = Grpc\ChannelCredentials::createSsl();
$creds2 = Grpc\ChannelCredentials::createSsl(
    file_get_contents(dirname(__FILE__).'/../data/ca.pem'));

$channel1 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds1,
                                     "grpc_target_persist_bound" => 3,
                                     ]);
$channel2 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds2]);

$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$channel1->close();
$channel2->close();


// Test PersistentChannelSameChannelCredentialsRootCerts
$creds1 = Grpc\ChannelCredentials::createSsl(
    file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
$creds2 = Grpc\ChannelCredentials::createSsl(
    file_get_contents(dirname(__FILE__).'/../data/ca.pem'));

$channel1 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds1,
                                     "grpc_target_persist_bound" => 3,
                                     ]);
$channel2 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds2]);

$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assertConnecting($state);

$channel1->close();
$channel2->close();

// Test PersistentChannelDifferentSecureChannelCredentials
$creds1 = Grpc\ChannelCredentials::createSsl();
$creds2 = Grpc\ChannelCredentials::createInsecure();

$channel1 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds1,
                                     "grpc_target_persist_bound" => 3,
                                     ]);
$channel2 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds2]);

$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$channel1->close();
$channel2->close();

// Test PersistentChannelSharedChannelClose1
$channel1 = new Grpc\Channel('localhost:1', [
    "grpc_target_persist_bound" => 3,
]);
$channel2 = new Grpc\Channel('localhost:1', []);

$channel1->close();

$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);
$channel2->close();

// Test PersistentChannelSharedChannelClose2
$channel1 = new Grpc\Channel('localhost:1', [
    "grpc_target_persist_bound" => 3,
]);
$channel2 = new Grpc\Channel('localhost:1', []);

$channel1->close();

$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

try{
  $state = $channel1->getConnectivityState();
}
catch(\Exception $e){
}

$channel2->close();

//Test PersistentChannelCreateAfterClose
$channel1 = new Grpc\Channel('localhost:1', [
    "grpc_target_persist_bound" => 3,
]);

$channel1->close();

$channel2 = new Grpc\Channel('localhost:1', []);
$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$channel2->close();

//Test PersistentChannelSharedMoreThanTwo
$channel1 = new Grpc\Channel('localhost:1', [
    "grpc_target_persist_bound" => 3,
]);
$channel2 = new Grpc\Channel('localhost:1', []);
$channel3 = new Grpc\Channel('localhost:1', []);

$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assertConnecting($state);
$state = $channel3->getConnectivityState();
assertConnecting($state);

$channel1->close();

//Test PersistentChannelWithCallCredentials
$creds = Grpc\ChannelCredentials::createSsl();
$callCreds = Grpc\CallCredentials::createFromPlugin(
    'callbackFunc');
$credsWithCallCreds = Grpc\ChannelCredentials::createComposite(
    $creds, $callCreds);

$channel1 = new Grpc\Channel('localhost:1',
                                   ["credentials" =>
                                    $credsWithCallCreds,
                                    "grpc_target_persist_bound" => 3,
                                    ]);
$channel2 = new Grpc\Channel('localhost:1',
                                   ["credentials" =>
                                    $credsWithCallCreds]);

$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$channel1->close();
$channel2->close();

// Test PersistentChannelWithDifferentCallCredentials
$callCreds1 = Grpc\CallCredentials::createFromPlugin('callbackFunc');
$callCreds2 = Grpc\CallCredentials::createFromPlugin('callbackFunc2');

$creds1 = Grpc\ChannelCredentials::createSsl();
$creds2 = Grpc\ChannelCredentials::createComposite(
    $creds1, $callCreds1);
$creds3 = Grpc\ChannelCredentials::createComposite(
    $creds1, $callCreds2);

$channel1 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds1,
                                    "grpc_target_persist_bound" => 3,
                                    ]);
$channel2 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds2]);
$channel3 = new Grpc\Channel('localhost:1',
                                   ["credentials" => $creds3]);

$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);
$state = $channel3->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$channel1->close();
$channel2->close();
$channel3->close();

// Test PersistentChannelForceNew
$channel1 = new Grpc\Channel('localhost:1', [
    "grpc_target_persist_bound" => 2,
]);
$channel2 = new Grpc\Channel('localhost:1',
                                   ["force_new" => true]);

$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$channel1->close();
$channel2->close();

// Test PersistentChannelForceNewOldChannelIdle1
$channel1 = new Grpc\Channel('localhost:1', [
    "grpc_target_persist_bound" => 2,
]);
$channel2 = new Grpc\Channel('localhost:1',
                                   ["force_new" => true]);
$channel3 = new Grpc\Channel('localhost:1', []);

$state = $channel2->getConnectivityState(true);
waitUntilNotIdle($channel2);
$state = $channel1->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);
$state = $channel2->getConnectivityState();
assertConnecting($state);
$state = $channel3->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$channel1->close();
$channel2->close();

// Test PersistentChannelForceNewOldChannelIdle2
$channel1 = new Grpc\Channel('localhost:1', [
    "grpc_target_persist_bound" => 2,
]);
$channel2 = new Grpc\Channel('localhost:1', []);

$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel2);
$state = $channel1->getConnectivityState();
assertConnecting($state);
$state = $channel2->getConnectivityState();
assertConnecting($state);

$channel1->close();
$channel2->close();

// Test PersistentChannelForceNewOldChannelClose1
$channel1 = new Grpc\Channel('localhost:1', [
    "grpc_target_persist_bound" => 2,
]);
$channel2 = new Grpc\Channel('localhost:1',
                                   ["force_new" => true]);
$channel3 = new Grpc\Channel('localhost:1', []);

$channel1->close();

$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);
$state = $channel3->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

$channel2->close();
$channel3->close();

// Test PersistentChannelForceNewOldChannelClose2
$channel1 = new Grpc\Channel('localhost:1', [
    "grpc_target_persist_bound" => 2,
]);
$channel2 = new Grpc\Channel('localhost:1',
  ["force_new" => true]);
// channel3 shares with channel1
$channel3 = new Grpc\Channel('localhost:1', []);

$channel1->close();

$state = $channel2->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

// channel3 is still usable
$state = $channel3->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

// channel 1 is closed
try{
  $channel1->getConnectivityState();
}
catch(\Exception $e){
}

$channel2->close();
$channel3->close();

// Test PersistentChannelForceNewNewChannelClose
$channel1 = new Grpc\Channel('localhost:1', [
    "grpc_target_persist_bound" => 2,
]);
$channel2 = new Grpc\Channel('localhost:1',
                                   ["force_new" => true]);
$channel3 = new Grpc\Channel('localhost:1', []);

$channel2->close();

$state = $channel1->getConnectivityState();
assert(GRPC\CHANNEL_IDLE == $state);

// can still connect on channel1
$state = $channel1->getConnectivityState(true);
waitUntilNotIdle($channel1);

$state = $channel1->getConnectivityState();
assertConnecting($state);

$channel1->close();

//============== Call Test ====================
$server = new Grpc\Server([]);
$port = $server->addHttp2Port('0.0.0.0:53000');
$channel = new Grpc\Channel('localhost:'.$port, []);
$call = new Grpc\Call($channel,
    '/foo',
    Grpc\Timeval::infFuture());

// Test AddEmptyMetadata
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => [],
];
$result = $call->startBatch($batch);
assert($result->send_metadata == true);

// Test testAddSingleMetadata
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['key' => ['value']],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
$result = $call->startBatch($batch);
assert($result->send_metadata == true);

// Test AddMultiValue
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['key' => ['value1', 'value2']],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
$result = $call->startBatch($batch);
assert($result->send_metadata == true);

// Test AddSingleAndMultiValueMetadata
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['key1' => ['value1'],
                                      'key2' => ['value2',
                                                 'value3', ], ],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
$result = $call->startBatch($batch);
assert($result->send_metadata == true);

// Test AddMultiAndMultiValueMetadata
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['key1' => ['value1'],
                                      'key2' => ['value2',
                                                 'value3', ], ],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
$result = $call->startBatch($batch);
assert($result->send_metadata == true);

// Test GetPeer
assert(is_string($call->getPeer()) == true);

// Test Cancel
assert($call->cancel == NULL);

// Test InvalidStartBatchKey
$batch = [
  'invalid' => ['key1' => 'value1'],
];
try{
  $result = $call->startBatch($batch);
}
catch(\Exception $e){
}

// Test InvalideMetadataStrKey
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['Key' => ['value1', 'value2']],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
try{
  $result = $call->startBatch($batch);
}
catch(\Exception $e){
}

// Test InvalidMetadataIntKey
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => [1 => ['value1', 'value2']],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
try{
  $result = $call->startBatch($batch);
}
catch(\Exception $e){
}

// Test InvalidMetadataInnerValue
$batch = [
    Grpc\OP_SEND_INITIAL_METADATA => ['key1' => 'value1'],
];
$call = new Grpc\Call($channel,
  '/foo',
  Grpc\Timeval::infFuture());
try{
  $result = $call->startBatch($batch);
}
catch(\Exception $e){
}

// Test InvalidConstuctor
try {
  $call = new Grpc\Call();
} catch (\Exception $e) {}

// Test InvalidConstuctor2
try {
  $call = new Grpc\Call('hi', 'hi', 'hi');
} catch (\Exception $e) {}

// Test InvalidSetCredentials
try{
  $call->setCredentials('hi');
}
catch(\Exception $e){
}

// Test InvalidSetCredentials2
try {
  $call->setCredentials([]);
} catch (\Exception $e) {}


//============== CallCredentials Test 2 ====================
// Set Up
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

// Test CreateFromPlugin
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
assert($event->send_metadata == true);
assert($event->send_close == true);

$event = $server->requestCall();
assert(is_array($event->metadata) == true);

$metadata = $event->metadata;
assert(array_key_exists('k1', $metadata) == true);
assert(array_key_exists('k2', $metadata) == true);
assert($metadata['k1'] == ['v1']);
assert($metadata['k2'] == ['v2']);
assert('/abc/dummy_method' == $event->method);

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
assert($event->send_metadata == true);
assert($event->send_status == true);
assert($event->cancelled == false);

$event = $call->startBatch([
    Grpc\OP_RECV_INITIAL_METADATA => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
assert([] == $event->metadata);

$status = $event->status;
assert([] == $status->metadata);
assert(Grpc\STATUS_OK == $status->code);
assert($status_text == $status->details);

unset($call);
unset($server_call);

function invalidKeyCallbackFunc($context)
{
  is_string($context->service_url);
  is_string($context->method_name);
  return ['K1' => ['v1']];
}

// Test CallbackWithInvalidKey
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
assert($event->send_metadata == true);
assert($event->send_close == true);
assert(($event->status->code == Grpc\STATUS_UNAVAILABLE) == true);

function invalidReturnCallbackFunc($context)
{
  is_string($context->service_url);
  is_string($context->method_name);
  return 'a string';
}

// Test CallbackWithInvalidReturnValue
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

assert($event->send_metadata == true);
assert($event->send_close == true);
assert(($event->status->code == Grpc\STATUS_UNAVAILABLE) == true);

unset($channel);
unset($server);

//============== CallCredentials Test ====================
//Set Up
$credentials = Grpc\ChannelCredentials::createSsl(
    file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
$call_credentials = Grpc\CallCredentials::createFromPlugin('callbackFunc');
$credentials = Grpc\ChannelCredentials::createComposite(
  $credentials,
  $call_credentials
);
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

// Test CreateComposite
$call_credentials2 = Grpc\CallCredentials::createFromPlugin('callbackFunc');
$call_credentials3 = Grpc\CallCredentials::createComposite(
    $call_credentials,
    $call_credentials2
);
assert('Grpc\CallCredentials' == get_class($call_credentials3));

// Test CreateFromPluginInvalidParam
try{
  $call_credentials = Grpc\CallCredentials::createFromPlugin(
      'callbackFunc'
  );
}
catch(\Exception $e){}

// Test CreateCompositeInvalidParam
try{
  $call_credentials3 = Grpc\CallCredentials::createComposite(
    $call_credentials,
    $credentials
  );
}
catch(\Exception $e){}

unset($channel);
unset($server);


//============== EndToEnd Test ====================
// Set Up
$server = new Grpc\Server([]);
$port = $server->addHttp2Port('0.0.0.0:0');
$channel = new Grpc\Channel('localhost:'.$port, []);
$server->start();

// Test SimpleRequestBody
$deadline = Grpc\Timeval::infFuture();
$status_text = 'xyz';
$call = new Grpc\Call($channel,
    'dummy_method',
    $deadline);
$event = $call->startBatch([
    Grpc\OP_SEND_INITIAL_METADATA => [],
    Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
]);
assert($event->send_metadata == true);
assert($event->send_close == true);

$event = $server->requestCall();
assert('dummy_method' == $event->method);

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
assert($event->send_metadata == true);
assert($event->send_status == true);
assert($event->cancelled == false)
;
 $event = $call->startBatch([
    Grpc\OP_RECV_INITIAL_METADATA => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
$status = $event->status;
assert([] == $status->metadata);
assert(Grpc\STATUS_OK == $status->code);
assert($status_text == $status->details);

unset($call);
unset($server_call);

// Test MessageWriteFlags
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
assert($event->send_metadata == true);
assert($event->send_close == true);

$event = $server->requestCall();
assert('dummy_method' == $event->method);

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

assert([] == $status->metadata);
assert(Grpc\STATUS_OK == $status->code);
assert($status_text == $status->details);

unset($call);
unset($server_call);

// Test ClientServerFullRequestResponse
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
assert($event->send_metadata == true);
assert($event->send_close == true);
assert($event->send_message == true);

$event = $server->requestCall();
assert('dummy_method' == $event->method);
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
assert($event->send_metadata == true);
assert($event->send_status == true);
assert($event->send_message == true);
assert($event->cancelled == false);
assert($req_text == $event->message);

$event = $call->startBatch([
    Grpc\OP_RECV_INITIAL_METADATA => true,
    Grpc\OP_RECV_MESSAGE => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
assert([] == $event->metadata);
assert($reply_text == $event->message);
$status = $event->status;
assert([] == $status->metadata);
assert(Grpc\STATUS_OK == $status->code);
assert($status_text == $status->details);

unset($call);
unset($server_call);

// Test InvalidClientMessageArray
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

// Test InvalidClientMessageString
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

// Test InvalidClientMessageFlags
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

// Test InvalidServerStatusMetadata
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
assert($event->send_metadata == true);
assert($event->send_close == true);
assert($event->send_message == true);

$event = $server->requestCall();
assert('dummy_method' == $event->method);
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

// Test InvalidServerStatusCode
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
assert($event->send_metadata == true);
assert($event->send_close == true);
assert($event->send_message == true);

$event = $server->requestCall();
assert('dummy_method' == $event->method);
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

// Test MissingServerStatusCode
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

// Test InvalidServerStatusDetails
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

// Test MissingServerStatusDetails
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

// Test InvalidStartBatchKey
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

// Test InvalidStartBatch
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

// Test GetTarget
assert(is_string($channel->getTarget()) == true);

// Test GetConnectivityState
assert(($channel->getConnectivityState() ==
                          Grpc\CHANNEL_IDLE) == true);

// Test WatchConnectivityStateFailed
$idle_state = $channel->getConnectivityState();
assert(($idle_state == Grpc\CHANNEL_IDLE) == true);

$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(50000); // should timeout
$deadline = $now->add($delta);
assert($channel->watchConnectivityState(
    $idle_state, $deadline) == false);

// Test WatchConnectivityStateSuccess()
$idle_state = $channel->getConnectivityState(true);
assert(($idle_state == Grpc\CHANNEL_IDLE) == true);

$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(3000000); // should finish well before
$deadline = $now->add($delta);
$new_state = $channel->getConnectivityState();
assert($new_state != $idle_state);

// Test WatchConnectivityStateDoNothing
$idle_state = $channel->getConnectivityState();
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(50000);
$deadline = $now->add($delta);
assert(!$channel->watchConnectivityState(
        $idle_state, $deadline));

$new_state = $channel->getConnectivityState();
assert($new_state == Grpc\CHANNEL_IDLE);

// Test GetConnectivityStateInvalidParam
try {
  $channel->getConnectivityState(new Grpc\Timeval());
} catch (\Exception $e) {}
// Test WatchConnectivityStateInvalidParam
try {
  $channel->watchConnectivityState(0, 1000);
} catch (\Exception $e) {}
// Test ChannelConstructorInvalidParam
try {
  $channel = new Grpc\Channel('localhost:'.$port, null);
} catch (\Exception $e) {}
// testClose()
$channel->close();


//============== SecureEndToEnd Test ====================
// Set Up

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

// Test SimpleRequestBody
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
assert($event->send_metadata == true);
assert($event->send_close == true);

$event = $server->requestCall();
assert('dummy_method' == $event->method);

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
assert($event->send_metadata == true);
assert($event->send_status == true);
assert($event->cancelled == false);

$event = $call->startBatch([
    Grpc\OP_RECV_INITIAL_METADATA => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
assert([] == $event->metadata);
$status = $event->status;
assert([] == $status->metadata);
assert(Grpc\STATUS_OK == $status->code);
assert($status_text == $status->details);

unset($call);
unset($server_call);

// Test MessageWriteFlags
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
assert($event->send_metadata == true);
assert($event->send_close == true);

$event = $server->requestCall();
assert('dummy_method' == $event->method);

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
assert([] == $event->metadata);
$status = $event->status;
assert([] == $status->metadata);
assert(Grpc\STATUS_OK == $status->code);
assert($status_text == $status->details);unset($call);

unset($call);
unset($server_call);

// Test ClientServerFullRequestResponse
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
assert($event->send_metadata == true);
assert($event->send_close == true);
assert($event->send_message == true);

$event = $server->requestCall();
assert('dummy_method' == $event->method);

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
assert($event->send_metadata);
assert($event->send_status);
assert($event->send_message);
assert(!$event->cancelled);
assert($req_text == $event->message);

$event = $call->startBatch([
    Grpc\OP_RECV_INITIAL_METADATA => true,
    Grpc\OP_RECV_MESSAGE => true,
    Grpc\OP_RECV_STATUS_ON_CLIENT => true,
]);
assert([] == $event->metadata);
assert($reply_text == $event->message);
$status = $event->status;
assert([] == $status->metadata);
assert(Grpc\STATUS_OK == $status->code);
assert($status_text == $status->details);

unset($call);
unset($server_call);

$channel->close();


//============== Timeval Test ====================
// Test ConstructorWithInt
$time = new Grpc\Timeval(1234);
assert($time != NULL);
assert('Grpc\Timeval' == get_class($time));

// Test ConstructorWithNegative
$time = new Grpc\Timeval(-123);
assert($time != NULL);
assert('Grpc\Timeval' == get_class($time));

// Test ConstructorWithZero
$time = new Grpc\Timeval(0);
assert($time != NULL);
assert('Grpc\Timeval' == get_class($time));

// Test ConstructorWithOct
$time = new Grpc\Timeval(0123);
assert($time != NULL);
assert('Grpc\Timeval' == get_class($time));

// Test ConstructorWithHex
$time = new Grpc\Timeval(0x1A);
assert($time != NULL);
assert('Grpc\Timeval' == get_class($time));

// Test ConstructorWithFloat
$time = new Grpc\Timeval(123.456);
assert($time != NULL);
assert('Grpc\Timeval' == get_class($time));

// Test CompareSame
$zero = Grpc\Timeval::zero();
assert(0 == Grpc\Timeval::compare($zero, $zero));

// Test PastIsLessThanZero
$zero = Grpc\Timeval::zero();
$past = Grpc\Timeval::infPast();
assert(0 > Grpc\Timeval::compare($past, $zero));
assert(0 < Grpc\Timeval::compare($zero, $past));

// Test FutureIsGreaterThanZero
$zero = Grpc\Timeval::zero();
$future = Grpc\Timeval::infFuture();
assert(0 > Grpc\Timeval::compare($zero, $future));
assert(0 < Grpc\Timeval::compare($future, $zero));

// Test NowIsBetweenZeroAndFuture
$zero = Grpc\Timeval::zero();
$future = Grpc\Timeval::infFuture();
$now = Grpc\Timeval::now();
assert(0 > Grpc\Timeval::compare($zero, $now));
assert(0 > Grpc\Timeval::compare($now, $future));

// Test NowAndAdd
$now = Grpc\Timeval::now();
assert($now != NULL);
$delta = new Grpc\Timeval(1000);
$deadline = $now->add($delta);
assert(0 < Grpc\Timeval::compare($deadline, $now));

// Test NowAndSubtract
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(1000);
$deadline = $now->subtract($delta);
assert(0 > Grpc\Timeval::compare($deadline, $now));

// Test AddAndSubtract
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(1000);
$deadline = $now->add($delta);
$back_to_now = $deadline->subtract($delta);
assert(0 == Grpc\Timeval::compare($back_to_now, $now));

// Test Similar
$a = Grpc\Timeval::now();
$delta = new Grpc\Timeval(1000);
$b = $a->add($delta);
$thresh = new Grpc\Timeval(1100);
assert(Grpc\Timeval::similar($a, $b, $thresh));
$thresh = new Grpc\Timeval(900);
assert(!Grpc\Timeval::similar($a, $b, $thresh));

// Test SleepUntil
$curr_microtime = microtime(true);
$now = Grpc\Timeval::now();
$delta = new Grpc\Timeval(1000);
$deadline = $now->add($delta);
$deadline->sleepUntil();
$done_microtime = microtime(true);
assert(($done_microtime - $curr_microtime) > 0.0009);

// Test ConstructorInvalidParam
try {
  $delta = new Grpc\Timeval('abc');
} catch (\Exception $e) {}
// Test AddInvalidParam
$a = Grpc\Timeval::now();
try {
  $a->add(1000);
} catch (\Exception $e) {}
// Test SubtractInvalidParam
$a = Grpc\Timeval::now();
try {
  $a->subtract(1000);
} catch (\Exception $e) {}
// Test CompareInvalidParam
try {
  $a = Grpc\Timeval::compare(1000, 1100);
} catch (\Exception $e) {}
// Test SimilarInvalidParam
try {
  $a = Grpc\Timeval::similar(1000, 1100, 1200);
} catch (\Exception $e) {}
 unset($time);

 //============== Server Test ====================
 //Set Up
 $server = NULL;

 // Test ConstructorWithNull
$server = new Grpc\Server();
assert($server != NULL);

// Test ConstructorWithNullArray
$server = new Grpc\Server([]);
assert($server != NULL);

// Test ConstructorWithArray
$server = new Grpc\Server(['ip' => '127.0.0.1',
                                          'port' => '8080', ]);
assert($server != NULL);

// Test RequestCall
$server = new Grpc\Server();
$port = $server->addHttp2Port('0.0.0.0:0');
$server->start();
$channel = new Grpc\Channel('localhost:'.$port,
     [
         'force_new' => true,
         'credentials' => Grpc\ChannelCredentials::createInsecure()
     ]);

$deadline = Grpc\Timeval::infFuture();
$call = new Grpc\Call($channel, 'dummy_method', $deadline);

$event = $call->startBatch([Grpc\OP_SEND_INITIAL_METADATA => [],
                            Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
                            ]);

$c = $server->requestCall();
assert('dummy_method' == $c->method);
assert(is_string($c->host));

unset($call);
unset($channel);

// Test InvalidConstructorWithNumKeyOfArray
try{
  $server = new Grpc\Server([10 => '127.0.0.1',
                                         20 => '8080', ]);
}
catch(\Exception $e){}

// Test Invalid ArgumentException
try{
  $server = new Grpc\Server(['127.0.0.1', '8080']);
}
catch(\Exception $e){}

// Test InvalidAddHttp2Port
$server = new Grpc\Server([]);
try{
  $port = $server->addHttp2Port(['0.0.0.0:0']);
}
catch(\Exception $e){}

// Test InvalidAddSecureHttp2Port
$server = new Grpc\Server([]);
try{
  $port = $server->addSecureHttp2Port(['0.0.0.0:0']);
}
catch(\Exception $e){}

// Test InvalidAddSecureHttp2Port2
$server = new Grpc\Server();
try{
  $port = $server->addSecureHttp2Port('0.0.0.0:0');
}
catch(\Exception $e){}

// Test InvalidAddSecureHttp2Port3
$server = new Grpc\Server();
try{
  $port = $server->addSecureHttp2Port('0.0.0.0:0', 'invalid');
}
catch(\Exception $e){}
unset($server);


//============== ChannelCredential Test ====================
// Test CreateSslWith3Null
$channel_credentials = Grpc\ChannelCredentials::createSsl(null, null,
    null);
assert($channel_credentials != NULL);

// Test CreateSslWith3NullString
$channel_credentials = Grpc\ChannelCredentials::createSsl('', '', '');
assert($channel_credentials != NULL);

// Test CreateInsecure
$channel_credentials = Grpc\ChannelCredentials::createInsecure();
assert($channel_credentials == NULL);

// Test InvalidCreateSsl()
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

//============== Interceptor Test ====================
require_once(dirname(__FILE__).'/../../lib/Grpc/BaseStub.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/AbstractCall.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/UnaryCall.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/ClientStreamingCall.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/Interceptor.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/CallInvoker.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/DefaultCallInvoker.php');
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
            '/dummy_method',
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
        return $this->_clientStreamRequest('/dummy_method', [], $metadata, $options);
    }
}

class ChangeMetadataInterceptor extends Grpc\Interceptor
{
    public function interceptUnaryUnary($method,
                                        $argument,
                                        $deserialize,
                                        array $metadata = [],
                                        array $options = [],
                                        $continuation)
    {
        $metadata["foo"] = array('interceptor_from_unary_request');
        return $continuation($method, $argument, $deserialize, $metadata, $options);
    }
    public function interceptStreamUnary($method, $deserialize, array $metadata = [], array $options = [], $continuation)
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
                                        array $metadata = [],
                                        array $options = [],
                                        $continuation)
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
                                         array $metadata = [],
                                         array $options = [],
                                         $continuation)
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
                                        array $metadata = [],
                                        array $options = [],
                                        $continuation)
    {
        $argument->setData('intercepted_unary_request');
        return $continuation($method, $argument, $deserialize, $metadata, $options);
    }
    public function interceptStreamUnary($method, $deserialize, array $metadata = [], array $options = [], $continuation)
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
                                        array $metadata = [],
                                        array $options = [],
                                        $continuation)
    {
        $metadata["foo"] = array('interceptor_from_request_response');
    }
    public function interceptStreamUnary($method,
                                         array $metadata = [],
                                         array $options = [],
                                         $continuation)
    {
        $metadata["foo"] = array('interceptor_from_request_response');
    }
}

// Set Up
$server = new Grpc\Server([]);
$port = $server->addHttp2Port('0.0.0.0:0');
$channel = new Grpc\Channel('localhost:'.$port, [
    'force_new' => true,
    'credentials' => Grpc\ChannelCredentials::createInsecure()]);
$server->start();

// Test ClientChangeMetadataOneInterceptor
$req_text = 'client_request';
$channel_matadata_interceptor = new ChangeMetadataInterceptor();
$intercept_channel = Grpc\Interceptor::intercept($channel, $channel_matadata_interceptor);
$client = new InterceptorClient('localhost:'.$port, [
    'force_new' => true,
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
], $intercept_channel);
$req = new SimpleRequest($req_text);
$unary_call = $client->UnaryCall($req);
$event = $server->requestCall();
assert('/dummy_method' == $event->method);
assert(['interceptor_from_unary_request'] == $event->metadata['foo']);

$stream_call = $client->StreamCall();
$stream_call->write($req);
$event = $server->requestCall();
assert('/dummy_method' == $event->method);
assert(['interceptor_from_stream_request'] == $event->metadata['foo']);

unset($unary_call);
unset($stream_call);
unset($server_call);

// Test ClientChangeMetadataTwoInterceptor
$req_text = 'client_request';
$channel_matadata_interceptor = new ChangeMetadataInterceptor();
$channel_matadata_intercepto2 = new ChangeMetadataInterceptor2();
// test intercept separately.
$intercept_channel1 = Grpc\Interceptor::intercept($channel, $channel_matadata_interceptor);
$intercept_channel2 = Grpc\Interceptor::intercept($intercept_channel1, $channel_matadata_intercepto2);
$client = new InterceptorClient('localhost:'.$port, [
    'force_new' => true,
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
], $intercept_channel2);

$req = new SimpleRequest($req_text);
$unary_call = $client->UnaryCall($req);
$event = $server->requestCall();
assert('/dummy_method' == $event->method);
assert(['interceptor_from_unary_request'] == $event->metadata['foo']);
assert(['interceptor_from_unary_request'] == $event->metadata['bar']);

$stream_call = $client->StreamCall();
$stream_call->write($req);
$event = $server->requestCall();
assert('/dummy_method' == $event->method);
assert(['interceptor_from_stream_request'] == $event->metadata['foo']);
assert(['interceptor_from_stream_request'] == $event->metadata['bar']);

unset($unary_call);
unset($stream_call);
unset($server_call);

// test intercept by array.
$intercept_channel3 = Grpc\Interceptor::intercept($channel,
    [$channel_matadata_intercepto2, $channel_matadata_interceptor]);
$client = new InterceptorClient('localhost:'.$port, [
    'force_new' => true,
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
], $intercept_channel3);

$req = new SimpleRequest($req_text);
$unary_call = $client->UnaryCall($req);
$event = $server->requestCall();
assert('/dummy_method' == $event->method);
assert(['interceptor_from_unary_request'] == $event->metadata['foo']);
assert(['interceptor_from_unary_request'] == $event->metadata['bar']);

$stream_call = $client->StreamCall();
$stream_call->write($req);
$event = $server->requestCall();
assert('/dummy_method' == $event->method);
assert(['interceptor_from_stream_request'] == $event->metadata['foo']);
assert(['interceptor_from_stream_request'] == $event->metadata['bar']);

unset($unary_call);
unset($stream_call);
unset($server_call);


// Test ClientChangeRequestInterceptor
$req_text = 'client_request';
$change_request_interceptor = new ChangeRequestInterceptor();
$intercept_channel = Grpc\Interceptor::intercept($channel,
    $change_request_interceptor);
$client = new InterceptorClient('localhost:'.$port, [
    'force_new' => true,
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
], $intercept_channel);

$req = new SimpleRequest($req_text);
$unary_call = $client->UnaryCall($req);

$event = $server->requestCall();
assert('/dummy_method' == $event->method);
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
assert('intercepted_unary_request' == $event->message);

$stream_call = $client->StreamCall();
$stream_call->write($req);
$event = $server->requestCall();
assert('/dummy_method' == $event->method);
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
assert('intercepted_stream_request' == $event->message);

unset($unary_call);
unset($stream_call);
unset($server_call);

// Test ClientChangeStopCallInterceptor
$req_text = 'client_request';
$channel_request_interceptor = new StopCallInterceptor();
$intercept_channel = Grpc\Interceptor::intercept($channel,
    $channel_request_interceptor);
$client = new InterceptorClient('localhost:'.$port, [
    'force_new' => true,
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
], $intercept_channel);

$req = new SimpleRequest($req_text);
$unary_call = $client->UnaryCall($req);
assert($unary_call == NULL);


$stream_call = $client->StreamCall();
assert($stream_call == NULL);

unset($unary_call);
unset($stream_call);
unset($server_call);

// Test GetInterceptorChannelConnectivityState
$channel = new Grpc\Channel(
    'localhost:0',
    [
        'force_new' => true,
        'credentials' => Grpc\ChannelCredentials::createInsecure()
    ]
);
$interceptor_channel = Grpc\Interceptor::intercept($channel, new Grpc\Interceptor());
$state = $interceptor_channel->getConnectivityState();
assert(0 == $state);
$channel->close();

// Test InterceptorChannelWatchConnectivityState
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
assert($state);
unset($time);
unset($deadline);
$channel->close();

// Test InterceptorChannelClose
$channel = new Grpc\Channel(
    'localhost:0',
    [
        'force_new' => true,
        'credentials' => Grpc\ChannelCredentials::createInsecure()
    ]
);
$interceptor_channel = Grpc\Interceptor::intercept($channel, new Grpc\Interceptor());
assert($interceptor_channel != NULL);
$channel->close();

// Test InterceptorChannelGetTarget
$channel = new Grpc\Channel(
    'localhost:8888',
    [
        'force_new' => true,
        'credentials' => Grpc\ChannelCredentials::createInsecure()
    ]
);
$interceptor_channel = Grpc\Interceptor::intercept($channel, new Grpc\Interceptor());
$target = $interceptor_channel->getTarget();
assert(is_string($target));

$channel->close();
unset($server);


//============== CallInvoker Test ====================
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
      '/dummy_method',
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

// Set Up
$server = new Grpc\Server([]);
$port = $server->addHttp2Port('0.0.0.0:0');
$server->start();

// Test CreateDefaultCallInvoker
$call_invoker = new \Grpc\DefaultCallInvoker();

// Test CreateCallInvoker
$call_invoker = new CallInvokerUpdateChannel();

// Test CallInvokerAccessChannel
$call_invoker = new CallInvokerUpdateChannel();
$stub = new \Grpc\BaseStub('localhost:50051',
  ['credentials' => \Grpc\ChannelCredentials::createInsecure(),
    'grpc_call_invoker' => $call_invoker]);
assert($call_invoker->getChannel()->getTarget() == 'localhost:50050');
$call_invoker->getChannel()->close();

// Test ClientChangeRequestCallInvoker
$req_text = 'client_request';
$call_invoker = new CallInvokerChangeRequest();
$client = new CallInvokerClient('localhost:'.$port, [
    'force_new' => true,
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
    'grpc_call_invoker' => $call_invoker,
]);

$req = new CallInvokerSimpleRequest($req_text);
$unary_call = $client->UnaryCall($req);

$event = $server->requestCall();
assert('/dummy_method' == $event->method);
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
assert('intercepted_unary_request' == $event->message);
$call_invoker->getChannel()->close();
unset($unary_call);
unset($server_call);

unset($server);

<<<<<<< HEAD
<<<<<<< HEAD
echo "Went Through All Unit Tests..............\r\n";
=======
echo "Went Through All Unit Tests..............";
>>>>>>> add MemoryLeakTest
=======
echo "Went Through All Unit Tests..............\r\n";
>>>>>>> complete memory leak test


