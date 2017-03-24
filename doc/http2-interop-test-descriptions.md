Negative HTTP/2 Interop Test Case Descriptions
=======================================

Client and server use
[test.proto](../src/proto/grpc/testing/test.proto).

Server
------
The code for the custom http2 server can be found
[here](https://github.com/grpc/grpc/tree/master/test/http2_test).
It is responsible for handling requests and sending responses, and also for 
fulfilling the behavior of each particular test case.

Server should accept these arguments:
* --port=PORT
  * The port the server will run on. For example, "8080"
* --test_case=TESTCASE
  * The name of the test case to execute. For example, "goaway"

Client
------

Clients implement test cases that test certain functionality. Each client is
provided the test case it is expected to run as a command-line parameter. Names
should be lowercase and without spaces.

Clients should accept these arguments:
* --server_host=HOSTNAME
    * The server host to connect to. For example, "localhost" or "127.0.0.1"
* --server_port=PORT
    * The server port to connect to. For example, "8080"
* --test_case=TESTCASE
    * The name of the test case to execute. For example, "goaway"

Note
-----

Note that the server and client must be invoked with the same test case or else
the test will be meaningless. For convenience, we provide a shell script wrapper
that invokes both server and client at the same time, with the same test_case.
This is the preferred way to run these tests.

## Test Cases

### goaway

This test verifies that the client correctly responds to a goaway sent by the
server. The client should handle the goaway by switching to a new stream without
the user application having to do a thing.

Client Procedure:
 1. Client sends two UnaryCall requests (and sleeps for 1 second in-between).
 TODO: resolve [9300](https://github.com/grpc/grpc/issues/9300) and remove the 1 second sleep
 
    ```
    {
      response_size: 314159
      payload:{
        body: 271828 bytes of zeros
      }
    }
    ```

Client asserts:
* Both calls are successful.
* Response payload body is 314159 bytes in size.

Server Procedure:
  1. Server sends a GOAWAY after receiving the first UnaryCall.

Server asserts:
* Two different connections were used from the client.

### rst_after_header

This test verifies that the client fails correctly when the server sends a
RST_STREAM immediately after sending headers to the client.

Procedure:
 1. Client sends UnaryCall with:
 
    ```
    {
      response_size: 314159
      payload:{
        body: 271828 bytes of zeros
      }
    }
    ```

Client asserts:
* Call was not successful.

Server Procedure:
  1. Server sends a RST_STREAM with error code 0 after sending headers to the client.
  
*At the moment the error code and message returned are not standardized throughout all
languages. Those checks will be added once all client languages behave the same way. [#9142](https://github.com/grpc/grpc/issues/9142) is in flight.*

### rst_during_data

This test verifies that the client fails "correctly" when the server sends a
RST_STREAM halfway through sending data to the client.

Procedure:
 1. Client sends UnaryCall with:
 
    ```
    {
      response_size: 314159
      payload:{
        body: 271828 bytes of zeros
      }
    }
    ```

Client asserts:
* Call was not successful.

Server Procedure:
  1. Server sends a RST_STREAM with error code 0 after sending half of 
     the requested data to the client.

### rst_after_data

This test verifies that the client fails "correctly" when the server sends a
RST_STREAM after sending all of the data to the client.

Procedure:
 1. Client sends UnaryCall with:
 
    ```
    {
      response_size: 314159
      payload:{
        body: 271828 bytes of zeros
      }
    }
    ```

Client asserts:
* Call was not successful.

Server Procedure:
  1. Server sends a RST_STREAM with error code 0 after sending all of the
  data to the client.

*Certain client languages allow the data to be accessed even though a RST_STREAM
was encountered. Once all client languages behave this way, checks will be added on
the incoming data.*

### ping

This test verifies that the client correctly acknowledges all pings it gets from the
server.

Procedure:
 1. Client sends UnaryCall with:
 
    ```
    {
      response_size: 314159
      payload:{
        body: 271828 bytes of zeros
      }
    }
    ```
  
Client asserts:
* call was successful.
* response payload body is 314159 bytes in size.

Server Procedure:
  1. Server tracks the number of outstanding pings (i.e. +1 when it sends a ping, and -1 
  when it receives an ack from the client).
  2. Server sends pings before and after sending headers, also before and after sending data.
  
Server Asserts:
* Number of outstanding pings is 0 when the connection is lost.

### max_streams

This test verifies that the client observes the MAX_CONCURRENT_STREAMS limit set by the server.

Client Procedure:
  1. Client sends initial UnaryCall to allow the server to update its MAX_CONCURRENT_STREAMS settings.
  2. Client concurrently sends 10 UnaryCalls.
  
Client Asserts:
* All UnaryCalls were successful, and had the correct type and payload size.
 
Server Procedure:
  1. Sets MAX_CONCURRENT_STREAMS to one after the connection is made.

*The assertion that the MAX_CONCURRENT_STREAMS limit is upheld occurs in the http2 library we used.*

### data_frame_padding

This test verifies that the client can correctly receive padded http2 data
frames. It also stresses the client's flow control (there is a high chance
that the sender will deadlock if the client's flow control logic doesn't
correctly account for padding).

Client Procedure:
(Note this is the same procedure as in the "large_unary" gRPC interop tests.
Clients should use their "large_unary" gRPC interop test implementations.)
Procedure:
 1. Client calls UnaryCall with:

    ```
    {
      response_size: 314159
      payload:{
        body: 271828 bytes of zeros
      }
    }
    ```

Client asserts:
* call was successful
* response payload body is 314159 bytes in size
* clients are free to assert that the response payload body contents are zero
  and comparing the entire response message against a golden response

Server Procedure:
  1. Reply to the client's request with a `SimpleResponse`, with a payload
  body length of `SimpleRequest.response_size`. But send it across specific
  http2 data frames as follows:
    * Each http2 data frame contains a 5 byte payload and 255 bytes of padding.

  * Note the 5 byte payload and 255 byte padding are partly arbitrary,
  and other numbers are also ok. With 255 bytes of padding for each 5 bytes of
  payload containing actual gRPC message, the 300KB response size will
  multiply into around 15 megabytes of flow control debt, which should stress
  flow control accounting.

### no_df_padding_sanity_test

This test verifies that the client can correctly receive a series of small
data frames. Note that this test is intentionally a slight variation of
"data_frame_padding", with the only difference being that this test doesn't use data
frame padding when the response is sent. This test is primarily meant to
prove correctness of the http2 server implementation and highlight failures
of the "data_frame_padding" test.

Client Procedure:
(Note this is the same procedure as in the "large_unary" gRPC interop tests.
Clients should use their "large_unary" gRPC interop test implementations.)
Procedure:
 1. Client calls UnaryCall with:

    ```
    {
      response_size: 314159
      payload:{
        body: 271828 bytes of zeros
      }
    }
    ```

Client asserts:
* call was successful
* response payload body is 314159 bytes in size
* clients are free to assert that the response payload body contents are zero
  and comparing the entire response message against a golden response

Server Procedure:
  1. Reply to the client's request with a `SimpleResponse`, with a payload
  body length of `SimpleRequest.response_size`. But send it across series of
  http2 data frames that contain 5 bytes of "payload" and zero bytes of
  "padding" (the padding flags on the data frames should not be set).
