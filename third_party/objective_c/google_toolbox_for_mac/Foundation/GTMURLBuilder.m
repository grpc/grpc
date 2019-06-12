//
//  GTMURLBuilder.m
//
//  Copyright 2012 Google Inc.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not
//  use this file except in compliance with the License.  You may obtain a copy
//  of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
//  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
//  License for the specific language governing permissions and limitations
//  under the License.
//

#import "GTMURLBuilder.h"

#import "GTMDefines.h"
#import "GTMLogger.h"
#import "GTMNSDictionary+URLArguments.h"
#import "GTMNSString+URLArguments.h"

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMURLBuilder
#pragma clang diagnostic ignored "-Wdeprecated-implementations"

@implementation GTMURLBuilder

@synthesize baseURLString = baseURLString_;

+ (GTMURLBuilder *)builderWithString:(NSString *)URLString {
  GTMURLBuilder *URLBuilder =
      [[[GTMURLBuilder alloc] initWithString:URLString] autorelease];
  return URLBuilder;
}

+ (GTMURLBuilder *)builderWithURL:(NSURL *)URL {
  return [self builderWithString:[URL absoluteString]];
}

- (id)init {
  self = [super init];
  [self release];
  _GTMDevAssert(NO, @"Invalid initialization.");
  return nil;
}

- (id)initWithString:(NSString *)URLString {
  self = [super init];
  if (self) {
    _GTMDevAssert(URLString, @"URL must not be nil");
    NSURL *URL = [NSURL URLWithString:URLString];
    _GTMDevAssert(URL, @"URL is invalid");

    // NSURL does not work with ports.
    baseURLString_ = [URL absoluteString];
    if ([URL query]) {
      NSRange pathRange =
          [baseURLString_ rangeOfString:[URL query] options:NSBackwardsSearch];
      if (pathRange.location != NSNotFound) {
        baseURLString_ = [baseURLString_ substringToIndex:pathRange.location-1];
      }
    }
    [baseURLString_ retain];
    params_ = [[NSDictionary gtm_dictionaryWithHttpArgumentsString:[URL query]]
        mutableCopy];
  }
  return self;
}

- (void)dealloc {
  [baseURLString_ release];
  [params_ release];
  [super dealloc];
}

- (void)setValue:(NSString *)value forParameter:(NSString *)parameter {
  [params_ setObject:value forKey:parameter];
}

- (void)setIntegerValue:(NSInteger)value forParameter:(NSString *)parameter {
  [params_ setObject:[NSString stringWithFormat:@"%ld", (long)value] forKey:parameter];
}

- (NSString *)valueForParameter:(NSString *)parameter {
  return [params_ objectForKey:parameter];
}

- (NSInteger)integerValueForParameter:(NSString *)parameter {
  return [[params_ objectForKey:parameter] integerValue];
}

- (void)removeParameter:(NSString *)parameter {
  [params_ removeObjectForKey:parameter];
}

- (void)setParameters:(NSDictionary *)parameters {
  [params_ autorelease];
  params_ = [[NSDictionary dictionaryWithDictionary:parameters] mutableCopy];
}

- (NSDictionary *)parameters {
  return params_;
}

- (NSURL *)URL {
  if (![params_ count]) {
    return [NSURL URLWithString:baseURLString_];
  } else {
    return [NSURL URLWithString:[NSString stringWithFormat:@"%@?%@",
        baseURLString_, [params_ gtm_httpArgumentsString]]];
  }
}

- (NSString *)URLString {
  return [[self URL] absoluteString];
}

- (BOOL)isEqual:(GTMURLBuilder *)URLBuilder {
  if (!URLBuilder) {
    return NO;
  }

  if (!([[self baseURLString] isEqualToString:[URLBuilder baseURLString]])) {
    return NO;
  }

  if (![[self parameters] isEqualToDictionary:[URLBuilder parameters]]) {
    return NO;
  }

  return YES;
}

- (NSUInteger)hash {
  return [baseURLString_ hash] * 17 + [params_ hash] * 37;
}

@end

#pragma clang diagnostic push
