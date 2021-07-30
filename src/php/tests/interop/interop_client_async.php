<?php
/*
 *
 * Copyright 2021 gRPC authors.
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
require_once realpath(dirname(__FILE__) . '/../../vendor/autoload.php');

// The following includes are needed when using protobuf 3.1.0
// and will suppress warnings when using protobuf 3.2.0+
@include_once 'src/proto/grpc/testing/test.pb.php';
@include_once 'src/proto/grpc/testing/test_grpc_pb.php';

use Google\Auth\CredentialsLoader;
use Google\Auth\ApplicationDefaultCredentials;
use GuzzleHttp\ClientInterface;

/**
 * Number of assertions performed
 */
$assertions = 0;

/**
 * Assertion function that always exits with an error code if the assertion is
 * falsy.
 *
 * @param $value Assertion value. Should be true.
 * @param $error_message Message to display if the assertion is false
 */
function hardAssert($value, $error_message)
{
    global $assertions;
    $assertions++;

    if (!$value) {
        echo $error_message . "\n";
        exit(1);
    }
}

function hardAssertIfStatusOk($status)
{
    global $assertions;
    $assertions++;

    if ($status->code !== Grpc\STATUS_OK) {
        echo "Call did not complete successfully. Status object:\n";
        var_dump($status);
        exit(1);
    }
}

/**
 * async calls requested and completed
 */
$call_count = 0;
$completed_count = 0;

/**
 * Run the empty_unary test.
 *
 * @param $stub Stub object that has service methods
 */
function emptyUnary($stub)
{
    $stub->EmptyCall(
        new Grpc\Testing\EmptyMessage(),
        [] /* metadata */,
        ["async_callbacks" => [
            "onData" => function ($response) {
                hardAssert($response !== null, 'Call completed with a null response');
            },
            "onStatus" => function ($status) {
                hardAssertIfStatusOk($status);
            },
        ]]
    );
    return 2;
}

/**
 * Run the large_unary test.
 *
 * @param $stub Stub object that has service methods
 */
function largeUnary($stub)
{
    return performLargeUnary($stub);
}

/**
 * Shared code between large unary test and auth test.
 *
 * @param $stub Stub object that has service methods
 * @param $fillUsername boolean whether to fill result with username
 * @param $fillOauthScope boolean whether to fill result with oauth scope
 */
function performLargeUnary(
    $stub,
    $fillUsername = false,
    $fillOauthScope = false,
    $callCredsCallback = false,
    $resultCallback = false
) {
    $request_len = 271828;
    $response_len = 314159;

    $request = new Grpc\Testing\SimpleRequest();
    $request->setResponseType(Grpc\Testing\PayloadType::COMPRESSABLE);
    $request->setResponseSize($response_len);
    $payload = new Grpc\Testing\Payload();
    $payload->setType(Grpc\Testing\PayloadType::COMPRESSABLE);
    $payload->setBody(str_repeat("\0", $request_len));
    $request->setPayload($payload);
    $request->setFillUsername($fillUsername);
    $request->setFillOauthScope($fillOauthScope);

    $options = ["async_callbacks" => [
        "onData" => function ($result) use ($response_len, $resultCallback) {
            hardAssert($result !== null, 'Call returned a null response');
            $payload = $result->getPayload();
            hardAssert(
                $payload->getType() === Grpc\Testing\PayloadType::COMPRESSABLE,
                'Payload had the wrong type'
            );
            hardAssert(
                strlen($payload->getBody()) === $response_len,
                'Payload had the wrong length'
            );
            hardAssert(
                $payload->getBody() === str_repeat("\0", $response_len),
                'Payload had the wrong content'
            );

            if ($resultCallback) {
                $resultCallback($result);
            }
        },
        "onStatus" => function ($status) {
            hardAssertIfStatusOk($status);
        },
    ]];
    if ($callCredsCallback) {
        $options['call_credentials_callback'] = $callCredsCallback;
    }

    $stub->UnaryCall($request, [], $options);
    return 5;
}

/**
 * Run the client_compressed_unary test.
 *
 * @param $stub Stub object that has service methods
 */
