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
  if (!$value) {
    echo $error_message . "\n";
    exit(1);
  }
}

/**
 * Run the empty_unary test.
 * Passes when run against the Node server as of 2015-04-30
 * @param $stub Stub object that has service methods
 */
function emptyUnary($stub) {
  list($result, $status) = $stub->EmptyCall(new grpc\testing\EmptyMessage())->wait();
  hardAssert($status->code === Grpc\STATUS_OK, 'Call did not complete successfully');
  hardAssert($result !== null, 'Call completed with a null response');
}

/**
 * Run the large_unary test.
 * Passes when run against the C++/Node server as of 2015-04-30
 * @param $stub Stub object that has service methods
 */
function largeUnary($stub) {
  performLargeUnary($stub);
}

/**
 * Shared code between large unary test and auth test
 * @param $stub Stub object that has service methods
 * @param $fillUsername boolean whether to fill result with username
 * @param $fillOauthScope boolean whether to fill result with oauth scope
 */
function performLargeUnary($stub, $fillUsername = false, $fillOauthScope = false) {
  $request_len = 271828;
  $response_len = 314159;

  $request = new grpc\testing\SimpleRequest();
  $request->setResponseType(grpc\testing\PayloadType::COMPRESSABLE);
  $request->setResponseSize($response_len);
  $payload = new grpc\testing\Payload();
  $payload->setType(grpc\testing\PayloadType::COMPRESSABLE);
  $payload->setBody(str_repeat("\0", $request_len));
  $request->setPayload($payload);
  $request->setFillUsername($fillUsername);
  $request->setFillOauthScope($fillOauthScope);

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
  return $result;
}

/**
 * Run the service account credentials auth test.
 * Passes when run against the cloud server as of 2015-04-30
 * @param $stub Stub object that has service methods
 * @param $args array command line args
 */
function serviceAccountCreds($stub, $args) {
  if (!array_key_exists('oauth_scope', $args)) {
    throw new Exception('Missing oauth scope');
  }
  $jsonKey = json_decode(
      file_get_contents(getenv(Google\Auth\CredentialsLoader::ENV_VAR)),
      true);
  $result = performLargeUnary($stub, $fillUsername=true, $fillOauthScope=true);
  hardAssert($result->getUsername() == $jsonKey['client_email'],
             'invalid email returned');
  hardAssert(strpos($args['oauth_scope'], $result->getOauthScope()) !== false,
             'invalid oauth scope returned');
}

/**
 * Run the compute engine credentials auth test.
 * Has not been run from gcloud as of 2015-05-05
 * @param $stub Stub object that has service methods
 * @param $args array command line args
 */
function computeEngineCreds($stub, $args) {
  if (!array_key_exists('oauth_scope', $args)) {
    throw new Exception('Missing oauth scope');
  }
  if (!array_key_exists('default_service_account', $args)) {
    throw new Exception('Missing default_service_account');
  }
  $result = performLargeUnary($stub, $fillUsername=true, $fillOauthScope=true);
  hardAssert($args['default_service_account'] == $result->getUsername(),
             'invalid email returned');
}

/**
 * Run the jwt token credentials auth test.
 * Passes when run against the cloud server as of 2015-05-12
 * @param $stub Stub object that has service methods
 * @param $args array command line args
 */
function jwtTokenCreds($stub, $args) {
  $jsonKey = json_decode(
      file_get_contents(getenv(Google\Auth\CredentialsLoader::ENV_VAR)),
      true);
  $result = performLargeUnary($stub, $fillUsername=true, $fillOauthScope=true);
  hardAssert($result->getUsername() == $jsonKey['client_email'],
             'invalid email returned');
}

/**
 * Run the client_streaming test.
 * Passes when run against the Node server as of 2015-04-30
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

  $call = $stub->StreamingInputCall();
  foreach ($requests as $request) {
    $call->write($request);
  }
  list($result, $status) = $call->wait();
  hardAssert($status->code === Grpc\STATUS_OK, 'Call did not complete successfully');
  hardAssert($result->getAggregatedPayloadSize() === 74922,
              'aggregated_payload_size was incorrect');
}

/**
 * Run the server_streaming test.
 * Passes when run against the Node server as of 2015-04-30
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
 * Passes when run against the Node server as of 2015-04-30
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

/**
 * Run the empty_stream test.
 * Passes when run against the Node server as of 2015-10-09
 * @param $stub Stub object that has service methods.
 */
