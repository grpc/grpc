//
//  GTMStringEncoding.h
//
//  Copyright 2010 Google Inc.
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
//  License for the specific language governing permissions and limitations under
//  the License.
//

#import <Foundation/Foundation.h>
#import "GTMDefines.h"

// A generic class for arbitrary base-2 to 128 string encoding and decoding.
@interface GTMStringEncoding : NSObject {
 @private
  NSData *charMapData_;
  char *charMap_;
  int reverseCharMap_[128];
  int shift_;
  int mask_;
  BOOL doPad_;
  char paddingChar_;
  int padLen_;
}

// Create a new, autoreleased GTMStringEncoding object with a standard encoding.
+ (id)binaryStringEncoding;
+ (id)hexStringEncoding;
+ (id)rfc4648Base32StringEncoding;
+ (id)rfc4648Base32HexStringEncoding;
+ (id)crockfordBase32StringEncoding;
+ (id)rfc4648Base64StringEncoding;
+ (id)rfc4648Base64WebsafeStringEncoding;

// Create a new, autoreleased GTMStringEncoding object with the given string,
// as described below.
+ (id)stringEncodingWithString:(NSString *)string;

// Initialize a new GTMStringEncoding object with the string.
//
// The length of the string must be a power of 2, at least 2 and at most 128.
// Only 7-bit ASCII characters are permitted in the string.
//
// These characters are the canonical set emitted during encoding.
// If the characters have alternatives (e.g. case, easily transposed) then use
// addDecodeSynonyms: to configure them.
- (id)initWithString:(NSString *)string;

// Add decoding synonyms as specified in the synonyms argument.
//
// It should be a sequence of one previously reverse mapped character,
// followed by one or more non-reverse mapped character synonyms.
// Only 7-bit ASCII characters are permitted in the string.
//
// e.g. If a GTMStringEncoder object has already been initialised with a set
// of characters excluding I, L and O (to avoid confusion with digits) and you
// want to accept them as digits you can call addDecodeSynonyms:@"0oO1iIlL".
- (void)addDecodeSynonyms:(NSString *)synonyms;

// A sequence of characters to ignore if they occur during encoding.
// Only 7-bit ASCII characters are permitted in the string.
- (void)ignoreCharacters:(NSString *)chars;

// Indicates whether padding is performed during encoding.
- (BOOL)doPad;
- (void)setDoPad:(BOOL)doPad;

// Sets the padding character to use during encoding.
- (void)setPaddingChar:(char)c;

// Encode a raw binary buffer to a 7-bit ASCII string.
- (NSString *)encode:(NSData *)data __attribute__((deprecated("Use encode:error:")));
- (NSString *)encodeString:(NSString *)string __attribute__((deprecated("Use encodeString:error:")));

- (NSString *)encode:(NSData *)data error:(NSError **)error;
- (NSString *)encodeString:(NSString *)string error:(NSError **)error;

// Decode a 7-bit ASCII string to a raw binary buffer.
- (NSData *)decode:(NSString *)string __attribute__((deprecated("Use decode:error:")));
- (NSString *)stringByDecoding:(NSString *)string __attribute__((deprecated("Use stringByDecoding:error:")));

- (NSData *)decode:(NSString *)string error:(NSError **)error;
- (NSString *)stringByDecoding:(NSString *)string error:(NSError **)error;

@end

FOUNDATION_EXPORT NSString *const GTMStringEncodingErrorDomain;
FOUNDATION_EXPORT NSString *const GTMStringEncodingBadCharacterIndexKey;  // NSNumber

typedef NS_ENUM(NSInteger, GTMStringEncodingError) {
  // Unable to convert a buffer to NSASCIIStringEncoding.
  GTMStringEncodingErrorUnableToConverToAscii = 1024,
  // Unable to convert a buffer to NSUTF8StringEncoding.
  GTMStringEncodingErrorUnableToConverToUTF8,
  // Encountered a bad character.
  // GTMStringEncodingBadCharacterIndexKey will have the index of the character.
  GTMStringEncodingErrorUnknownCharacter,
  // The data had a padding character in the middle of the data. Padding characters
  // can only be at the end.
  GTMStringEncodingErrorExpectedPadding,
  // There is unexpected data at the end of the data that could not be decoded.
  GTMStringEncodingErrorIncompleteTrailingData,
};
