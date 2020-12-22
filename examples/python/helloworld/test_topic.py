import logging
import asyncio
import grpc

import helloworld_pb2
import helloworld_pb2_grpc


# this is the generic way of sending message to a channel with a topic
# there's no stub (auto generated async code) required
# we can use this code in userclient without auto generated code to send request to pusher
async def send_info_through_topic():
    addr = "localhost:50051"
    TOPIC = '/helloworld.Greeter/SayHelloStreaming'
    _channel = grpc.aio.insecure_channel(addr)
    stream_stream_call = _channel.stream_stream(TOPIC)
    call = stream_stream_call()

    await call.write(helloworld_pb2.HelloRequest(name='name 1 ').SerializeToString())
    await call.write(helloworld_pb2.HelloRequest(name='name 2').SerializeToString())

    response_1 = await call.read()
    print(f'response1: {response_1}')
    response_2 = await call.read()
    print(f'response2: {response_2}')


if __name__ == '__main__':
    asyncio.run(send_info_through_topic())
