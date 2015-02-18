#import "GRXWriter.h"

@interface GRXWriter () <GRXWriteable>
@end

@implementation GRXWriter {
  id<GRXWriter> _writer;
  id<GRXWriteable> _writeable;
}

- (instancetype)init {
  return [self initWithWriter:nil];
}

// Designated initializer
- (instancetype)initWithWriter:(id<GRXWriter>)writer {
  if (!writer) {
    [NSException raise:NSInvalidArgumentException format:@"writer can't be nil."];
  }
  if ((self = [super init])) {
    _writer = writer;
  }
  return self;
}

// This is used to send a completion or an error to the writeable. It nillifies
// our reference to it in order to guarantee no more messages are sent to it,
// and to release it.
- (void)finishOutputWithError:(NSError *)errorOrNil {
  id<GRXWriteable> writeable = _writeable;
  _writeable = nil;
  [writeable didFinishWithError:errorOrNil];
}

// This is used to stop the input writer. It nillifies our reference to it
// to release it.
- (void)finishInput {
  id<GRXWriter> writer = _writer;
  _writer = nil;
  writer.state = GRXWriterStateFinished;
}

#pragma mark GRXWriteable implementation

- (void)didReceiveValue:(id)value {
  [_writeable didReceiveValue:value];
}

- (void)didFinishWithError:(NSError *)errorOrNil {
  _writer = nil;
  [self finishOutputWithError:errorOrNil];
}

#pragma mark GRXWriter implementation

- (GRXWriterState)state {
  return _writer ? _writer.state : GRXWriterStateFinished;
}

- (void)setState:(GRXWriterState)state {
  if (state == GRXWriterStateFinished) {
    _writeable = nil;
    [self finishInput];
  } else {
    _writer.state = state;
  }
}

- (void)startWithWriteable:(id<GRXWriteable>)writeable {
  _writeable = writeable;
  [_writer startWithWriteable:self];
}

- (void)finishWithError:(NSError *)errorOrNil {
  [self finishOutputWithError:errorOrNil];
  [self finishInput];
}

@end
