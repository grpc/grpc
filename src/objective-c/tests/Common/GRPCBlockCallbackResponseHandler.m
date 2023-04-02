/**
 * Copyright 2022 gRPC authors.
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
 */

#import "GRPCBlockCallbackResponseHandler.h"

@implementation GRPCBlockCallbackResponseHandler {
  void (^_initialMetadataCallback)(NSDictionary *);
  void (^_messageCallback)(id);
  void (^_closeCallback)(NSDictionary *, NSError *);
  void (^_writeDataCallback)(void);
  dispatch_queue_t _dispatchQueue;
}

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                messageCallback:(void (^)(id))messageCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback
                              writeDataCallback:(void (^)(void))writeDataCallback {
  if ((self = [super init])) {
    _initialMetadataCallback = initialMetadataCallback;
    _messageCallback = messageCallback;
    _closeCallback = closeCallback;
    _writeDataCallback = writeDataCallback;
    _dispatchQueue = dispatch_queue_create(nil, DISPATCH_QUEUE_SERIAL);
  }
  return self;
}

- (instancetype)initWithInitialMetadataCallback:(void (^)(NSDictionary *))initialMetadataCallback
                                messageCallback:(void (^)(id))messageCallback
                                  closeCallback:(void (^)(NSDictionary *, NSError *))closeCallback {
  return [self initWithInitialMetadataCallback:initialMetadataCallback
                               messageCallback:messageCallback
                                 closeCallback:closeCallback
                             writeDataCallback:nil];
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  if (self->_initialMetadataCallback) {
    self->_initialMetadataCallback(initialMetadata);
  }
}

- (void)didReceiveRawMessage:(id)message {
  if (self->_messageCallback) {
    self->_messageCallback(message);
  }
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  if (self->_closeCallback) {
    self->_closeCallback(trailingMetadata, error);
  }
}

- (void)didWriteData {
  if (self->_writeDataCallback) {
    self->_writeDataCallback();
  }
}

- (dispatch_queue_t)dispatchQueue {
  return _dispatchQueue;
}

@end