function clientCompressedUnary($stub)
{
    $request_len = 271828;
    $response_len = 314159;
    $falseBoolValue = new Grpc\Testing\BoolValue(['value' => false]);
    $trueBoolValue = new Grpc\Testing\BoolValue(['value' => true]);
    // 1. Probing for compression-checks support
    $payload = new Grpc\Testing\Payload([
        'body' => str_repeat("\0", $request_len),
    ]);
    $request = new Grpc\Testing\SimpleRequest([
        'payload' => $payload,
        'response_size' => $response_len,
        'expect_compressed' => $trueBoolValue, // lie
    ]);
    $stub->UnaryCall($request, [], ["async_callbacks" => [
        "onData" => function ($result) {
        },
        "onStatus" => function ($status) {
            hardAssert(
                $status->code === GRPC\STATUS_INVALID_ARGUMENT,
                'Received unexpected UnaryCall status code: ' .
                    $status->code
            );
        },
    ]]);
    // 2. with/without compressed message
    foreach ([true, false] as $compression) {
        $request->setExpectCompressed($compression ? $trueBoolValue : $falseBoolValue);
        $metadata = $compression ? [
            'grpc-internal-encoding-request' => ['gzip'],
        ] : [];

        $stub->UnaryCall($request, $metadata, ["async_callbacks" => [
            "onData" => function ($result) use ($response_len) {
                hardAssert($result !== null, 'Call returned a null response');
                $payload = $result->getPayload();
                hardAssert(
                    strlen($payload->getBody()) === $response_len,
                    'Payload had the wrong length'
                );
                hardAssert(
                    $payload->getBody() === str_repeat("\0", $response_len),
                    'Payload had the wrong content'
                );
            },
            "onStatus" => function ($status) {
                hardAssertIfStatusOk($status);
            },
        ]]);
    }

    return 9;
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
        true
    );
    return 2 + performLargeUnary(
        $stub,
        $fillUsername = true,
        $fillOauthScope = true,
        false /* callCredsCallback */,
        function ($result) use ($jsonKey, $args) {
            hardAssert(
                $result->getUsername() === $jsonKey['client_email'],
                'invalid email returned'
            );
            hardAssert(
                strpos($args['oauth_scope'], $result->getOauthScope()) !== false,
                'invalid oauth scope returned'
            );
        }
    );
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
    return 1 + performLargeUnary(
        $stub,
        $fillUsername = true,
        $fillOauthScope = true,
        false /* callCredsCallback */,
        function ($result) use ($args) {
            hardAssert(
                $args['default_service_account'] === $result->getUsername(),
                'invalid email returned'
            );
        }
    );
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
        true
    );
    return 1 + performLargeUnary(
        $stub,
        $fillUsername = true,
        $fillOauthScope = true,
        false /* callCredsCallback */,
        function ($result) use ($jsonKey) {
            hardAssert(
                $result->getUsername() === $jsonKey['client_email'],
                'invalid email returned'
            );
        }
    );
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
        true
    );
    return 1 + performLargeUnary(
        $stub,
        $fillUsername = true,
        $fillOauthScope = true,
        false /* callCredsCallback */,
        function ($result) use ($jsonKey) {
            hardAssert(
                $result->getUsername() === $jsonKey['client_email'],
                'invalid email returned'
            );
        }
    );
}

function updateAuthMetadataCallback($context)
{
    $authUri = $context->service_url;
    $methodName = $context->method_name;
    $auth_credentials = ApplicationDefaultCredentials::getCredentials();

    $metadata = [];
    $result = $auth_credentials->updateMetadata([], $authUri);
    foreach ($result as $key => $value) {
        $metadata[strtolower($key)] = $value;
    }

    return $metadata;
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
        true
    );
    return 1 + performLargeUnary(
        $stub,
        $fillUsername = true,
        $fillOauthScope = true,
        'updateAuthMetadataCallback',
        function ($result) use ($jsonKey) {
            hardAssert(
                $result->getUsername() === $jsonKey['client_email'],
                'invalid email returned'
            );
        }
    );
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
            $request = new Grpc\Testing\StreamingInputCallRequest();
            $payload = new Grpc\Testing\Payload();
            $payload->setBody(str_repeat("\0", $length));
            $request->setPayload($payload);

            return $request;
        },
        $request_lengths
    );

    $call = $stub->StreamingInputCall([] /* metadata */, [
        'async_callbacks' => [
            'onData' => function ($result) {
                hardAssert(
                    $result->getAggregatedPayloadSize() === 74922,
                    'aggregated_payload_size was incorrect'
                );
            },
            'onStatus' => function ($status) {
                hardAssertIfStatusOk($status);
            },
        ],
    ]);
    foreach ($requests as $request) {
        $call->write($request);
    }
    $call->writesDone();

    return 2;
}

