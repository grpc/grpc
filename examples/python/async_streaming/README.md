# gRPC Python Non-Blocking Streaming RPC Client Example

The goal of this example is to demonstrate how to handle streaming responses
without blocking the current thread. Effectively, this can be achieved by
converting the gRPC Python streaming API into callback-based.

In this example, the RPC service `Phone` simulates the life cycle of virtual
phone calls. It requires one thread to handle the phone-call session state
changes, and another thread to process the audio stream. In this case, the
normal blocking style API could not fulfill the need easily. Hence, we should
asynchronously execute the streaming RPC.

## Steps to run this example

Start the server in one session
```
python3 server.py
```

Start the client in another session
```
python3 client.py
```

## Example Output
```
$ python3 server.py
INFO:root:Server serving at [::]:50051
INFO:root:Received a phone call request for number [1415926535]
INFO:root:Created a call session [{
  "sessionId": "0",
  "media": "https://link.to.audio.resources"
}]
INFO:root:Call finished [1415926535]
INFO:root:Call session cleaned [{
  "sessionId": "0",
  "media": "https://link.to.audio.resources"
}]
```

```
$ python3 client.py
INFO:root:Waiting for peer to connect [1415926535]...
INFO:root:Call toward [1415926535] enters [NEW] state
INFO:root:Call toward [1415926535] enters [ACTIVE] state
INFO:root:Consuming audio resource [https://link.to.audio.resources]
INFO:root:Call toward [1415926535] enters [ENDED] state
INFO:root:Audio session finished [https://link.to.audio.resources]
INFO:root:Call finished!
```
