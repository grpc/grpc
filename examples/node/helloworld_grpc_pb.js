// GENERATED CODE -- DO NOT EDIT!

var grpc = require('grpc');
var helloworld_pb = require('./helloworld_pb.js');

function serialize_HelloReply(arg) {
  if (!(arg instanceof helloworld_pb.HelloReply)) {
    throw new Error('Expected argument of type HelloReply');
  }
  return new Buffer(arg.serializeBinary());
}
function deserialize_HelloReply(buffer_arg) {
  return helloworld_pb.HelloReply.deserializeBinary(new Uint8Array(buffer_arg));
}
function serialize_HelloRequest(arg) {
  if (!(arg instanceof helloworld_pb.HelloRequest)) {
    throw new Error('Expected argument of type HelloRequest');
  }
  return new Buffer(arg.serializeBinary());
}
function deserialize_HelloRequest(buffer_arg) {
  return helloworld_pb.HelloRequest.deserializeBinary(new Uint8Array(buffer_arg));
}

var GreeterService = exports.GreeterService = {
  sayHello: {
    path: '/helloworld.Greeter/SayHello',
    requestStream: false,
    responseStream: false,
    requestType: helloworld_pb.HelloRequest,
    responseType: helloworld_pb.HelloReply,
    requestSerialize: serialize_HelloRequest,
    requestDeserialize: deserialize_HelloRequest,
    responseSerialize: serialize_HelloReply,
    responseDeserialize: deserialize_HelloReply,
  },
};

exports.GreeterClient = grpc.makeGenericClientConstructor(GreeterService);
