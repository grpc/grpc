## Demo of Flow-Control mechanism in gRPC Python
Flow control is the mechanism used to ensure that a message *receiver* does not get overwhelmed by a fast *sender*. This is done by checking if the receiver is ready to receive messages (has enough buffer space) and then send messages. gRPC handles interactions with flow-control by default and this example can be used to see this in action. [Read more about Flow Control here](https://grpc.io/docs/guides/flow-control/)

The design of the example is as follows:

1. The client sends a bulk amount of data(approx. 2 KB in each iteration) to the server in a streaming call.
2. The server applies back-pressure by delaying reading of the requests, which makes the client pause sending requests after around 64KB.
3. The client then resumes sending requests only after the server starts reading requests and clears the buffer.
4. The client further waits to send every subsequent request as the server reads requests at a slower rate.

### Steps to run the example
1. Install Python3 and gRPC by following commands in the [**Quickstart Guide**](https://grpc.io/docs/languages/python/quickstart/)
2. Open 2 terminals and navigate to the `flow_control` example folder in both using below command:
    ```
    $ cd grpc/examples/python/flow_control
    ```

3. In one terminal start the server using the command:
    ```
    $ python3 flow_control_server.py
    ``` 
4. In the other terminal, run the client using the command:
    ```
    $ python3 flow_control_client.py
    ```

### Code Explanation
#### Client
1. A channel with the following options are created by the client:
    * **grpc.http2.max_frame_size: 16384** 
        <br>The maximum size of the data frame we are willing to receive over HTTP2. As we want to fill up the buffer space, we want to use the smallest size for this example. 16384 (16kB) is the minimum value allowed by gRPC
    * **grpc.http2.bdp_probe: 0**
        <br>Bandwidth Delay Product (BDP) is the bandwidth of a network connection times its round-trip latency. This effectively tells us how many bytes can be “on the wire” at a given moment, if full utilization is achieved. The `bdp_probe` argument is a boolean value used to determine whether or not to resize the window to allow more flow of data over the wire.
        <br>For this example, as we want to fill up the window, we disallow window resize by setting to 0 (False)
2. The client then makes a bi-directional streaming call to the server, sending bulk amounts of data in the stream. It sends approximately 2kB of data per iteration for 100 iterations, i.e. approx. 200kB of data.
3. For every 10 requests sent/received the client logs the total amount of data sent.

#### Server
1. A server is created with the following options:
    * **grpc.http2.max_frame_size: 16384** - (Same reasoning as above)
    * **grpc.http2.bdp_probe: 0** - (Same reasoning as above)
    * **grpc.max_concurrent_streams: 1**
        <br>Restricting maximum incoming streams to 1, so as to limit maximum amount of data that can be sent at a time.
2. On receiving a streaming call, the server thread is initially made to wait for 5 seconds before starting to read requests. This is done intentionally to apply back-pressure on the client to stop sending requests.
3. The server thread is further made to process each request slowly with a simulated 1 second delay, to continue applying back-pressure on the client.
4. For every 10 requests received/sent the server logs the total amount of data received.

### Sample Output
**Sample Client Logs**
```
12:21:45.755064   Request 10: Sent 20000 bytes in total

12:21:45.755748   Request 20: Sent 40000 bytes in total

12:21:45.756414   Request 30: Sent 60000 bytes in total     # the client waits for the server to read before making further requests (note difference in timestamp below)

12:21:57.769414   Request 40: Sent 80000 bytes in total
12:22:00.777107   Received 10 responses


12:22:06.793740   Request 50: Sent 100000 bytes in total    # the client further waits for the server to read more requests beforeit can continue sending 
12:22:10.801932   Received 20 responses


12:22:17.817946   Request 60: Sent 120000 bytes in total
12:22:20.825493   Received 30 responses


12:22:25.836613   Request 70: Sent 140000 bytes in total
12:22:30.849854   Received 40 responses


12:22:36.860275   Request 80: Sent 160000 bytes in total
12:22:40.868944   Received 50 responses


12:22:47.884548   Request 90: Sent 180000 bytes in total
12:22:50.891289   Received 60 responses


12:22:56.903075   Request 100: Sent 200000 bytes in total
12:23:00.911084   Received 70 responses

12:23:10.928850   Received 80 responses

12:23:20.943803   Received 90 responses

12:23:30.968277   Received 100 responses
```

**Sample Server Logs**
```
Server started, listening on 50051
12:21:59.774561   Request 10:   Received 20000 bytes in total   # the server first logs that it has received 10 requests.
12:22:00.776726   Request 10:   Sent 20000 bytes in total

12:22:09.799547   Request 20:   Received 40000 bytes in total
12:22:10.801503   Request 20:   Sent 40000 bytes in total

12:22:19.822669   Request 30:   Received 60000 bytes in total
12:22:20.824415   Request 30:   Sent 60000 bytes in total

12:22:29.846496   Request 40:   Received 80000 bytes in total
12:22:30.848089   Request 40:   Sent 80000 bytes in total

12:22:39.866171   Request 50:   Received 100000 bytes in total
12:22:40.867788   Request 50:   Sent 100000 bytes in total

12:22:49.890288   Request 60:   Received 120000 bytes in total
12:22:50.891621   Request 60:   Sent 120000 bytes in total

12:22:59.909998   Request 70:   Received 140000 bytes in total
12:23:00.910996   Request 70:   Sent 140000 bytes in total

12:23:09.926919   Request 80:   Received 160000 bytes in total
12:23:10.928391   Request 80:   Sent 160000 bytes in total

12:23:19.941289   Request 90:   Received 180000 bytes in total
12:23:20.943090   Request 90:   Sent 180000 bytes in total

12:23:29.965695   Request 100:   Received 200000 bytes in total
12:23:30.967889   Request 100:   Sent 200000 bytes in total
```