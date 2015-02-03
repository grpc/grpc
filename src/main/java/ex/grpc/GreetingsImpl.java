package ex.grpc;

import com.google.net.stubby.stub.StreamObserver;

public class GreetingsImpl implements GreetingsGrpc.Greetings {

  @Override
  public void hello(Helloworld.HelloRequest req,
      StreamObserver<Helloworld.HelloReply> responseObserver) {
    Helloworld.HelloReply reply = Helloworld.HelloReply.newBuilder().setMessage(
        "Hello " + req.getName()).build();
    responseObserver.onValue(reply);
    responseObserver.onCompleted();
  }

}
