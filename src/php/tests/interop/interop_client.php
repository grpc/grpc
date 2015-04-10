<?php
/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
require_once realpath(dirname(__FILE__) . '/../../vendor/autoload.php');
require 'DrSlump/Protobuf.php';
\DrSlump\Protobuf::autoload();
require 'empty.php';
require 'message_set.php';
require 'messages.php';
require 'test.php';
/**
 * Assertion function that always exits with an error code if the assertion is
 * falsy
 * @param $value Assertion value. Should be true.
 * @param $error_message Message to display if the assertion is false
 */
function hardAssert($value, $error_message) {
  if(!$value) {
    echo $error_message . "\n";
    exit(1);
  }
}

/**
 * Run the empty_unary test.
 * Currently not tested against any server as of 2014-12-04
 * @param $stub Stub object that has service methods
 */
function emptyUnary($stub) {
  list($result, $status) = $stub->EmptyCall(new grpc\testing\EmptyMessage())->wait();
  hardAssert($status->code === Grpc\STATUS_OK, 'Call did not complete successfully');
  hardAssert($result !== null, 'Call completed with a null response');
}

/**
 * Run the large_unary test.
 * Passes when run against the C++ server as of 2014-12-04
 * Not tested against any other server as of 2014-12-04
 * @param $stub Stub object that has service methods
 */
function largeUnary($stub) {
  $request_len = 271828;
  $response_len = 314159;

  $request = new grpc\testing\SimpleRequest();
  $request->setResponseType(grpc\testing\PayloadType::COMPRESSABLE);
  $request->setResponseSize($response_len);
  $payload = new grpc\testing\Payload();
  $payload->setType(grpc\testing\PayloadType::COMPRESSABLE);
  $payload->setBody(str_repeat("\0", $request_len));
  $request->setPayload($payload);

  list($result, $status) = $stub->UnaryCall($request)->wait();
  hardAssert($status->code === Grpc\STATUS_OK, 'Call did not complete successfully');
  hardAssert($result !== null, 'Call returned a null response');
  $payload = $result->getPayload();
  hardAssert($payload->getType() === grpc\testing\PayloadType::COMPRESSABLE,
         'Payload had the wrong type');
  hardAssert(strlen($payload->getBody()) === $response_len,
         'Payload had the wrong length');
  hardAssert($payload->getBody() === str_repeat("\0", $response_len),
         'Payload had the wrong content');
}

/**
 * Run the client_streaming test.
 * Not tested against any server as of 2014-12-04.
 * @param $stub Stub object that has service methods
 */
function clientStreaming($stub) {
  $request_lengths = array(27182, 8, 1828, 45904);

  $requests = array_map(
      function($length) {
        $request = new grpc\testing\StreamingInputCallRequest();
        $payload = new grpc\testing\Payload();
        $payload->setBody(str_repeat("\0", $length));
        $request->setPayload($payload);
        return $request;
      }, $request_lengths);

  list($result, $status) = $stub->StreamingInputCall($requests)->wait();
  hardAssert($status->code === Grpc\STATUS_OK, 'Call did not complete successfully');
  hardAssert($result->getAggregatedPayloadSize() === 74922,
              'aggregated_payload_size was incorrect');
}

/**
 * Run the server_streaming test.
 * Not tested against any server as of 2014-12-04.
 * @param $stub Stub object that has service methods.
 */
function serverStreaming($stub) {
  $sizes = array(31415, 9, 2653, 58979);

  $request = new grpc\testing\StreamingOutputCallRequest();
  $request->setResponseType(grpc\testing\PayloadType::COMPRESSABLE);
  foreach($sizes as $size) {
    $response_parameters = new grpc\testing\ResponseParameters();
    $response_parameters->setSize($size);
    $request->addResponseParameters($response_parameters);
  }

  $call = $stub->StreamingOutputCall($request);
  $i = 0;
  foreach($call->responses() as $value) {
    hardAssert($i < 4, 'Too many responses');
    $payload = $value->getPayload();
    hardAssert($payload->getType() === grpc\testing\PayloadType::COMPRESSABLE,
                'Payload ' . $i . ' had the wrong type');
    hardAssert(strlen($payload->getBody()) === $sizes[$i],
                'Response ' . $i . ' had the wrong length');
    $i += 1;
  }
  hardAssert($call->getStatus()->code === Grpc\STATUS_OK,
             'Call did not complete successfully');
}

