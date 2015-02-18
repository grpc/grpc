#import "GRXWriteable.h"

@implementation GRXWriteable {
  GRXValueHandler _valueHandler;
  GRXCompletionHandler _completionHandler;
}

- (instancetype)init {
  return [self initWithValueHandler:nil completionHandler:nil];
}

// Designated initializer
- (instancetype)initWithValueHandler:(GRXValueHandler)valueHandler
                   completionHandler:(GRXCompletionHandler)completionHandler {
  if ((self = [super init])) {
    _valueHandler = valueHandler;
    _completionHandler = completionHandler;
  }
  return self;
}

- (void)didReceiveValue:(id)value {
  if (_valueHandler) {
    _valueHandler(value);
  }
}

- (void)didFinishWithError:(NSError *)errorOrNil {
  if (_completionHandler) {
    _completionHandler(errorOrNil);
  }
}
@end
