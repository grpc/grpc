#import "GRPCWrappedChannelArgs.h"

#import <grpc/support/log.h>

#pragma mark - Argument Types

@interface GRPCWrappedChannelArg : NSObject

@property(nonatomic, readonly) NSString *grpc_key;

- (instancetype)init NS_UNAVAILABLE;

- (instancetype)initWithKey:(NSString *)key NS_DESIGNATED_INITIALIZER;

@end

@implementation GRPCWrappedChannelArg {
  NSString *grpc_key_;
}

- (instancetype)initWithKey:(NSString *)key {
  GPR_ASSERT(key);
  if (self = [super init]) {
    grpc_key_ = [key copy];
  }
  return self;
}

- (NSString *)grpc_key {
  return grpc_key_;
}

@end

#pragma mark String Argument Type

@interface GRPCWrappedChannelStringArg : GRPCWrappedChannelArg

@property(nonatomic, readonly) NSString *grpc_string;

- (instancetype)initWithKey:(NSString *)key NS_UNAVAILABLE;

- (instancetype)initWithKey:(NSString *)key value:(NSString *)value NS_DESIGNATED_INITIALIZER;

@end


@implementation GRPCWrappedChannelStringArg  {
  NSString *grpc_value_;
}

- (instancetype)initWithKey:(NSString *)key value:(NSString *)value {
  GPR_ASSERT(value);
  if (self = [super initWithKey:key]) {
    grpc_value_ = [value copy];
  }
  return self;
}

- (NSString *)grpc_string {
  return grpc_value_;
}

@end

#pragma mark Integer Argument Type

@interface GRPCWrappedChannelIntegerArg : GRPCWrappedChannelArg

@property(nonatomic, readonly) int grpc_integer;

- (instancetype)initWithKey:(NSString *)key NS_UNAVAILABLE;

- (instancetype)initWithKey:(NSString *)key value:(int)value NS_DESIGNATED_INITIALIZER;

@end


@implementation GRPCWrappedChannelIntegerArg  {
  int grpc_value_;
}

- (instancetype)initWithKey:(NSString *)key value:(int)value {
  if (self = [super initWithKey:key]) {
    grpc_value_ = value;
  }
  return self;
}

- (int)grpc_integer {
  return grpc_value_;
}

@end

#pragma mark - Wrapped Channel Arguments

@interface GRPCWrappedChannelArgs ()

- (instancetype)initWithChannelArgs:(grpc_channel_args)channelArgs;

@end

@implementation GRPCWrappedChannelArgs {
  grpc_channel_args channelArgs_;
}

- (instancetype)initWithChannelArgs:(grpc_channel_args)channelArgs {
  if (self = [super init]) {
    channelArgs_ = channelArgs;
  }
  return self;
}

- (grpc_channel_args)channelArgs {
  return channelArgs_;
}

- (void)dealloc {
  for (size_t i = 0; i < channelArgs_.num_args; ++i) {
    grpc_arg *arg = &channelArgs_.args[i];
    free(arg->key);
    if (arg->type == GRPC_ARG_STRING) {
      free(arg->value.string);
    }
  }
  free(channelArgs_.args);
}

@end

#pragma mark - Wrapped Channel Arguments Builder

@implementation GRPCWrappedChannelArgsBuilder {
  NSMutableArray *args_;
}

- (instancetype)init {
  if (self = [super init]) {
    args_ = [NSMutableArray array];
  }
  return self;
}

- (instancetype)addKey:(NSString *)key stringValue:(NSString *)value {
  GRPCWrappedChannelStringArg *arg = [[GRPCWrappedChannelStringArg alloc] initWithKey:key value:value];
  [args_ addObject:arg];
  return self;
}

- (instancetype)addKey:(NSString *)key integerValue:(int)value {
  GRPCWrappedChannelIntegerArg *arg = [[GRPCWrappedChannelIntegerArg alloc] initWithKey:key value:value];
  [args_ addObject:arg];
  return self;
}

- (GRPCWrappedChannelArgs *)build {
  grpc_channel_args channelArgs;

  // channelArgs.args and contents is freed by GRPCWrappedChannelArgs::dealloc
  channelArgs.num_args = args_.count;
  channelArgs.args = (grpc_arg *) calloc(args_.count, sizeof(grpc_arg));

  for (NSInteger i = 0; i < args_.count; ++i) {
    if ([args_[i] respondsToSelector:@selector(grpc_string)]) {
      GRPCWrappedChannelStringArg *arg = (GRPCWrappedChannelStringArg *)args_[i];
      grpc_arg *wrappedArg = &channelArgs.args[i];
      wrappedArg->key = strdup(arg.grpc_key.UTF8String);
      wrappedArg->type = GRPC_ARG_STRING;
      wrappedArg->value.string = strdup(arg.grpc_string.UTF8String);
      GPR_ASSERT(wrappedArg->key);
      GPR_ASSERT(wrappedArg->value.string);
    } else if ([args_[i] respondsToSelector:@selector(grpc_integer)]) {
      GRPCWrappedChannelIntegerArg *arg = (GRPCWrappedChannelIntegerArg *)args_[i];
      grpc_arg *wrappedArg = &channelArgs.args[i];
      wrappedArg->key = strdup(arg.grpc_key.UTF8String);
      wrappedArg->type = GRPC_ARG_INTEGER;
      wrappedArg->value.integer = arg.grpc_integer;
      GPR_ASSERT(wrappedArg->key);
    } else {
      GPR_ASSERT(0); // Argument type not recognized
    }
  }

  return [[GRPCWrappedChannelArgs alloc] initWithChannelArgs:channelArgs];
}

@end