/**
 * Run the ping_pong test.
 * Not tested against any server as of 2014-12-04.
 * @param $stub Stub object that has service methods.
 */
function pingPong($stub) {
  $request_lengths = array(27182, 8, 1828, 45904);
  $response_lengths = array(31415, 9, 2653, 58979);

  $call = $stub->FullDuplexCall();
  for($i = 0; $i < 4; $i++) {
    $request = new grpc\testing\StreamingOutputCallRequest();
    $request->setResponseType(grpc\testing\PayloadType::COMPRESSABLE);
    $response_parameters = new grpc\testing\ResponseParameters();
    $response_parameters->setSize($response_lengths[$i]);
    $request->addResponseParameters($response_parameters);
    $payload = new grpc\testing\Payload();
    $payload->setBody(str_repeat("\0", $request_lengths[$i]));
    $request->setPayload($payload);

    $call->write($request);
    $response = $call->read();

    hardAssert($response !== null, 'Server returned too few responses');
    $payload = $response->getPayload();
    hardAssert($payload->getType() === grpc\testing\PayloadType::COMPRESSABLE,
                'Payload ' . $i . ' had the wrong type');
    hardAssert(strlen($payload->getBody()) === $response_lengths[$i],
                'Payload ' . $i . ' had the wrong length');
  }
  $call->writesDone();
  hardAssert($call->read() === null, 'Server returned too many responses');
  hardAssert($call->getStatus()->code === Grpc\STATUS_OK,
              'Call did not complete successfully');
}

function cancelAfterFirstResponse($stub) {
  $call = $stub->FullDuplexCall();
  $request = new grpc\testing\StreamingOutputCallRequest();
  $request->setResponseType(grpc\testing\PayloadType::COMPRESSABLE);
  $response_parameters = new grpc\testing\ResponseParameters();
  $response_parameters->setSize(31415);
  $request->addResponseParameters($response_parameters);
  $payload = new grpc\testing\Payload();
  $payload->setBody(str_repeat("\0", 27182));
  $request->setPayload($payload);

  $call->write($request);
  $response = $call->read();

  $call->cancel();
  hardAssert($call->getStatus()->code === Grpc\STATUS_CANCELLED,
             'Call status was not CANCELLED');
}

$args = getopt('', array('server_host:', 'server_port:', 'test_case:'));
if (!array_key_exists('server_host', $args) ||
    !array_key_exists('server_port', $args) ||
    !array_key_exists('test_case', $args)) {
  throw new Exception('Missing argument');
}

$server_address = $args['server_host'] . ':' . $args['server_port'];

$credentials = Grpc\Credentials::createSsl(
    file_get_contents(dirname(__FILE__) . '/../data/ca.pem'));
$stub = new grpc\testing\TestServiceClient(
    new Grpc\BaseStub(
        $server_address,
        [
            'grpc.ssl_target_name_override' => 'foo.test.google.fr',
            'credentials' => $credentials
         ]));

echo "Connecting to $server_address\n";
echo "Running test case $args[test_case]\n";

switch($args['test_case']) {
  case 'empty_unary':
    emptyUnary($stub);
    break;
  case 'large_unary':
    largeUnary($stub);
    break;
  case 'client_streaming':
    clientStreaming($stub);
    break;
  case 'server_streaming':
    serverStreaming($stub);
    break;
  case 'ping_pong':
    pingPong($stub);
    break;
  case 'cancel_after_first_response':
    cancelAfterFirstResponse($stub);
    break;
  default:
    exit(1);
}
