<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/test.proto

namespace GPBMetadata\Src\Proto\Grpc\Testing;

class Test
{
    public static $is_initialized = false;

    public static function initOnce() {
        $pool = \Google\Protobuf\Internal\DescriptorPool::getGeneratedPool();

        if (static::$is_initialized == true) {
          return;
        }
        \GPBMetadata\Src\Proto\Grpc\Testing\PBEmpty::initOnce();
        \GPBMetadata\Src\Proto\Grpc\Testing\Messages::initOnce();
        $pool->internalAddGeneratedFile(
            '
�
!src/proto/grpc/testing/test.protogrpc.testing%src/proto/grpc/testing/messages.proto2�
TestServiceC
	EmptyCall.grpc.testing.EmptyMessage.grpc.testing.EmptyMessageF
	UnaryCall.grpc.testing.SimpleRequest.grpc.testing.SimpleResponseO
CacheableUnaryCall.grpc.testing.SimpleRequest.grpc.testing.SimpleResponsel
StreamingOutputCall(.grpc.testing.StreamingOutputCallRequest).grpc.testing.StreamingOutputCallResponse0i
StreamingInputCall\'.grpc.testing.StreamingInputCallRequest(.grpc.testing.StreamingInputCallResponse(i
FullDuplexCall(.grpc.testing.StreamingOutputCallRequest).grpc.testing.StreamingOutputCallResponse(0i
HalfDuplexCall(.grpc.testing.StreamingOutputCallRequest).grpc.testing.StreamingOutputCallResponse(0K
UnimplementedCall.grpc.testing.EmptyMessage.grpc.testing.EmptyMessage2c
UnimplementedServiceK
UnimplementedCall.grpc.testing.EmptyMessage.grpc.testing.EmptyMessage2�
ReconnectServiceB
Start.grpc.testing.ReconnectParams.grpc.testing.EmptyMessage?
Stop.grpc.testing.EmptyMessage.grpc.testing.ReconnectInfo2�
LoadBalancerStatsServicec
GetClientStats&.grpc.testing.LoadBalancerStatsRequest\'.grpc.testing.LoadBalancerStatsResponse" �
GetClientAccumulatedStats1.grpc.testing.LoadBalancerAccumulatedStatsRequest2.grpc.testing.LoadBalancerAccumulatedStatsResponse" 2�
HookService>
Hook.grpc.testing.EmptyMessage.grpc.testing.EmptyMessageS
SetReturnStatus$.grpc.testing.SetReturnStatusRequest.grpc.testing.EmptyMessageK
ClearReturnStatus.grpc.testing.EmptyMessage.grpc.testing.EmptyMessage2�
XdsUpdateHealthServiceD

SetServing.grpc.testing.EmptyMessage.grpc.testing.EmptyMessageG
SetNotServing.grpc.testing.EmptyMessage.grpc.testing.EmptyMessageH
SendHookRequest.grpc.testing.HookRequest.grpc.testing.HookResponse2{
XdsUpdateClientConfigureServiceX
	Configure$.grpc.testing.ClientConfigureRequest%.grpc.testing.ClientConfigureResponseB
io.grpc.testing.integrationbproto3'
        , true);

        static::$is_initialized = true;
    }
}

