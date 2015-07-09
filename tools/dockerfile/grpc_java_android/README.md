GRPC Android Dockerfile
====================

Dockerfile for creating the gRPC Android integration test image

As of 2015/05 this
 - is based on the gRPC Java base
 - installs Android sdk 24.2
 - creates an AVD for API level 22
 - Pulls gRpc Android test App from github

Usage
-----

Start the emulator in a detached container, the argument is the name of the AVD you want to start:

```
$ sudo docker run --name=grpc_android_test -d grpc/android /var/local/git/grpc-java/android-interop-testing/start-emulator.sh avd-google-api-22
```

You can use the following cammand to wait until the emulator is ready:
```
$ sudo docker exec grpc_android_test /var/local/git/grpc-java/android-interop-testing/wait-for-emulator.sh
```

When you want to update the apk, run:
```
$ sudo docker exec grpc_android_test bash -c "cd /var/local/git/grpc-java && git pull origin master && ./gradlew grpc-core:install grpc-stub:install grpc-okhttp:install grpc-protobuf-nano:install grpc-compiler:install && cd android-interop-testing && ../gradlew installDebug"
```
It pulls the fresh code of gRpc Java and our interop test app from github, build and install it to the runing emulator (so you need to make sure there is a runing emulator).

Trigger the integration test:
```
$ sudo docker exec grpc_android_test adb -e shell am instrument -w -e server_host <hostname or ip address> -e server_port 8030 -e server_host_override foo.test.google.fr -e use_tls true -e use_test_ca true -e test_case all io.grpc.android.integrationtest/.TesterInstrumentation
```

You can also use the android/adb cammands to get more info, such as:
```
$ sudo docker exec grpc_android_test android list avd
$ sudo docker exec grpc_android_test adb devices
$ sudo docker exec grpc_android_test adb logcat
```
