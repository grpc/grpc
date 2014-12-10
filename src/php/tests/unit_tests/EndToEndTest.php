<?php
require __DIR__ . '/../util/port_picker.php';
class EndToEndTest extends PHPUnit_Framework_TestCase{
  public function setUp() {
    $this->client_queue = new Grpc\CompletionQueue();
    $this->server_queue = new Grpc\CompletionQueue();
    $this->server = new Grpc\Server($this->server_queue, []);
    $address = '127.0.0.1:' . getNewPort();
    $this->server->add_http2_port($address);
    $this->channel = new Grpc\Channel($address, []);
  }

  public function tearDown() {
    unset($this->channel);
    unset($this->server);
    unset($this->client_queue);
    unset($this->server_queue);
  }

  public function testSimpleRequestBody() {
    $deadline = Grpc\Timeval::inf_future();
    $status_text = 'xyz';
    $call = new Grpc\Call($this->channel,
                          'dummy_method',
                          $deadline);
    $tag = 1;
    $this->assertEquals(Grpc\CALL_OK,
                        $call->start_invoke($this->client_queue,
                                            $tag,
                                            $tag,
                                            $tag));

    $server_tag = 2;

    // the client invocation was accepted
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\INVOKE_ACCEPTED, $event->get_type());

    $this->assertEquals(Grpc\CALL_OK, $call->writes_done($tag));
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISH_ACCEPTED, $event->get_type());
    $this->assertEquals(Grpc\OP_OK, $event->get_data());

    // check that a server rpc new was received
    $this->server->start();
    $this->assertEquals(Grpc\CALL_OK, $this->server->request_call($server_tag));
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\SERVER_RPC_NEW, $event->get_type());
    $server_call = $event->get_call();
    $this->assertNotNull($server_call);
    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->accept($this->server_queue, $server_tag));

    // the server sends the status
    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->start_write_status(Grpc\STATUS_OK,
                                                         $status_text,
                                                         $server_tag));
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISH_ACCEPTED, $event->get_type());
    $this->assertEquals(Grpc\OP_OK, $event->get_data());

    // the client gets CLIENT_METADATA_READ
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\CLIENT_METADATA_READ, $event->get_type());

    // the client gets FINISHED
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISHED, $event->get_type());
    $status = $event->get_data();
    $this->assertEquals(Grpc\STATUS_OK, $status->code);
    $this->assertEquals($status_text, $status->details);

    // and the server gets FINISHED
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISHED, $event->get_type());
    $status = $event->get_data();

    unset($call);
    unset($server_call);
  }

  public function testClientServerFullRequestResponse() {
    $deadline = Grpc\Timeval::inf_future();
    $req_text = 'client_server_full_request_response';
    $reply_text = 'reply:client_server_full_request_response';
    $status_text = 'status:client_server_full_response_text';

    $call = new Grpc\Call($this->channel,
                          'dummy_method',
                          $deadline);
    $tag = 1;
    $this->assertEquals(Grpc\CALL_OK,
                        $call->start_invoke($this->client_queue,
                                            $tag,
                                            $tag,
                                            $tag));

    $server_tag = 2;

    // the client invocation was accepted
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\INVOKE_ACCEPTED, $event->get_type());

    // the client writes
    $this->assertEquals(Grpc\CALL_OK, $call->start_write($req_text, $tag));
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\WRITE_ACCEPTED, $event->get_type());

    // check that a server rpc new was received
    $this->server->start();
    $this->assertEquals(Grpc\CALL_OK, $this->server->request_call($server_tag));
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\SERVER_RPC_NEW, $event->get_type());
    $server_call = $event->get_call();
    $this->assertNotNull($server_call);
    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->accept($this->server_queue, $server_tag));

    // start the server read
    $this->assertEquals(Grpc\CALL_OK, $server_call->start_read($server_tag));
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\READ, $event->get_type());
    $this->assertEquals($req_text, $event->get_data());

    // the server replies
    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->start_write($reply_text, $server_tag));
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\WRITE_ACCEPTED, $event->get_type());

    // the client reads the metadata
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\CLIENT_METADATA_READ, $event->get_type());

    // the client reads the reply
    $this->assertEquals(Grpc\CALL_OK, $call->start_read($tag));
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\READ, $event->get_type());
    $this->assertEquals($reply_text, $event->get_data());

    // the client sends writes done
    $this->assertEquals(Grpc\CALL_OK, $call->writes_done($tag));
    $event = $this->client_queue->next($deadline);
    $this->assertEquals(Grpc\FINISH_ACCEPTED, $event->get_type());
    $this->assertEquals(Grpc\OP_OK, $event->get_data());

    // the server sends the status
    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->start_write_status(GRPC\STATUS_OK,
                                                         $status_text,
                                                         $server_tag));
    $event = $this->server_queue->next($deadline);
    $this->assertEquals(Grpc\FINISH_ACCEPTED, $event->get_type());
    $this->assertEquals(Grpc\OP_OK, $event->get_data());

    // the client gets FINISHED
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISHED, $event->get_type());
    $status = $event->get_data();
    $this->assertEquals(Grpc\STATUS_OK, $status->code);
    $this->assertEquals($status_text, $status->details);

    // and the server gets FINISHED
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISHED, $event->get_type());

    unset($call);
    unset($server_call);
  }
}