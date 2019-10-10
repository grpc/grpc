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

require dirname(__FILE__).'/vendor/autoload.php';

$client = new Grpc\Gateway\Testing\EchoServiceClient('node-server:9090', [
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
]);


// unary call
$request = new Grpc\Gateway\Testing\EchoRequest();
$request->setMessage("Hello World!");

list($response, $status) = $client->Echo($request)->wait();

echo $response->getMessage()."\n";


// server streaming call
$stream_request = new Grpc\Gateway\Testing\ServerStreamingEchoRequest();
$stream_request->setMessage("stream message");
$stream_request->setMessageCount(5);

$responses = $client->ServerStreamingEcho($stream_request)->responses();

foreach ($responses as $response) {
    echo $response->getMessage()."\n";
}
