This app can be used to manually test gRPC under changing network conditions.

It makes RPCs in a loop, logging when the request is sent and the response is received.

To test on the simulator, run `pod install`, open the workspace created by Cocoapods, and run the app.
Once running, disable WiFi (or ethernet) _in your computer_, then enable it again after a while. Don't
bother with the simulator's WiFi or cell settings, as they have no effect: Simulator apps are just Mac
apps running within the simulator UI.

The expected result is to never see a "hanged" RPC: success or failure should happen almost immediately
after sending the request. Symptom of a hanged RPC is a log like the following being the last in your
console:

```
2016-06-29 16:51:29.443 ConnectivityTestingApp[73129:3567949] Sending request.
```
