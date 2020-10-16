<?php
// DO NOT EDIT
namespace Grpc\Testing;
class LoadBalancerStatsServiceStub {
    /**
     * @return array of [response, initialMetadata, status]
     */
    function getClientStats(
    \Grpc\Testing\LoadBalancerStatsRequest $request,
    array $metadata,
    \Grpc\ServerContext $serverContext) {
        return [null, [], \Grpc\Status::unimplemented()];
    }
}

