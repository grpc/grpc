<?php
require_once realpath(dirname(__FILE__) . '/../../lib/autoload.php');
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
  list($result, $status) = $stub->EmptyCall(new proto2\EmptyMessage())->wait();
  hardAssert($status->code == Grpc\STATUS_OK, 'Call did not complete successfully');
  hardAssert($result != null, 'Call completed with a null response');
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
  hardAssert($status->code == Grpc\STATUS_OK, 'Call did not complete successfully');
  hardAssert($result != null, 'Call returned a null response');
  $payload = $result->getPayload();
  hardAssert($payload->getType() == grpc\testing\PayloadType::COMPRESSABLE,
         'Payload had the wrong type');
  hardAssert(strlen($payload->getBody()) == $response_len,
         'Payload had the wrong length');
  hardAssert($payload->getBody() == str_repeat("\0", $response_len),
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
  hardAssert($status->code == Grpc\STATUS_OK, 'Call did not complete successfully');
  hardAssert($result->getAggregatedPayloadSize() == 74922,
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
  hardAssert($call->getStatus()->code == Grpc\STATUS_OK,
              'Call did not complete successfully');
  $i = 0;
  foreach($call->responses() as $value) {
    hardAssert($i < 4, 'Too many responses');
    $payload = $value->getPayload();
    hardAssert($payload->getType() == grpc\testing\PayloadType::COMPRESSABLE,
                'Payload ' . $i . ' had the wrong type');
    hardAssert(strlen($payload->getBody()) == $sizes[$i],
                'Response ' . $i . ' had the wrong length');
  }
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

    hardAssert($response != null, 'Server returned too few responses');
    $payload = $response->getPayload();
    hardAssert($payload->getType() == grpc\testing\PayloadType::COMPRESSABLE,
                'Payload ' . $i . ' had the wrong type');
    hardAssert(strlen($payload->getBody()) == $response_lengths[$i],
                'Payload ' . $i . ' had the wrong length');
  }
  $call->writesDone();
  hardAssert($call->read() == null, 'Server returned too many responses');
  hardAssert($call->getStatus()->code == Grpc\STATUS_OK,
              'Call did not complete successfully');
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
    $server_address,
    [
        'grpc.ssl_target_name_override' => 'foo.test.google.com',
        'credentials' => $credentials
     ]);

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
}