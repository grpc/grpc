package ex.grpc;

import com.google.net.stubby.stub.StreamObserver;

public class GreeterImpl implements GreeterGrpc.Greeter {

  @Override
  public void sayHello(Helloworld.HelloRequest req,
      StreamObserver<Helloworld.HelloReply> responseObserver) {
    Helloworld.HelloReply reply = Helloworld.HelloReply.newBuilder().setMessage(
        "Hello " + req.getName()).build();
    responseObserver.onValue(reply);
    responseObserver.onCompleted();
  }

}