/**
 * Run the client_compressed_streaming test.
 *
 * @param $stub Stub object that has service methods
 */
function clientCompressedStreaming($stub)
{
    $request_len = 27182;
    $request2_len = 45904;
    $response_len = 73086;
    $falseBoolValue = new Grpc\Testing\BoolValue(['value' => false]);
    $trueBoolValue = new Grpc\Testing\BoolValue(['value' => true]);

    // 1. Probing for compression-checks support

    $payload = new Grpc\Testing\Payload([
        'body' => str_repeat("\0", $request_len),
    ]);
    $request = new Grpc\Testing\StreamingInputCallRequest([
        'payload' => $payload,
        'expect_compressed' => $trueBoolValue, // lie
    ]);

    $call = $stub->StreamingInputCall([] /* metadata */, [
        'async_callbacks' => [
            'onData' => function ($result) {
            },
            'onStatus' => function ($status) {
                hardAssert(
                    $status->code === GRPC\STATUS_INVALID_ARGUMENT,
                    'Received unexpected StreamingInputCall status code: ' .
                        $status->code
                );
            },
        ],
    ]);
    $call->write($request);
    $call->writesDone();

    // 2. write compressed message

    $call = $stub->StreamingInputCall([
        'grpc-internal-encoding-request' => ['gzip'],
    ], [
        'async_callbacks' => [
            'onData' => function ($result) use ($response_len) {
                // 4.1 verify response
                hardAssert(
                    $result->getAggregatedPayloadSize() === $response_len,
                    'aggregated_payload_size was incorrect'
                );
            },
            'onStatus' => function ($status) {
                // 4.2 verify status
                hardAssertIfStatusOk($status);
            },
        ],
    ]);
    $request->setExpectCompressed($trueBoolValue);
    $call->write($request);

    // 3. write uncompressed message

    $payload2 = new Grpc\Testing\Payload([
        'body' => str_repeat("\0", $request2_len),
    ]);
    $request->setPayload($payload2);
    $request->setExpectCompressed($falseBoolValue);
    $call->write($request, [
        'flags' => 0x02 // GRPC_WRITE_NO_COMPRESS
    ]);
    $call->writesDone();

    return 3;
}

/**
 * Run the server_streaming test.
 *
 * @param $stub Stub object that has service methods.
 */
