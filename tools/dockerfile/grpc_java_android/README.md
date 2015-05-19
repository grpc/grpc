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
$ sudo docker run --name=grpc_android_test -d grpc/android /var/local/git/grpc-android-test/start-emulator.sh avd-api-22
```

You can use the following cammand to wait until the emulator is ready:
```
$ sudo docker exec grpc_android_test /var/local/git/grpc-android-test/wait-for-emulator.sh
```

When you want to update the apk, run:
```
$ sudo docker exec grpc_android_test /var/local/git/grpc-android-test/update-apk.sh
```
It will pull the fresh code of gRpc Java and our integration test app from github, build and install it to the runing emulator (so you need to make sure there is a runing emulator).

Trigger the integration test:
```
$ sudo docker exec grpc_android_test /var/local/git/grpc-android-test/run-test.sh -e server_host <hostname or ip address> -e server_port 8030 -e server_host_override foo.test.google.fr -e use_tls true -e use_test_ca true
```

You can also use the android/adb cammands to get more info, such as:
```
$ sudo docker exec grpc_android_test android list avd
$ sudo docker exec grpc_android_test adb devices
$ sudo docker exec grpc_android_test adb logcat
```
