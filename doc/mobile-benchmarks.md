Mobile gRPC Benchmarks
======================
This readme outlines the methodology used for the mobile benchmarks, results 
observed, and instructions to replicate our results. There are two main 
sections: protobuf (de)serialization benchmarks and gRPC benchmarks. The 
protobuf benchmarks are run against equivalent JSON structures, and the gRPC 
benchmarks are run against a similar RESTful HTTP service using JSON. 

Methodology
-----------
### Protobuf Benchmarks
After a user chooses which message to benchmark, an instance with 
[randomally generated fields](/protolite_app/app/src/main/java/io/grpc/grpcbenchmarks/protobufRandomWriter.java) is instantiated. After a short warmup, the 
message is then either serialized or deserialized over and over until a certain 
amount of time has passed, ten seconds in this case. In order to keep 
benchmarks as accurate as possible, we wrap the code to be benchmarked in an interface and simply loop it for a set number of iterations.
```Java
interface Action {
    void execute();
}

// Sample benchmark of multiplication
Action a = new Action() {
    @Override
    public void execute() {
        int x = 1000 * 123456;
    }
}

for (int i = 0; i < 100; ++i) {
    a.execute();
}
```
We determine the number of iterations to be done by first timing a single 
iteration, seeing if it surpasses a minimum sample time (2 seconds), and 
repeating with 2 * previous number of iterations if it doesn't. If it does, 
we then guess the number of iterations it would take to run for 10 seconds by 
doing a little math. The code for this can be seen in 
[protobufBenchmarker.java](/protolite_app/app/src/main/java/io/grpc/grpcbenchmarks/protobufBenchmarker.java#L177). 

The same method is used for the JSON equivalent. The JSON benchmarks can also be run using gzip to compress the data after serialization.

### gRPC Benchmarks
The gRPC benchmarks are mostly adapted from 
[AsyncClient.java](https://github.com/grpc/grpc-java/blob/master/benchmarks/src/main/java/io/grpc/benchmarks/qps/AsyncClient.java) in the `benchmarks/` folder of the grpc-java repo. Essentially what happens is a channel is opened, a message with a specified payload size is sent, a response with the same payload size is recieved, and we repeat until 60 seconds have passed. Total end to end times are recorded in a historgram (this includes processing time). 

Unlike the protobuf benchmarks, we don't mind if we check or get the time every 
iteration since the latency of the connection will always vastly overshadow it. 

#### HTTP JSON Benchmarks
The JSON benchmark uses the same method as the gRPC benchmark. The server simply reads in the payload and returns it as a response. Various servers were used, such as [Spark](http://sparkjava.com/), Apache, and a basic Python server.

Results
-------
All benchmarks were run on a Nexus 7 tablet running Android 4.4.4.

### Protobuf vs. JSON
The below show how quickly protobuf can serialize/deserialize a messasge of a specific size. The last two compare JSON and gzipped JSON's performance to protobuf's.
![Speeds of protobuf serialization/deserialiation](/benchmark_results/protobuf_speeds.png)

![Comparison of serialization/deserialization speeds of protobuf and JSON](/benchmark_results/proto_vs_json.png)

![Comparison of serialization/deserialization speeds of protobuf and gzipped JSON](/benchmark_results/proto_speeds_gzip.png)

#### Considerations
Protobuf needs to calculate the size of its message when serializing in order to allocate a large enough byte array. However, when it's called once it gets cached, thus leading to skewed results with successive runs. We suspect this could up to double the reported speed. However, the speed at which protobuf serializes is well over 2x than JSON.

Gzip is disabled for the "Small request" proto, since it actually increases size.

### gRPC vs. RESTful HTTP JSON API
![Graph of latencies for RPC calls](/benchmark_results/latencies.png)

![Latencies vs Spark using a 250b payload](/benchmark_results/latencies_250.png)
![Latencies vs Spark using a 2.5k payload](/benchmark_results/latencies_2500.png)
![Latencies vs Spark using a 50k payload](/benchmark_results/latencies_50k.png)
![Latencies vs Spark using a 100k payload](/benchmark_results/latencies_100k.png)

#### Considerations
As you can see, the results for a POST vs. a GET are drastically different. This is due to the fact that for each POST request done in Android, an output stream needs to be opened, written to, then closed before sending the request. Using Square's OkHttp library makes this a bit better, but still results in a large difference between a gRPC request and a POST request.

There was a tiny difference between using `HttpURLConnection` and the OkHttp library, but it is almost negligible. You can test it yourself by checking the 'Use OkHttp' box. 

Replicating Results
-------------------
TODO: Revise once merged into grpc-java repo

In order to run the benchmarks on your own device, you'll first need to clone and install the grpc-java repo (not necessary for released versions).
```
$ git clone https://github.com/grpc/grpc-java.git
$ cd grpc-java/
$ ./gradlew install -PskipCodegen=true
```

Clone this repo
```
$ git clone https://github.com/david-cao/gRPCBenchmarks.git
```
If you're using Android Studio, simply open the protolite_app project in Android Studio and sync and build.

Otherwise, change directories to the protolite_app folder, and run
```
$ ./gradlew installDebug
```
to build the application. From there use [`adb`](https://developer.android.com/studio/command-line/adb.html) to run the application on your device.

### Benchmarking Protobuf
First choose the protofile you want to run benchmarks on. You can examine them in more depth [here](/protolite_app/app/src/main/proto). Also choose whether or not to gzip the JSON during benchmarks (gzip is disabled for "Small request", since it adds in size). Then tap the "Run All Benchmarks" button to begin the benchmarks. Each benchmark takes about 15 seconds, 5 for warmup and calculation, and 10 for the actual benchmark. 

Note: If you tap "Run All Benchmarks", the same protofile/JSON object will be used across all benchmarks. If you tap each benchmark individually, a new random protofile/JSON object will be used each time.

### Benchmarking gRPC
First, build the benchmark server. From the grc-java directory type
```
$ ./gradlew :grpc-benchmarks:installDist
```

Ensure your Android device can access your computer over the network. This can be done either with USB tethering or a local network. Then start the `qps_server` by running
```
$ ./benchmarks/build/install/grpc-benchmarks/bin/qps_server --address=localhost:50051
```
The benchmarking app expects for `qps_server` to be running on port 50051. 

Once server is up, type in your IP, number of concurrent connections you want (recommended 1), the size of your payload (defaults to 100 bytes), and press the play button for the gRPC benchmarks. The benchmarks will take about 70 seconds, 10 for warmup and 60 for the benchmarks.

#### Benchmarking HTTP JSON
Make sure you're in the `http_server` directory and simply run 
```
$ ./gradlew run
```
to start the server. From there, everything is the same as the gRPC benchmarks. Benchmarks will also take about 70 seconds, 10 for warmup and 60 for the benchmarks. The app expects the server to be running on port 50052, which is already enabled by default. Make sure nothing is blocking that port before starting the server.