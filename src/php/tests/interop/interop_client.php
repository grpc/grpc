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
require_once realpath(dirname(__FILE__).'/../../vendor/autoload.php');
require 'empty.php';
require 'messages.php';
require 'test.php';
use Google\Auth\CredentialsLoader;
use Google\Auth\ApplicationDefaultCredentials;
use GuzzleHttp\ClientInterface;

/**
 * Assertion function that always exits with an error code if the assertion is
 * falsy.
 *
 * @param $value Assertion value. Should be true.
 * @param $error_message Message to display if the assertion is false
 */
function hardAssert($value, $error_message)
{
    if (!$value) {
        echo $error_message."\n";
        exit(1);
    }
}

/**
 * Run the empty_unary test.
 *
 * @param $stub Stub object that has service methods
 */
function emptyUnary($stub)
{
    list($result, $status) = $stub->EmptyCall(new grpc\testing\EmptyMessage())->wait();
    hardAssert($status->code === Grpc\STATUS_OK, 'Call did not complete successfully');
    hardAssert($result !== null, 'Call completed with a null response');
}

/**
 * Run the large_unary test.
 *
 * @param $stub Stub object that has service methods
 */
function largeUnary($stub)
{
    performLargeUnary($stub);
}

/**
 * Shared code between large unary test and auth test.
 *
 * @param $stub Stub object that has service methods
 * @param $fillUsername boolean whether to fill result with username
 * @param $fillOauthScope boolean whether to fill result with oauth scope
 */
