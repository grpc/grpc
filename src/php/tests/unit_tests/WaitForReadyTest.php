<?php

require_once(dirname(__FILE__).'/../../lib/Grpc/BaseStub.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/AbstractCall.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/UnaryCall.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/ClientStreamingCall.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/ServerStreamingCall.php');
require_once(dirname(__FILE__).'/../../lib/Grpc/Interceptor.php');

define("INITIAL_METADATA_WAIT_FOR_READY", 0x20);
define("INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET", 0x40);
define("INITIAL_METADATA_USED_MASK", 0x80);


class CallClient extends Grpc\BaseStub
{

  /**
   * @param string $hostname hostname
   * @param array $opts channel options
   * @param Channel|InterceptorChannel $channel (optional) re-use channel object
   */
  public function __construct($hostname, $opts, $channel = null)
  {
    parent::__construct($hostname, $opts, $channel);
  }

  /**
   * A simple RPC.
   * @param SimpleRequest $argument input argument
   * @param array $metadata metadata
   * @param array $options call options
   */
  public function UnaryUnaryCall(
    CallInvokerSimpleRequest $argument,
    $metadata = [],
    $options = [],
    $wait_for_ready = null
  ) {
    return $this->_simpleRequest(
      '/dummy_method',
      $argument,
      [],
      $metadata,
      $options,
      $wait_for_ready
    );
  }

  public function UnaryStreamCall(
  	CallInvokerSimpleRequest $argument,
  	$metadata = [],
  	$options = [],
  	$wait_for_ready = null
  ) {
  	return $this->_serverStreamRequest(
  		'/dummy_method',
  		$argument,
  		[],
  		$metadata,
  		$options,
  		$wait_for_ready
  	);
  }

  public function StreamUnaryCall(
  	$metadata = [],
  	$options = [],
  	$wait_for_ready = null
  ) {
  	return $this->_clientStreamRequest(
  		'/dummy_method',
  		[],
  		$metadata,
  		$options,
  		$wait_for_ready
  	);
  }
}

class WaitForReadyTest extends PHPUnit_Framework_TestCase
{
	public function setup()
	{
        $this->server = new Grpc\Server([]);
        $this->port = $this->server->addHttp2Port('0.0.0.0:0');
        $this->server->start();
	}

	public function teardown()
	{
        unset($this->server);
	}

	public function testSetWaitForReady()
	{
        $req_text = 'client_request';
        $client = new CallClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ]);

        $req = new CallInvokerSimpleRequest($req_text);
        $call = $client->UnaryUnaryCall($req);

        $call->SetWaitForReady(true);
        $flag = $call->GetWaitForReady();
        $flag_true = INITIAL_METADATA_WAIT_FOR_READY & INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
        $this->assertEquals($flag, $flag_true);

        $call->SetWaitForReady(false);
        $flag = $call->GetWaitForReady();
        $flag_false = ~INITIAL_METADATA_WAIT_FOR_READY & INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
        $this->assertEquals($flag, $flag_false);

        unset($call);
        $client->close();
	}

	public function testIsWaitForReady()
	{
		$req_text = 'client_request';
        $client = new CallClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ]);

        $req = new CallInvokerSimpleRequest($req_text);
        $call = $client->UnaryUnaryCall($req);

        $call->SetWaitForReady(true);
        $this->assertTrue($call->IsWaitForReady());

        $call->SetWaitForReady(false);
        $this->assertFalse($call->IsWaitForReady());

        unset($call);
        $client->close();
	}

	public function testUnaryUnaryCallDefault()
	{
		$req_text = 'client_request';
        $client = new CallClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ]);

        $req = new CallInvokerSimpleRequest($req_text);
        $call = $client->UnaryUnaryCall($req,[],[],true);

        $this->assertTrue($call->IsWaitForReady());

        $call = $client->UnaryUnaryCall($req,[],[],false);

        $this->assertFalse($call->IsWaitForReady());

        unset($call);
        $client->close();
	}

	public function testUnaryStreamCallDefault()
	{
		$req_text = 'client_request';
        $client = new CallClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ]);

        $req = new CallInvokerSimpleRequest($req_text);
        $call = $client->UnaryStreamCall($req,[],[],true);

        $this->assertTrue($call->IsWaitForReady());

        $call = $client->UnaryStreamCall($req,[],[],false);

        $this->assertFalse($call->IsWaitForReady());

        unset($call);
        $client->close();
	}

	public function testStreamUnaryCallDefault()
	{
        $client = new CallClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ]);

        $call = $client->StreamUnaryCall([],[],true);

        $this->assertTrue($call->IsWaitForReady());

        $call = $client->StreamUnaryCall([],[],false);

        $this->assertFalse($call->IsWaitForReady());

        unset($call);
        $client->close();
	}

	public function testMultiCallsDefault()
	{
		$req_text = 'client_request';
        $client = new CallClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ]);

        $req = new CallInvokerSimpleRequest($req_text);
        $unaryunary_call = $client->UnaryUnaryCall($req,[],[],false);
        $unarystream_call = $client->UnaryStreamCall($req,[],[],true);
        $streamunary_call = $client->StreamUnaryCall([],[],false);

        $this->assertFalse($unaryunary_call->IsWaitForReady());
        $this->assertTrue($unarystream_call->IsWaitForReady());
        $this->assertFalse($streamunary_call->IsWaitForReady());

        unset($unaryunary_call);
        unset($unarystream_call);
        unset($streamunary_call);
        $client->close();
	}

	public function testMultiCallsWithSet()
	{
		$req_text = 'client_request';
        $client = new CallClient('localhost:'.$this->port, [
            'force_new' => true,
            'credentials' => Grpc\ChannelCredentials::createInsecure(),
        ]);

        $req = new CallInvokerSimpleRequest($req_text);
        $unaryunary_call = $client->UnaryUnaryCall($req);
        $unarystream_call = $client->UnaryStreamCall($req);
        $streamunary_call = $client->StreamUnaryCall();

        $unaryunary_call->SetWaitForReady(true);
        $unarystream_call->SetWaitForReady(false);
        $streamunary_call->SetWaitForReady(true);

        $this->assertTrue($unaryunary_call->IsWaitForReady());
        $this->assertFalse($unarystream_call->IsWaitForReady());
        $this->assertTrue($streamunary_call->IsWaitForReady());

        unset($unaryunary_call);
        unset($unarystream_call);
        unset($streamunary_call);
        $client->close();
	}
}