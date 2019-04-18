This app can be used to manually test gRPC under changing network conditions.

It makes RPCs in a loop, logging when the request is sent and the response is received.

To test on the simulator, run `pod install`, open the workspace created by Cocoapods, and run the
app on an iOS device. Once running, tap a few times of each of the two buttons to make a few unary and streaming
calls. Then disable/enable different network interfaces (WiFi, cellular) on your device.

The expected behavior is that the pending streaming calls fails immediately with error UNAVAILABLE.
Moreover, when network comes back, new calls have the same behavior.

```
2016-06-29 16:51:29.443 ConnectivityTestingApp[73129:3567949] Sending request.
```