function performLargeUnary($stub, $fillUsername = false, $fillOauthScope = false,
                           $metadata = [])
{
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

    list($result, $status) = $stub->UnaryCall($request, $metadata)->wait();
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
 *
 * @param $stub Stub object that has service methods
 * @param $args array command line args
 */
function serviceAccountCreds($stub, $args)
{
    if (!array_key_exists('oauth_scope', $args)) {
        throw new Exception('Missing oauth scope');
    }
    $jsonKey = json_decode(
        file_get_contents(getenv(CredentialsLoader::ENV_VAR)),
        true);
    $result = performLargeUnary($stub, $fillUsername = true, $fillOauthScope = true);
    hardAssert($result->getUsername() == $jsonKey['client_email'],
             'invalid email returned');
    hardAssert(strpos($args['oauth_scope'], $result->getOauthScope()) !== false,
             'invalid oauth scope returned');
}

/**
 * Run the compute engine credentials auth test.
 * Has not been run from gcloud as of 2015-05-05.
 *
 * @param $stub Stub object that has service methods
 * @param $args array command line args
 */
function computeEngineCreds($stub, $args)
{
    if (!array_key_exists('oauth_scope', $args)) {
        throw new Exception('Missing oauth scope');
    }
    if (!array_key_exists('default_service_account', $args)) {
        throw new Exception('Missing default_service_account');
    }
    $result = performLargeUnary($stub, $fillUsername = true, $fillOauthScope = true);
    hardAssert($args['default_service_account'] == $result->getUsername(),
             'invalid email returned');
}

/**
 * Run the jwt token credentials auth test.
 *
 * @param $stub Stub object that has service methods
 * @param $args array command line args
 */
function jwtTokenCreds($stub, $args)
{
    $jsonKey = json_decode(
        file_get_contents(getenv(CredentialsLoader::ENV_VAR)),
        true);
    $result = performLargeUnary($stub, $fillUsername = true, $fillOauthScope = true);
    hardAssert($result->getUsername() == $jsonKey['client_email'],
             'invalid email returned');
}

/**
 * Run the oauth2_auth_token auth test.
 *
 * @param $stub Stub object that has service methods
 * @param $args array command line args
 */
function oauth2AuthToken($stub, $args)
{
    $jsonKey = json_decode(
        file_get_contents(getenv(CredentialsLoader::ENV_VAR)),
        true);
    $result = performLargeUnary($stub, $fillUsername = true, $fillOauthScope = true);
    hardAssert($result->getUsername() == $jsonKey['client_email'],
             'invalid email returned');
}

/**
 * Run the per_rpc_creds auth test.
 *
 * @param $stub Stub object that has service methods
 * @param $args array command line args
 */
function perRpcCreds($stub, $args)
{
    $jsonKey = json_decode(
        file_get_contents(getenv(CredentialsLoader::ENV_VAR)),
        true);
    $auth_credentials = ApplicationDefaultCredentials::getCredentials(
        $args['oauth_scope']
    );
    $token = $auth_credentials->fetchAuthToken();
    $metadata = [CredentialsLoader::AUTH_METADATA_KEY => [sprintf('%s %s',
                          $token['token_type'],
                          $token['access_token'])]];
    $result = performLargeUnary($stub, $fillUsername = true, $fillOauthScope = true,
                              $metadata);
    hardAssert($result->getUsername() == $jsonKey['client_email'],
             'invalid email returned');
}

/**
 * Run the client_streaming test.
 *
 * @param $stub Stub object that has service methods
 */
function clientStreaming($stub)
{
    $request_lengths = [27182, 8, 1828, 45904];

    $requests = array_map(
        function ($length) {
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
 *
 * @param $stub Stub object that has service methods.
 */
function serverStreaming($stub)
{
    $sizes = [31415, 9, 2653, 58979];

    $request = new grpc\testing\StreamingOutputCallRequest();
    $request->setResponseType(grpc\testing\PayloadType::COMPRESSABLE);
    foreach ($sizes as $size) {
        $response_parameters = new grpc\testing\ResponseParameters();
        $response_parameters->setSize($size);
        $request->addResponseParameters($response_parameters);
    }

    $call = $stub->StreamingOutputCall($request);
    $i = 0;
    foreach ($call->responses() as $value) {
        hardAssert($i < 4, 'Too many responses');
        $payload = $value->getPayload();
        hardAssert($payload->getType() === grpc\testing\PayloadType::COMPRESSABLE,
                'Payload '.$i.' had the wrong type');
        hardAssert(strlen($payload->getBody()) === $sizes[$i],
                'Response '.$i.' had the wrong length');
        $i += 1;
    }
    hardAssert($call->getStatus()->code === Grpc\STATUS_OK,
             'Call did not complete successfully');
}

/**
 * Run the ping_pong test.
 *
 * @param $stub Stub object that has service methods.
 */
function pingPong($stub)
{
    $request_lengths = [27182, 8, 1828, 45904];
    $response_lengths = [31415, 9, 2653, 58979];

    $call = $stub->FullDuplexCall();
    for ($i = 0; $i < 4; ++$i) {
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
                'Payload '.$i.' had the wrong type');
        hardAssert(strlen($payload->getBody()) === $response_lengths[$i],
                'Payload '.$i.' had the wrong length');
    }
    $call->writesDone();
    hardAssert($call->read() === null, 'Server returned too many responses');
    hardAssert($call->getStatus()->code === Grpc\STATUS_OK,
              'Call did not complete successfully');
}

/**
 * Run the empty_stream test.
 *
 * @param $stub Stub object that has service methods.
 */
function emptyStream($stub)
{
    $call = $stub->FullDuplexCall();
    $call->writesDone();
    hardAssert($call->read() === null, 'Server returned too many responses');
    hardAssert($call->getStatus()->code === Grpc\STATUS_OK,
             'Call did not complete successfully');
}

/**
 * Run the cancel_after_begin test.
 *
 * @param $stub Stub object that has service methods.
 */
function cancelAfterBegin($stub)
{
    $call = $stub->StreamingInputCall();
    $call->cancel();
    list($result, $status) = $call->wait();
    hardAssert($status->code === Grpc\STATUS_CANCELLED,
             'Call status was not CANCELLED');
}

/**
 * Run the cancel_after_first_response test.
 *
 * @param $stub Stub object that has service methods.
 */
function cancelAfterFirstResponse($stub)
{
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

function timeoutOnSleepingServer($stub)
{
    $call = $stub->FullDuplexCall(['timeout' => 1000]);
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

$args = getopt('', ['server_host:', 'server_port:', 'test_case:',
                    'use_tls::', 'use_test_ca::',
                    'server_host_override:', 'oauth_scope:',
                    'default_service_account:', ]);
if (!array_key_exists('server_host', $args)) {
    throw new Exception('Missing argument: --server_host is required');
}
if (!array_key_exists('server_port', $args)) {
    throw new Exception('Missing argument: --server_port is required');
}
if (!array_key_exists('test_case', $args)) {
    throw new Exception('Missing argument: --test_case is required');
}

if ($args['server_port'] == 443) {
    $server_address = $args['server_host'];
} else {
    $server_address = $args['server_host'].':'.$args['server_port'];
}

$test_case = $args['test_case'];

$host_override = 'foo.test.google.fr';
if (array_key_exists('server_host_override', $args)) {
    $host_override = $args['server_host_override'];
}

$use_tls = false;
if (array_key_exists('use_tls', $args) &&
    $args['use_tls'] != 'false') {
    $use_tls = true;
}

$use_test_ca = false;
if (array_key_exists('use_test_ca', $args) &&
    $args['use_test_ca'] != 'false') {
    $use_test_ca = true;
}

$opts = [];

if ($use_tls) {
    if ($use_test_ca) {
        $ssl_credentials = Grpc\ChannelCredentials::createSsl(
            file_get_contents(dirname(__FILE__).'/../data/ca.pem'));
    } else {
        $ssl_credentials = Grpc\Credentials::createSsl();
    }
    $opts['credentials'] = $ssl_credentials;
    $opts['grpc.ssl_target_name_override'] = $host_override;
}

if (in_array($test_case, ['service_account_creds',
    'compute_engine_creds', 'jwt_token_creds', ])) {
    if ($test_case == 'jwt_token_creds') {
        $auth_credentials = ApplicationDefaultCredentials::getCredentials();
    } else {
        $auth_credentials = ApplicationDefaultCredentials::getCredentials(
            $args['oauth_scope']
        );
    }
    $opts['update_metadata'] = $auth_credentials->getUpdateMetadataFunc();
}

if ($test_case == 'oauth2_auth_token') {
    $auth_credentials = ApplicationDefaultCredentials::getCredentials(
        $args['oauth_scope']
    );
    $token = $auth_credentials->fetchAuthToken();
    $update_metadata =
        function ($metadata,
                  $authUri = null,
                  ClientInterface $client = null) use ($token) {
            $metadata_copy = $metadata;
            $metadata_copy[CredentialsLoader::AUTH_METADATA_KEY] =
                [sprintf('%s %s',
                         $token['token_type'],
                         $token['access_token'])];

            return $metadata_copy;
        };
    $opts['update_metadata'] = $update_metadata;
}

$stub = new grpc\testing\TestServiceClient($server_address, $opts);

echo "Connecting to $server_address\n";
echo "Running test case $test_case\n";

switch ($test_case) {
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
    case 'oauth2_auth_token':
        oauth2AuthToken($stub, $args);
        break;
    case 'per_rpc_creds':
        perRpcCreds($stub, $args);
        break;
    default:
        echo "Unsupported test case $test_case\n";
        exit(1);
}
