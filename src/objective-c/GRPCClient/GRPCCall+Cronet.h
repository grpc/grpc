/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifdef GRPC_COMPILE_WITH_CRONET
#import <Cronet/Cronet.h>

#import "GRPCCall.h"

/**
 * Methods for using cronet transport.
 */
@interface GRPCCall (Cronet)

/**
 * This method should be called before issuing the first RPC. It should be
 * called only once. Create an instance of Cronet engine in your app elsewhere
 * and pass the instance pointer in the stream_engine parameter. Once set,
 * all subsequent RPCs will use Cronet transport. The method is not thread
 * safe.
 */
+(void)useCronetWithEngine:(stream_engine *)engine;

+(stream_engine *)cronetEngine;

+(BOOL)isUsingCronet;

@end
#endif