function serverStreaming($stub)
{
    $sizes = [31415, 9, 2653, 58979];

    $request = new Grpc\Testing\StreamingOutputCallRequest();
    $request->setResponseType(Grpc\Testing\PayloadType::COMPRESSABLE);
    foreach ($sizes as $size) {
        $response_parameters = new Grpc\Testing\ResponseParameters();
        $response_parameters->setSize($size);
        $request->getResponseParameters()[] = $response_parameters;
    }

    $i = 0;
    $stub->StreamingOutputCall($request, [] /* metadata */, ['async_callbacks' => [
        'onData' => function ($data) use ($sizes, &$i) {
            if ($data === null) {
                hardAssert($i == 4, 'Empty frame indicates streaming finish');
                return;
            }
            hardAssert($i < 4, 'Too many responses');
            $payload = $data->getPayload();
            hardAssert(
                $payload->getType() === Grpc\Testing\PayloadType::COMPRESSABLE,
                'Payload ' . $i . ' had the wrong type'
            );
            hardAssert(
                strlen($payload->getBody()) === $sizes[$i],
                'Response ' . $i . ' had the wrong length'
            );
            $i += 1;
        },
        'onStatus' => function ($status) {
            hardAssertIfStatusOk($status);
        },
    ]]);

    return 14;
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

    $writeRequest = function ($call, $i) use ($request_lengths, $response_lengths) {
        $request = new Grpc\Testing\StreamingOutputCallRequest();
        $request->setResponseType(Grpc\Testing\PayloadType::COMPRESSABLE);
        $response_parameters = new Grpc\Testing\ResponseParameters();
        $response_parameters->setSize($response_lengths[$i]);
        $request->getResponseParameters()[] = $response_parameters;
        $payload = new Grpc\Testing\Payload();
        $payload->setBody(str_repeat("\0", $request_lengths[$i]));
        $request->setPayload($payload);

        $call->write($request);
    };

    $i = 0;
    $call = $stub->FullDuplexCall(
        []/* metadata */,
        ['async_callbacks' => [
            'onData' => function ($response) use (
                $writeRequest,
                $request_lengths,
                $response_lengths,
                &$i,
                &$call
            ) {
                if ($i == 4) {
                    hardAssert($response === null, 'Server returned too many responses');
                } else {
                    hardAssert($response !== null, 'Server returned too few responses');
                    $payload = $response->getPayload();
                    hardAssert(
                        $payload->getType() === Grpc\Testing\PayloadType::COMPRESSABLE,
                        'Payload ' . $i . ' had the wrong type'
                    );
                    hardAssert(
                        strlen($payload->getBody()) === $response_lengths[$i],
                        'Payload ' . $i . ' had the wrong length'
                    );
                    $i += 1;
                    if ($i == 4) {
                        $call->writesDone();
                    } else {
                        $writeRequest($call, $i);
                    }
                }
            },
            'onStatus' => function ($status) {
                hardAssertIfStatusOk($status);
            },
        ]]
    );

    // write first request
    $writeRequest($call, 0);

    return 14;
}

/**
 * Run the empty_stream test.
 *
 * @param $stub Stub object that has service methods.
 */
function emptyStream($stub)
{
    $call = $stub->FullDuplexCall([]/* metadata */, [
        'async_callbacks' => [
            'onData' => function ($data) {
                hardAssert($data === null, 'Server returned too many responses');
            },
            'onStatus' => function ($status) {
                hardAssertIfStatusOk($status);
            },
        ],
    ]);
    $call->writesDone();

    return 2;
}

/**
 * Run the cancel_after_begin test.
 *
 * @param $stub Stub object that has service methods.
 */
function cancelAfterBegin($stub)
{
    $call = $stub->StreamingInputCall([]/* metadata */, [
        'async_callbacks' => [
            'onData' => function ($data) {
            },
            'onStatus' => function ($status) {
                hardAssert(
                    $status->code === Grpc\STATUS_CANCELLED,
                    'Call status was not CANCELLED'
                );
            },
        ],
    ]);
    $call->cancel();

    return 1;
}

/**
 * Run the cancel_after_first_response test.
 *
 * @param $stub Stub object that has service methods.
 */
function cancelAfterFirstResponse($stub)
{
    $call = $stub->FullDuplexCall([]/* metadata */, [
        'async_callbacks' => [
            'onData' => function ($data) {
            },
            'onStatus' => function ($status) {
                hardAssert(
                    $status->code === Grpc\STATUS_CANCELLED,
                    'Call status was not CANCELLED'
                );
            },
        ],
    ]);
    $request = new Grpc\Testing\StreamingOutputCallRequest();
    $request->setResponseType(Grpc\Testing\PayloadType::COMPRESSABLE);
    $response_parameters = new Grpc\Testing\ResponseParameters();
    $response_parameters->setSize(31415);
    $request->getResponseParameters()[] = $response_parameters;
    $payload = new Grpc\Testing\Payload();
    $payload->setBody(str_repeat("\0", 27182));
    $request->setPayload($payload);

    $call->write($request);
    $call->cancel();

    return 1;
}

