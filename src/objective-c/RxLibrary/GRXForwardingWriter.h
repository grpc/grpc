/*
 *
 * Copyright 2015 gRPC authors.
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

#import "GRXWriter.h"

/**
 * A "proxy" class that simply forwards values, completion, and errors from its input writer to its
 * writeable.
 * It is useful as a superclass for pipes that act as a transformation of their
 * input writer, and for classes that represent objects with input and
 * output sequences of values, like an RPC.
 *
 * Thread-safety:
 * All messages sent to this object need to be serialized. When it is started, the writer it wraps
 * is started in the same thread. Manual state changes are propagated to the wrapped writer in the
 * same thread too. Importantly, all messages the wrapped writer sends to its writeable need to be
 * serialized with any message sent to this object.
 */
@interface GRXForwardingWriter : GRXWriter
- (instancetype)initWithWriter:(GRXWriter *)writer NS_DESIGNATED_INITIALIZER;
@end
