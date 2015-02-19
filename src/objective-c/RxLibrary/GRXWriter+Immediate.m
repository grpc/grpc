#import "GRXWriter+Immediate.h"

#import "GRXImmediateWriter.h"

@implementation GRXWriter (Immediate)

+ (instancetype)writerWithEnumerator:(NSEnumerator *)enumerator {
  return [[self alloc] initWithWriter:[GRXImmediateWriter writerWithEnumerator:enumerator]];
}

+ (instancetype)writerWithValueSupplier:(id (^)())block {
  return [[self alloc] initWithWriter:[GRXImmediateWriter writerWithValueSupplier:block]];
}

+ (instancetype)writerWithContainer:(id<NSFastEnumeration>)container {
  return [[self alloc] initWithWriter:[GRXImmediateWriter writerWithContainer:container]];
}

+ (instancetype)writerWithValue:(id)value {
  return [[self alloc] initWithWriter:[GRXImmediateWriter writerWithValue:value]];
}

+ (instancetype)writerWithError:(NSError *)error {
  return [[self alloc] initWithWriter:[GRXImmediateWriter writerWithError:error]];
}

+ (instancetype)emptyWriter {
  return [[self alloc] initWithWriter:[GRXImmediateWriter emptyWriter]];
}

@end