function timeoutOnSleepingServer($stub)
{
    $call = $stub->FullDuplexCall([], [
        'timeout' => 1000,
        'async_callbacks' => [
            'onData' => function ($data) {
            },
            'onStatus' => function ($status) {
                hardAssert(
                    $status->code === Grpc\STATUS_DEADLINE_EXCEEDED,
                    'Call status was not DEADLINE_EXCEEDED'
                );
            },
        ],
    ]);
    $request = new Grpc\Testing\StreamingOutputCallRequest();
    $request->setResponseType(Grpc\Testing\PayloadType::COMPRESSABLE);
    $response_parameters = new Grpc\Testing\ResponseParameters();
    $response_parameters->setSize(8);
    $request->getResponseParameters()[] = $response_parameters;
    $payload = new Grpc\Testing\Payload();
    $payload->setBody(str_repeat("\0", 9));
    $request->setPayload($payload);

    $call->write($request);

    return 1;
}

function customMetadata($stub)
{
    $ECHO_INITIAL_KEY = 'x-grpc-test-echo-initial';
    $ECHO_INITIAL_VALUE = 'test_initial_metadata_value';
    $ECHO_TRAILING_KEY = 'x-grpc-test-echo-trailing-bin';
    $ECHO_TRAILING_VALUE = 'ababab';
    $request_len = 271828;
    $response_len = 314159;

    $request = new Grpc\Testing\SimpleRequest();
    $request->setResponseType(Grpc\Testing\PayloadType::COMPRESSABLE);
    $request->setResponseSize($response_len);
    $payload = new Grpc\Testing\Payload();
    $payload->setType(Grpc\Testing\PayloadType::COMPRESSABLE);
    $payload->setBody(str_repeat("\0", $request_len));
    $request->setPayload($payload);

    $metadata = [
        $ECHO_INITIAL_KEY => [$ECHO_INITIAL_VALUE],
        $ECHO_TRAILING_KEY => [$ECHO_TRAILING_VALUE],
    ];
    $stub->UnaryCall($request, $metadata, ["async_callbacks" => [
        "onMetadata" => function ($initial_metadata) use (
            $ECHO_INITIAL_KEY,
            $ECHO_INITIAL_VALUE
        ) {
            hardAssert(
                array_key_exists($ECHO_INITIAL_KEY, $initial_metadata),
                'Initial metadata does not contain expected key'
            );
            hardAssert(
                $initial_metadata[$ECHO_INITIAL_KEY][0] === $ECHO_INITIAL_VALUE,
                'Incorrect initial metadata value'
            );
        },
        "onData" => function ($response) {
        },
        "onStatus" => function ($status)  use (
            $ECHO_TRAILING_KEY,
            $ECHO_TRAILING_VALUE
        ) {
            hardAssertIfStatusOk($status);
            $trailing_metadata = $status->metadata;
            hardAssert(
                array_key_exists($ECHO_TRAILING_KEY, $trailing_metadata),
                'Trailing metadata does not contain expected key'
            );
            hardAssert(
                $trailing_metadata[$ECHO_TRAILING_KEY][0] === $ECHO_TRAILING_VALUE,
                'Incorrect trailing metadata value'
            );
        },
    ]]);


    $streaming_call = $stub->FullDuplexCall($metadata, ["async_callbacks" => [
        "onMetadata" => function ($streaming_initial_metadata) use (
            $ECHO_INITIAL_KEY,
            $ECHO_INITIAL_VALUE
        ) {
            hardAssert(
                array_key_exists($ECHO_INITIAL_KEY, $streaming_initial_metadata),
                'Initial metadata does not contain expected key'
            );
            hardAssert(
                $streaming_initial_metadata[$ECHO_INITIAL_KEY][0] === $ECHO_INITIAL_VALUE,
                'Incorrect initial metadata value'
            );
        },
        "onData" => function ($response) {
        },
        "onStatus" => function ($status)  use (
            $ECHO_TRAILING_KEY,
            $ECHO_TRAILING_VALUE
        ) {
            hardAssertIfStatusOk($status);
            $streaming_trailing_metadata = $status->metadata;
            hardAssert(
                array_key_exists(
                    $ECHO_TRAILING_KEY,
                    $streaming_trailing_metadata
                ),
                'Trailing metadata does not contain expected key'
            );
            hardAssert($streaming_trailing_metadata[$ECHO_TRAILING_KEY][0] ===
                $ECHO_TRAILING_VALUE, 'Incorrect trailing metadata value');
        },
    ]]);

    $streaming_request = new Grpc\Testing\StreamingOutputCallRequest();
    $streaming_request->setPayload($payload);
    $response_parameters = new Grpc\Testing\ResponseParameters();
    $response_parameters->setSize($response_len);
    $streaming_request->getResponseParameters()[] = $response_parameters;
    $streaming_call->write($streaming_request);
    $streaming_call->writesDone();

    return 10;
}

