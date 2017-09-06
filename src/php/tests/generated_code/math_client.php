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
include 'vendor/autoload.php';

function p($line)
{
    echo "$line<br/>\n";
}

$host = 'localhost:50051';
p("Connecting to host: $host");
$client = new Math\MathClient($host, [
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
]);
p('Client class: '.get_class($client));
p('');

p('Running unary call test:');
$dividend = 7;
$divisor = 4;
$div_arg = new Math\DivArgs();
$div_arg->setDividend($dividend);
$div_arg->setDivisor($divisor);
$call = $client->Div($div_arg);
p('Call peer: '.$call->getPeer());
p("Dividing $dividend by $divisor");
list($response, $status) = $call->wait();
p('quotient = '.$response->getQuotient());
p('remainder = '.$response->getRemainder());
p('');

p('Running server streaming test:');
$limit = 7;
$fib_arg = new Math\FibArgs();
$fib_arg->setLimit($limit);
$call = $client->Fib($fib_arg);
$result_array = iterator_to_array($call->responses());
$result = '';
foreach ($result_array as $num) {
    $result .= ' '.$num->getNum();
}
p("The first $limit Fibonacci numbers are:".$result);
p('');

p('Running client streaming test:');
$call = $client->Sum();
for ($i = 0; $i <= $limit; ++$i) {
    $num = new Math\Num();
    $num->setNum($i);
    $call->write($num);
}
list($response, $status) = $call->wait();
p(sprintf('The first %d positive integers sum to: %d',
          $limit, $response->getNum()));
p('');

p('Running bidi-streaming test:');
$call = $client->DivMany();
for ($i = 0; $i < 7; ++$i) {
    $div_arg = new Math\DivArgs();
    $dividend = 2 * $i + 1;
    $divisor = 3;
    $div_arg->setDividend($dividend);
    $div_arg->setDivisor($divisor);
    $call->write($div_arg);
    p("client writing: $dividend / $divisor");
    $response = $call->read();
    p(sprintf('server writing: quotient = %d, remainder = %d',
            $response->getQuotient(), $response->getRemainder()));
}
$call->writesDone();
