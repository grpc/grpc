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
$channel = new Grpc\Channel('localhost:50101', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);

// Test InsecureCredentials
assert('Grpc\Channel' == get_class($channel));

// Test ConnectivityState
$state = $channel->getConnectivityState();
assert(0 == $state);

$channel->close();

// Test GetTarget
$channel = new Grpc\Channel('localhost:50102', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
$target = $channel->getTarget();
assert(is_string($target) == true);
$channel->close();

// Test WatchConnectivityState
$channel = new Grpc\Channel('localhost:50103', ['credentials' => Grpc\ChannelCredentials::createInsecure()]);
$now = Grpc\Timeval::now();
$deadline = $now->add(new Grpc\Timeval(100*1000));

$state = $channel->watchConnectivityState(1, $deadline);
assert($state == true);

unset($now);
unset($deadline);

$channel->close();