function statusCodeAndMessage($stub)
{
    $echo_status = new Grpc\Testing\EchoStatus();
    $echo_status->setCode(2);
    $echo_status->setMessage('test status message');

    $request = new Grpc\Testing\SimpleRequest();
    $request->setResponseStatus($echo_status);

    $stub->UnaryCall($request, [] /* metadata */, ["async_callbacks" => [
        "onData" => function ($result) {
        },
        "onStatus" => function ($status) {
            hardAssert(
                $status->code === 2,
                'Received unexpected UnaryCall status code: ' .
                    $status->code
            );
            hardAssert(
                $status->details === 'test status message',
                'Received unexpected UnaryCall status details: ' .
                    $status->details
            );
        },
    ]]);


    $streaming_call = $stub->FullDuplexCall([] /* metadata */, ["async_callbacks" => [
        "onData" => function ($result) {
        },
        "onStatus" => function ($status) {
            hardAssert(
                $status->code === 2,
                'Received unexpected FullDuplexCall status code: ' .
                    $status->code
            );
            hardAssert(
                $status->details === 'test status message',
                'Received unexpected FullDuplexCall status details: ' .
                    $status->details
            );
        },
    ]]);

    $streaming_request = new Grpc\Testing\StreamingOutputCallRequest();
    $streaming_request->setResponseStatus($echo_status);
    $streaming_call->write($streaming_request);
    $streaming_call->writesDone();

    return 4;
}

function specialStatusMessage($stub)
{
    $test_code = Grpc\STATUS_UNKNOWN;
    $test_msg = "\t\ntest with whitespace\r\nand Unicode BMP ☺ and non-BMP 😈\t\n";

    $echo_status = new Grpc\Testing\EchoStatus();
    $echo_status->setCode($test_code);
    $echo_status->setMessage($test_msg);

    $request = new Grpc\Testing\SimpleRequest();
    $request->setResponseStatus($echo_status);

    $stub->UnaryCall($request, [] /* metadata */, ["async_callbacks" => [
        "onData" => function ($result) {
        },
        "onStatus" => function ($status) use ($test_code, $test_msg) {
            hardAssert(
                $status->code === $test_code,
                'Received unexpected UnaryCall status code: ' . $status->code
            );
            hardAssert(
                $status->details === $test_msg,
                'Received unexpected UnaryCall status details: ' . $status->details
            );
        },
    ]]);

    return 2;
}

# NOTE: the stub input to this function is from UnimplementedService
function unimplementedService($stub)
{
    $stub->UnimplementedCall(new Grpc\Testing\EmptyMessage(), [] /* metadata */, ["async_callbacks" => [
        "onData" => function ($result) {
        },
        "onStatus" => function ($status) {
            hardAssert(
                $status->code === Grpc\STATUS_UNIMPLEMENTED,
                'Received unexpected status code'
            );
        },
    ]]);

    return 1;
}

# NOTE: the stub input to this function is from TestService
function unimplementedMethod($stub)
{
    $stub->UnimplementedCall(new Grpc\Testing\EmptyMessage(), [] /* metadata */, ["async_callbacks" => [
        "onData" => function ($result) {
        },
        "onStatus" => function ($status) {
            hardAssert(
                $status->code === Grpc\STATUS_UNIMPLEMENTED,
                'Received unexpected status code'
            );
        },
    ]]);

    return 1;
}

