
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

# Fix the following line to point to your installation
# This assumes that you are using protoc 3.2.0+ and the generated stubs
# were being autoloaded via composer.

require_once(dirname(__FILE__) . '/../../lib/Grpc/AbstractCall.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/BidiStreamingCall.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/BaseStub.php');

include 'vendor/autoload.php';

// periodicaly drain completed events until all calls completed
$w = new EvTimer(0., 0.05, function ($w, $revents) {
    drainCompletionEvents();
});


$client = new Math\MathClient('localhost:50052', [
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
]);


$call = $client->DivMany([] /* metadata */, [
    'async_callbacks' => [
        'onMetadata' => function ($initial_metadata) {
        },
        'onData' => function ($response) {
            if ($response) {
                echo $response->getQuotient() .
                    " R " . $response->getRemainder() . PHP_EOL;
            }
        },
        'onStatus' => function ($status) {
            echo "ERROR: status: " . $status->code .
                ": " . $status->details . PHP_EOL;
            EV::stop();
        },
    ],
]);


$inputErrors = 0;
echo "input div experssion (e.g. 3/2): " . PHP_EOL;
$wreadInput = new EvIo(STDIN, Ev::READ, function ($watcher, $revents) {
    $line = trim(fgets(STDIN));
    if ($line && strlen($line) > 0) {
        preg_match('/^([+-]?\d+)\s*\/\s*([+-]?\d+)$/', $line, $matches);
        if (strlen($matches[1]) == 0 || strlen($matches[2]) == 0) {
            global $inputErrors;
            $inputErrors++;
            if ($inputErrors > 2) {
                echo "invalid div expression, bye." . PHP_EOL;
                global $call;
                $call->writesDone();
            } else {
                echo "invalid div expression, try '3/2':" . PHP_EOL;
            }
            return;
        }

        $dividend = intval($matches[1]);
        $divisor = intval($matches[2]);
        echo $dividend . " / " . $divisor . " = ";
        global $call;
        $call->write(new Math\DivArgs([
            'dividend' => $dividend,
            'divisor' => $divisor,
        ]));
    }
});


Ev::run();
