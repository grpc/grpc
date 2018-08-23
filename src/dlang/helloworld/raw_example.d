
module helloworld.raw_example;


import helloworld.helloworld;
import google.protobuf;
import grpc;
import std.stdio;
import std.array:array;


import kiss.logger;

class MyService : GrpcService
{
	string getModule()
	{
		return "helloworld.Greeter";
	}

	ubyte[] process(string method , ubyte[] data)
    {
        HelloRequest request = new HelloRequest();
		data.fromProtobuf!HelloRequest(request);

		HelloReply reply = new HelloReply();
		reply.message = "hello " ~ request.name;
		logError(reply.message , reply.message.length);
		return reply.toProtobuf.array;
    }
}




void main() {

	string host = "0.0.0.0";
	ushort port = 50051;

	GrpcServer server = new GrpcServer();
	server.listen(host , port);
	server.register( new MyService());
	server.start();

	GrpcClient client = new GrpcClient();

	client.connect("127.0.0.1" , port);
	HelloRequest request = new HelloRequest();
    request.name = "world";
	ubyte[] data =  client.send("/helloworld.Greeter/SayHello" , request.toProtobuf.array);
	HelloReply reply = new HelloReply();
	data.fromProtobuf!HelloReply(reply);

	logError(reply.message);

	string[] test_name = ["1" , "2" , "3" , "4" , "5" , "6" , "7" , "8" , "9" , "0"];
	foreach(name ; test_name)	
	{
		HelloRequest request1 = new HelloRequest();
		request1.name = name;
		ubyte[] data1 =  client.send("/helloworld.Greeter/SayHello" ,request1.toProtobuf.array);
		HelloReply reply1 = new HelloReply();
		data1.fromProtobuf!HelloReply(reply1);
		logError(reply1.message);
	}

}
