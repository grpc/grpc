gRPC Hello World Tutorial (Android Java)
========================

BACKGROUND
-------------
For this sample, we've already generated the server and client stubs from [helloworld.proto](https://github.com/grpc/grpc-common/blob/master/protos/helloworld.proto). 

PREREQUISITES
-------------
- [Java gRPC](https://github.com/grpc/grpc-java)

- [Android Tutorial](https://developer.android.com/training/basics/firstapp/index.html) If you're new to Android development

- We only have Android gRPC client in this example. Please follow examples in other languages to build and run a gRPC server.

INSTALL
-------
1 Clone the gRPC Java git repo
```sh
$ git clone https://github.com/grpc/grpc-java
```

2 Install gRPC Java, as described in [How to Build](https://github.com/grpc/grpc-java#how-to-build)
```sh
$ # from this dir
$ cd grpc-java
$ # follow the instructions in 'How to Build'
```

3 [Create an Android project](https://developer.android.com/training/basics/firstapp/creating-project.html) under your working directory.
- Set Application name to "Helloworld Example" and set Company Domain to "grpc.io". Make sure your package name is "io.grpc.helloworldexample"
- Choose appropriate minimum SDK
- Use Blank Activity
- Set Activity Name to HelloworldActivity
- Set Layout Name to activity_helloworld

4 Prepare the app
- Clone this git repo
```sh
$ git clone https://github.com/grpc/grpc-common

```
- Replace the generated HelloworldActivity.java and activity_helloworld.xml with the two files in this repo
- Copy GreeterGrpc.java and Helloworld.java under your_app_dir/app/src/main/java/io/grpc/examples/
- In your AndroidManifest.xml, make sure you have
```sh
<uses-permission android:name="android.permission.INTERNET" />
```
added outside your appplication tag

5 Add dependencies. gRPC Java on Android depends on grpc-java, protobuf nano, okhttp
- Copy grpc-java .jar files to your_app_dir/app/libs/:
  - grpc-java/core/build/libs/*.jar
  - grpc-java/stub/build/libs/*.jar
  - grpc-java/nano/build/libs/*.jar
  - grpc-java/okhttp/build/libs/*.jar
- Copy or download other dependencies to your_app_dir/app/libs/:
  - [Guava 18](http://search.maven.org/remotecontent?filepath=com/google/guava/guava/18.0/guava-18.0.jar)
  - [okhttp 2.2.0](http://repo1.maven.org/maven2/com/squareup/okhttp/okhttp/2.2.0/okhttp-2.2.0.jar)
  - protobuf nano:
```sh
$ cp ~/.m2/repository/com/google/protobuf/nano/protobuf-javanano/2.6.2-pre/protobuf-javanano-2.6.2-pre.jar your_app_dir/app/libs/
```
- Make sure your_app_dir/app/build.gradle contains:
```sh
dependencies {
    compile fileTree(dir: 'libs', include: ['*.jar'])
}
```

6 [Run your example app](https://developer.android.com/training/basics/firstapp/running-app.html)