function emptyStream($stub) {
  // for the current PHP implementation, $call->read() will wait
  // forever for a server response if the server is not sending any.
  // so this test is imeplemented as a timeout to indicate the absence
  // of receiving any response from the server
  $call = $stub->FullDuplexCall(array('timeout' => 100000));
  $call->writesDone();
  hardAssert($call->read() === null, 'Server returned too many responses');
  hardAssert($call->getStatus()->code === Grpc\STATUS_OK,
              'Call did not complete successfully');
}

/**
 * Run the cancel_after_begin test.
 * Passes when run against the Node server as of 2015-08-28
 * @param $stub Stub object that has service methods.
 */
function cancelAfterBegin($stub) {
  $call = $stub->StreamingInputCall();
  $call->cancel();
  list($result, $status) = $call->wait();
  hardAssert($status->code === Grpc\STATUS_CANCELLED,
             'Call status was not CANCELLED');
}

/**
 * Run the cancel_after_first_response test.
 * Passes when run against the Node server as of 2015-04-30
 * @param $stub Stub object that has service methods.
 */
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

function timeoutOnSleepingServer($stub) {
  $call = $stub->FullDuplexCall(array('timeout' => 1000));
  $request = new grpc\testing\StreamingOutputCallRequest();
  $request->setResponseType(grpc\testing\PayloadType::COMPRESSABLE);
  $response_parameters = new grpc\testing\ResponseParameters();
  $response_parameters->setSize(8);
  $request->addResponseParameters($response_parameters);
  $payload = new grpc\testing\Payload();
  $payload->setBody(str_repeat("\0", 9));
  $request->setPayload($payload);

  $call->write($request);
  $response = $call->read();

  hardAssert($call->getStatus()->code === Grpc\STATUS_DEADLINE_EXCEEDED,
             'Call status was not DEADLINE_EXCEEDED');
}

$args = getopt('', array('server_host:', 'server_port:', 'test_case:',
                         'server_host_override:', 'oauth_scope:',
                         'default_service_account:'));
if (!array_key_exists('server_host', $args) ||
    !array_key_exists('server_port', $args) ||
    !array_key_exists('test_case', $args)) {
  throw new Exception('Missing argument');
}

if ($args['server_port'] == 443) {
  $server_address = $args['server_host'];
} else {
  $server_address = $args['server_host'] . ':' . $args['server_port'];
}

if (!array_key_exists('server_host_override', $args)) {
  $args['server_host_override'] = 'foo.test.google.fr';
}

$ssl_cert_file = getenv('SSL_CERT_FILE');
if (!$ssl_cert_file) {
  $ssl_cert_file = dirname(__FILE__) . '/../data/ca.pem';
}

$credentials = Grpc\Credentials::createSsl(file_get_contents($ssl_cert_file));

$opts = [
    'grpc.ssl_target_name_override' => $args['server_host_override'],
    'credentials' => $credentials,
         ];

if (in_array($args['test_case'], array(
      'service_account_creds',
      'compute_engine_creds',
      'jwt_token_creds'))) {
  if ($args['test_case'] == 'jwt_token_creds') {
    $auth = Google\Auth\ApplicationDefaultCredentials::getCredentials();
  } else {
    $auth = Google\Auth\ApplicationDefaultCredentials::getCredentials(
      $args['oauth_scope']);
  }
  $opts['update_metadata'] = $auth->getUpdateMetadataFunc();
}

$stub = new grpc\testing\TestServiceClient($server_address, $opts);

echo "Connecting to $server_address\n";
echo "Running test case $args[test_case]\n";

switch ($args['test_case']) {
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
  case 'empty_stream':
    emptyStream($stub);
    break;
  case 'cancel_after_begin':
    cancelAfterBegin($stub);
    break;
  case 'cancel_after_first_response':
    cancelAfterFirstResponse($stub);
    break;
  case 'timeout_on_sleeping_server':
    timeoutOnSleepingServer($stub);
    break;
  case 'service_account_creds':
    serviceAccountCreds($stub, $args);
    break;
  case 'compute_engine_creds':
    computeEngineCreds($stub, $args);
    break;
  case 'jwt_token_creds':
    jwtTokenCreds($stub, $args);
    break;
  default:
    echo "Unsupported test case $args[test_case]\n";
    exit(1);
}