function _makeStub($args)
{
    if (!array_key_exists('server_host', $args)) {
        throw new Exception('Missing argument: --server_host is required');
    }
    if (!array_key_exists('server_port', $args)) {
        throw new Exception('Missing argument: --server_port is required');
    }
    if (!array_key_exists('test_case', $args)) {
        throw new Exception('Missing argument: --test_case is required');
    }

    $server_address = $args['server_host'] . ':' . $args['server_port'];
    $test_case = $args['test_case'];

    $host_override = '';
    if (array_key_exists('server_host_override', $args)) {
        $host_override = $args['server_host_override'];
    }

    $use_tls = false;
    if (
        array_key_exists('use_tls', $args) &&
        $args['use_tls'] != 'false'
    ) {
        $use_tls = true;
    }

    $use_test_ca = false;
    if (
        array_key_exists('use_test_ca', $args) &&
        $args['use_test_ca'] != 'false'
    ) {
        $use_test_ca = true;
    }

    $opts = [];

    if ($use_tls) {
        if ($use_test_ca) {
            $ssl_credentials = Grpc\ChannelCredentials::createSsl(
                file_get_contents(dirname(__FILE__) . '/../data/ca.pem')
            );
        } else {
            $ssl_credentials = Grpc\ChannelCredentials::createSsl();
        }
        $opts['credentials'] = $ssl_credentials;
        if (!empty($host_override)) {
            $opts['grpc.ssl_target_name_override'] = $host_override;
        }
    } else {
        $opts['credentials'] = Grpc\ChannelCredentials::createInsecure();
    }

    if (in_array($test_case, [
        'service_account_creds',
        'compute_engine_creds', 'jwt_token_creds',
    ])) {
        if ($test_case === 'jwt_token_creds') {
            $auth_credentials = ApplicationDefaultCredentials::getCredentials();
        } else {
            $auth_credentials = ApplicationDefaultCredentials::getCredentials(
                $args['oauth_scope']
            );
        }
        $opts['update_metadata'] = $auth_credentials->getUpdateMetadataFunc();
    }

    if ($test_case === 'oauth2_auth_token') {
        $auth_credentials = ApplicationDefaultCredentials::getCredentials(
            $args['oauth_scope']
        );
        $token = $auth_credentials->fetchAuthToken();
        $update_metadata =
            function (
                $metadata,
                $authUri = null,
                ClientInterface $client = null
            ) use ($token) {
                $metadata_copy = $metadata;
                $metadata_copy[CredentialsLoader::AUTH_METADATA_KEY] =
                    [sprintf(
                        '%s %s',
                        $token['token_type'],
                        $token['access_token']
                    )];

                return $metadata_copy;
            };
        $opts['update_metadata'] = $update_metadata;
    }

    $channel = new Grpc\Channel($server_address, $opts);
    $intercept_channel = Grpc\Interceptor::intercept($channel, new RecordCallCountInterceptor());

    if ($test_case === 'unimplemented_service') {
        $stub = new Grpc\Testing\UnimplementedServiceClient(
            $server_address,
            $opts,
            $intercept_channel
        );
    } else {
        $stub = new Grpc\Testing\TestServiceClient($server_address, $opts, $intercept_channel);
    }

    return $stub;
}

class RecordCallCountInterceptor extends Grpc\Interceptor
{
    private function setCallCountHook(array $options)
    {
        global $call_count;
        $call_count++;

        if ($options["async_callbacks"]) {
            $onStatus = $options["async_callbacks"]["onStatus"];
            if ($onStatus) {
                $options["async_callbacks"]["onStatus"] = function ($status) use ($onStatus) {
                    $onStatus($status);
                    global $completed_count;
                    $completed_count++;
                };
            } else {
                $options["async_callbacks"]["onStatus"] = function ($status) {
                    global $completed_count;
                    $completed_count++;
                };
            }
        }
        return $options;
    }

    public function interceptUnaryUnary(
        $method,
        $argument,
        $deserialize,
        $continuation,
        array $metadata = [],
        array $options = []
    ) {
        $options = $this->setCallCountHook($options);
        return $continuation($method, $argument, $deserialize, $metadata, $options);
    }
    public function interceptStreamUnary(
        $method,
        $deserialize,
        $continuation,
        array $metadata = [],
        array $options = []
    ) {
        $options = $this->setCallCountHook($options);
        return $continuation($method, $deserialize, $metadata, $options);
    }
    public function interceptUnaryStream(
        $method,
        $argument,
        $deserialize,
        $continuation,
        array $metadata = [],
        array $options = []
    ) {
        $options = $this->setCallCountHook($options);
        return $continuation($method, $argument, $deserialize, $metadata, $options);
    }

