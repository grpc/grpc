namespace cpp test

struct HelloRequest {
	1:string name
}

struct HelloResponse {
	1:string message
}

service Greeter {
	HelloResponse SayHello(1:HelloRequest request);
}