<?php
class SecureEndToEndTest extends PHPUnit_Framework_TestCase{
  public function setUp() {
    $this->client_queue = new Grpc\CompletionQueue();
    $this->server_queue = new Grpc\CompletionQueue();
    $credentials = Grpc\Credentials::createSsl(
        file_get_contents(dirname(__FILE__) . '/../data/ca.pem'));
    $server_credentials = Grpc\ServerCredentials::createSsl(
        null,
        file_get_contents(dirname(__FILE__) . '/../data/server1.key'),
        file_get_contents(dirname(__FILE__) . '/../data/server1.pem'));
    $this->server = new Grpc\Server($this->server_queue,
                                    ['credentials' => $server_credentials]);
    $address = '127.0.0.1:' . getNewPort();
    $this->server->add_secure_http2_port($address);
    $this->channel = new Grpc\Channel(
        $address,
        [
            'grpc.ssl_target_name_override' => 'foo.test.google.com',
            'credentials' => $credentials
         ]);
  }

  public function tearDown() {
    unset($this->channel);
    unset($this->server);
    unset($this->client_queue);
    unset($this->server_queue);
  }

  public function testSimpleRequestBody() {
    $this->server->start();
    $deadline = Grpc\Timeval::inf_future();
    $status_text = 'xyz';
    $call = new Grpc\Call($this->channel,
                          'dummy_method',
                          $deadline);
    $tag = 1;
    $this->assertEquals(Grpc\CALL_OK,
                        $call->invoke($this->client_queue,
                                      $tag,
                                      $tag));
    $server_tag = 2;

    $call->writes_done($tag);
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertEquals(Grpc\OP_OK, $event->data);

    // check that a server rpc new was received
    $this->server->request_call($server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\SERVER_RPC_NEW, $event->type);
    $server_call = $event->call;
    $this->assertNotNull($server_call);
    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->server_accept($this->server_queue,
                                                    $server_tag));

    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->server_end_initial_metadata());

    // the server sends the status
    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->start_write_status(Grpc\STATUS_OK,
                                                         $status_text,
                                                         $server_tag));
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertEquals(Grpc\OP_OK, $event->data);

    // the client gets CLIENT_METADATA_READ
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\CLIENT_METADATA_READ, $event->type);

    // the client gets FINISHED
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISHED, $event->type);
    $status = $event->data;
    $this->assertEquals(Grpc\STATUS_OK, $status->code);
    $this->assertEquals($status_text, $status->details);

    // and the server gets FINISHED
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISHED, $event->type);
    $status = $event->data;

    unset($call);
    unset($server_call);
  }

  public function testClientServerFullRequestResponse() {
    $this->server->start();
    $deadline = Grpc\Timeval::inf_future();
    $req_text = 'client_server_full_request_response';
    $reply_text = 'reply:client_server_full_request_response';
    $status_text = 'status:client_server_full_response_text';

    $call = new Grpc\Call($this->channel,
                          'dummy_method',
                          $deadline);
    $tag = 1;
    $this->assertEquals(Grpc\CALL_OK,
                        $call->invoke($this->client_queue,
                                      $tag,
                                      $tag));

    $server_tag = 2;

    // the client writes
    $call->start_write($req_text, $tag);
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\WRITE_ACCEPTED, $event->type);

    // check that a server rpc new was received
    $this->server->request_call($server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\SERVER_RPC_NEW, $event->type);
    $server_call = $event->call;
    $this->assertNotNull($server_call);
    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->server_accept($this->server_queue,
                                                    $server_tag));

    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->server_end_initial_metadata());

    // start the server read
    $server_call->start_read($server_tag);
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\READ, $event->type);
    $this->assertEquals($req_text, $event->data);

    // the server replies
    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->start_write($reply_text, $server_tag));
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\WRITE_ACCEPTED, $event->type);

    // the client reads the metadata
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\CLIENT_METADATA_READ, $event->type);

    // the client reads the reply
    $call->start_read($tag);
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\READ, $event->type);
    $this->assertEquals($reply_text, $event->data);

    // the client sends writes done
    $call->writes_done($tag);
    $event = $this->client_queue->next($deadline);
    $this->assertEquals(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertEquals(Grpc\OP_OK, $event->data);

    // the server sends the status
    $this->assertEquals(Grpc\CALL_OK,
                        $server_call->start_write_status(GRPC\STATUS_OK,
                                                         $status_text,
                                                         $server_tag));
    $event = $this->server_queue->next($deadline);
    $this->assertEquals(Grpc\FINISH_ACCEPTED, $event->type);
    $this->assertEquals(Grpc\OP_OK, $event->data);

    // the client gets FINISHED
    $event = $this->client_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISHED, $event->type);
    $status = $event->data;
    $this->assertEquals(Grpc\STATUS_OK, $status->code);
    $this->assertEquals($status_text, $status->details);

    // and the server gets FINISHED
    $event = $this->server_queue->next($deadline);
    $this->assertNotNull($event);
    $this->assertEquals(Grpc\FINISHED, $event->type);

    unset($call);
    unset($server_call);
  }
}