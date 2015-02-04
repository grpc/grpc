<?php
class EndToEndTest extends PHPUnit_Framework_TestCase{
  public function setUp() {
    $this->client_queue = new Grpc\CompletionQueue();
    $this->server_queue = new Grpc\CompletionQueue();
    $this->server = new Grpc\Server($this->server_queue, []);
    $port = $this->server->add_http2_port('0.0.0.0:0');
    $this->channel = new Grpc\Channel('localhost:' . $port, []);
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
    $call->invoke($this->client_queue, $tag, $tag);
    $server_tag = 2;

    $call->writes_done($tag);
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertSame(Grpc\OP_OK, $event->data);

    // check that a server rpc new was received
    $this->server->start();
    $this->server->request_call($server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\SERVER_RPC_NEW, $event->type);
    $server_call = $event->call;
    $this->assertNotNull($server_call);
    $server_call->server_accept($this->server_queue, $server_tag);

    $server_call->server_end_initial_metadata();


    // the server sends the status
    $server_call->start_write_status(Grpc\STATUS_OK, $status_text, $server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertSame(Grpc\OP_OK, $event->data);

    // the client gets CLIENT_METADATA_READ
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\CLIENT_METADATA_READ, $event->type);

    // the client gets FINISHED
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISHED, $event->type);
    $status = $event->data;
    $this->assertSame(Grpc\STATUS_OK, $status->code);
    $this->assertSame($status_text, $status->details);

    // and the server gets FINISHED
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISHED, $event->type);
    $status = $event->data;

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
    $call->invoke($this->client_queue, $tag, $tag);

    $server_tag = 2;

    // the client writes
    $call->start_write($req_text, $tag);
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\WRITE_ACCEPTED, $event->type);

    // check that a server rpc new was received
    $this->server->start();
    $this->server->request_call($server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\SERVER_RPC_NEW, $event->type);
    $server_call = $event->call;
    $this->assertNotNull($server_call);
    $server_call->server_accept($this->server_queue, $server_tag);

    $server_call->server_end_initial_metadata();

    // start the server read
    $server_call->start_read($server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\READ, $event->type);
    $this->assertSame($req_text, $event->data);

    // the server replies
    $server_call->start_write($reply_text, $server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\WRITE_ACCEPTED, $event->type);

    // the client reads the metadata
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\CLIENT_METADATA_READ, $event->type);

    // the client reads the reply
    $call->start_read($tag);
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\READ, $event->type);
    $this->assertSame($reply_text, $event->data);

    // the client sends writes done
    $call->writes_done($tag);
    $event = $this->client_queue->next($deadline);
    $this->assertSame(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertSame(Grpc\OP_OK, $event->data);

    // the server sends the status
    $server_call->start_write_status(GRPC\STATUS_OK, $status_text, $server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertSame(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertSame(Grpc\OP_OK, $event->data);

    // the client gets FINISHED
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISHED, $event->type);
    $status = $event->data;
    $this->assertSame(Grpc\STATUS_OK, $status->code);
    $this->assertSame($status_text, $status->details);

    // and the server gets FINISHED
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertSame(Grpc\FINISHED, $event->type);

    unset($call);
    unset($server_call);
  }
}