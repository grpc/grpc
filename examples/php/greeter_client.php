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

// To generate the necessary proto classes:
// $ protoc --proto_path=../protos --php_out=. --grpc_out=.
//   --plugin=protoc-gen-grpc=../../bins/opt/grpc_php_plugin
//   ../protos/helloworld.proto

require dirname(__FILE__).'/vendor/autoload.php';

function tryReadStdin()
{
    $stdinFn = fopen('php://stdin', 'r');
    $stdin = '';
    if (!stream_isatty($stdinFn)) {
        $stdin = stream_get_contents($stdinFn);
    }
    $message = !empty($stdin) ? $stdin : null;
    return $message;
}

function greet($hostname, $name)
{
    $client = new Helloworld\GreeterClient($hostname, [
        'credentials' => Grpc\ChannelCredentials::createInsecure(),
    ]);
    $request = new Helloworld\HelloRequest();
    $request->setName($name);
    $messageLen = strlen($name);
    $time1 = microtime(true);
       list($response, $status) = $client->SayHello($request)->wait();
       $time2 = microtime(true);
       if ($status->code !== Grpc\STATUS_OK) {
        echo "ERROR: " . $status->code . ", " . $status->details . PHP_EOL;
        exit(1);
    }
    $reply = $response->getMessage();
    fwrite(STDERR, "== Message length: " . $messageLen
        . ", request time: " . ($time2 - $time1) * 1000 . " ms"
        . PHP_EOL);
    echo $reply . PHP_EOL;
    }

$name = tryReadStdin() ?? $argv[1] ?? 'world';
$hostname = !empty($argv[2]) ? $argv[2] : 'localhost:50051';
greet($hostname, $name);