    public function interceptStreamStream(
        $method,
        $deserialize,
        $continuation,
        array $metadata = [],
        array $options = []
    ) {
        $options = $this->setCallCountHook($options);
        return $continuation($method, $deserialize, $metadata, $options);
    }
}

function waitForCallCompleted()
{
    // wait for started async call to complete
    for ($i = 0; $i < 30 * 100 /* 30s timeout */; $i++) {
        drainCompletionEvents();
        global $completed_count;
        global $call_count;
        if ($completed_count == $call_count) {
            break;
        }
        usleep(10000);
    }
    if ($completed_count != $call_count) {
        echo "test timeout\n";
        exit(1);
    }
}

function interop_main($args, $stub = false)
{
    if (!$stub) {
        $stub = _makeStub($args);
    }

    $test_case = $args['test_case'];
    echo "Running test case $test_case... ";
    $expected_assertions = 0;

    switch ($test_case) {
        case 'empty_unary':
            $expected_assertions = emptyUnary($stub);
            break;
        case 'large_unary':
            $expected_assertions = largeUnary($stub);
            break;
        case 'client_streaming':
            $expected_assertions = clientStreaming($stub);
            break;
        case 'server_streaming':
            $expected_assertions = serverStreaming($stub);
            break;
        case 'ping_pong':
            $expected_assertions = pingPong($stub);
            break;
        case 'empty_stream':
            $expected_assertions = emptyStream($stub);
            break;
        case 'cancel_after_begin':
            $expected_assertions = cancelAfterBegin($stub);
            break;
        case 'cancel_after_first_response':
            $expected_assertions = cancelAfterFirstResponse($stub);
            break;
        case 'timeout_on_sleeping_server':
            $expected_assertions = timeoutOnSleepingServer($stub);
            break;
        case 'custom_metadata':
            $expected_assertions = customMetadata($stub);
            break;
        case 'status_code_and_message':
            $expected_assertions = statusCodeAndMessage($stub);
            break;
        case 'special_status_message':
            $expected_assertions = specialStatusMessage($stub);
            break;
        case 'unimplemented_service':
            $expected_assertions = unimplementedService($stub);
            break;
        case 'unimplemented_method':
            $expected_assertions = unimplementedMethod($stub);
            break;
        case 'service_account_creds':
            $expected_assertions = serviceAccountCreds($stub, $args);
            break;
        case 'compute_engine_creds':
            $expected_assertions = computeEngineCreds($stub, $args);
            break;
        case 'jwt_token_creds':
            $expected_assertions = jwtTokenCreds($stub, $args);
            break;
        case 'oauth2_auth_token':
            $expected_assertions = oauth2AuthToken($stub, $args);
            break;
        case 'per_rpc_creds':
            $expected_assertions = perRpcCreds($stub, $args);
            break;
        case 'client_compressed_unary':
            $expected_assertions = clientCompressedUnary($stub);
            break;
        case 'client_compressed_streaming':
            $expected_assertions = clientCompressedStreaming($stub);
            break;
        default:
            echo "Unsupported test case $test_case\n";
            exit(1);
    }
    waitForCallCompleted();

    global $assertions;
    echo $assertions . " assersions\n";
    if ($assertions == 0 || $assertions != $expected_assertions) {
        echo "actual assertions: " . $assertions .
            " != expected assertions: " . $expected_assertions . "\n";
        exit(1);
    }

    return $stub;
}


if (
    isset($_SERVER['PHP_SELF']) &&
    preg_match('/interop_client/', $_SERVER['PHP_SELF'])
) {
    $args = getopt('', [
        'server_host:', 'server_port:', 'test_case:',
        'use_tls::', 'use_test_ca::',
        'server_host_override:', 'oauth_scope:',
        'default_service_account:',
    ]);
    interop_main($args);
}
