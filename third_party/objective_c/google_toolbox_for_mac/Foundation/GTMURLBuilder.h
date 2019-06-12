//
//  GTMURLBuilder.h
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

//
// Class for creating URLs. It handles URL encoding of parameters.
//
// Usage example:
//
// GTMURLBuilder *URLBuilder =
//     [GTMURLBuilder builderWithString:@"http://www.google.com"];
// [URLBuilder setValue:@"abc" forParameter:@"q"];
// NSURL *URL = [URLBuilder URL];
//
// NOTE: Apps targeting iOS 8 or OS X 10.10 and later should use
//       NSURLComponents and NSURLQueryItem to create URLs with
//       query arguments instead of using this class.


#import <Foundation/Foundation.h>
#import "GTMDefines.h"

#if (!TARGET_OS_IPHONE && defined(MAC_OS_X_VERSION_10_10) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_10) \
|| (TARGET_OS_IPHONE && defined(__IPHONE_8_0) && __IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_8_0)
__deprecated_msg("GTMURLBuilder is obsolete; update your app to use NSURLComponents queryItems property instead.")
#endif
@interface GTMURLBuilder : NSObject {
 @private
  NSMutableDictionary *params_;
}

@property(nonatomic, readonly) NSString *baseURLString;

// |URLString| is expected to be a valid URL with already escaped parameter
// values.
+ (GTMURLBuilder *)builderWithString:(NSString *)URLString;
+ (GTMURLBuilder *)builderWithURL:(NSURL *)URL;

// |URLString| The base URL to which parameters will be appended.
// If the URL already contains parameters, they should already be encoded.
- (id)initWithString:(NSString *)URLString;
- (void)setValue:(NSString *)value forParameter:(NSString *)parameter;
- (void)setIntegerValue:(NSInteger)value forParameter:(NSString *)parameter;
- (NSString *)valueForParameter:(NSString *)parameter;
// Returns 0 if there is no value for |parameter| or if the value cannot
// be parsed into an NSInteger. Use valueForParameter if you want to make
// sure that the value is set before attempting the parsing.
- (NSInteger)integerValueForParameter:(NSString *)parameter;
- (void)removeParameter:(NSString *)parameter;
- (void)setParameters:(NSDictionary *)parameters;
- (NSDictionary *)parameters;
- (NSURL *)URL;
- (NSString *)URLString;

// Case-sensitive comparison of the URL. Also protocol and host are compared
// as case-sensitive strings. The order of URL parameters is ignored.
- (BOOL)isEqual:(GTMURLBuilder *)URLBuilder;

@end
