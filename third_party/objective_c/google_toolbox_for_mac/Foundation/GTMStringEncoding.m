//
//  GTMStringEncoding.m
//
//  Copyright 2009 Google Inc.
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

#import "GTMStringEncoding.h"

NSString *const GTMStringEncodingErrorDomain = @"com.google.GTMStringEncodingErrorDomain";
NSString *const GTMStringEncodingBadCharacterIndexKey = @"GTMStringEncodingBadCharacterIndexKey";

enum {
  kUnknownChar = -1,
  kPaddingChar = -2,
  kIgnoreChar = -3
};

@implementation GTMStringEncoding

+ (id)binaryStringEncoding {
  return [self stringEncodingWithString:@"01"];
}

+ (id)hexStringEncoding {
  GTMStringEncoding *ret = [self stringEncodingWithString:
      @"0123456789ABCDEF"];
  [ret addDecodeSynonyms:@"AaBbCcDdEeFf"];
  return ret;
}

+ (id)rfc4648Base32StringEncoding {
  GTMStringEncoding *ret = [self stringEncodingWithString:
      @"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"];
  [ret setPaddingChar:'='];
  [ret setDoPad:YES];
  return ret;
}

+ (id)rfc4648Base32HexStringEncoding {
  GTMStringEncoding *ret = [self stringEncodingWithString:
      @"0123456789ABCDEFGHIJKLMNOPQRSTUV"];
  [ret setPaddingChar:'='];
  [ret setDoPad:YES];
  return ret;
}

+ (id)crockfordBase32StringEncoding {
  GTMStringEncoding *ret = [self stringEncodingWithString:
      @"0123456789ABCDEFGHJKMNPQRSTVWXYZ"];
  [ret addDecodeSynonyms:
      @"0oO1iIlLAaBbCcDdEeFfGgHhJjKkMmNnPpQqRrSsTtVvWwXxYyZz"];
  return ret;
}

+ (id)rfc4648Base64StringEncoding {
  GTMStringEncoding *ret = [self stringEncodingWithString:
      @"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"];
  [ret setPaddingChar:'='];
  [ret setDoPad:YES];
  return ret;
}

+ (id)rfc4648Base64WebsafeStringEncoding {
  GTMStringEncoding *ret = [self stringEncodingWithString:
      @"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"];
  [ret setPaddingChar:'='];
  [ret setDoPad:YES];
  return ret;
}

GTM_INLINE int lcm(int a, int b) {
  for (int aa = a, bb = b;;) {
    if (aa == bb)
      return aa;
    else if (aa < bb)
      aa += a;
    else
      bb += b;
  }
}

+ (id)stringEncodingWithString:(NSString *)string {
  return [[[self alloc] initWithString:string] autorelease];
}

- (id)initWithString:(NSString *)string {
  if ((self = [super init])) {
    charMapData_ = [[string dataUsingEncoding:NSASCIIStringEncoding] retain];
    if (!charMapData_) {
      _GTMDevLog(@"Unable to convert string to ASCII");
      [self release];
      return nil;
    }
    charMap_ = (char *)[charMapData_ bytes];
    NSUInteger length = [charMapData_ length];
    if (length < 2 || length > 128 || length & (length - 1)) {
      _GTMDevLog(@"Length not a power of 2 between 2 and 128");
      [self release];
      return nil;
    }

    memset(reverseCharMap_, kUnknownChar, sizeof(reverseCharMap_));
    for (unsigned int i = 0; i < length; i++) {
      if (reverseCharMap_[(int)charMap_[i]] != kUnknownChar) {
        _GTMDevLog(@"Duplicate character at pos %d", i);
        [self release];
        return nil;
      }
      reverseCharMap_[(int)charMap_[i]] = i;
    }

    for (NSUInteger i = 1; i < length; i <<= 1)
      shift_++;
    mask_ = (1 << shift_) - 1;
    padLen_ = lcm(8, shift_) / shift_;
  }
  return self;
}

- (void)dealloc {
  [charMapData_ release];
  [super dealloc];
}

- (NSString *)description {
  // TODO(iwade) track synonyms
  return [NSString stringWithFormat:@"<Base%d StringEncoder: %@>",
          1 << shift_, charMapData_];
}

- (void)addDecodeSynonyms:(NSString *)synonyms {
  char *buf = (char *)[synonyms cStringUsingEncoding:NSASCIIStringEncoding];
  int val = kUnknownChar;
  while (*buf) {
    int c = *buf++;
    if (reverseCharMap_[c] == kUnknownChar) {
      reverseCharMap_[c] = val;
    } else {
      val = reverseCharMap_[c];
    }
  }
}

- (void)ignoreCharacters:(NSString *)chars {
  char *buf = (char *)[chars cStringUsingEncoding:NSASCIIStringEncoding];
  while (*buf) {
    int c = *buf++;
    _GTMDevAssert(reverseCharMap_[c] == kUnknownChar,
                  @"Character already mapped");
    reverseCharMap_[c] = kIgnoreChar;
  }
}

- (BOOL)doPad {
  return doPad_;
}

- (void)setDoPad:(BOOL)doPad {
  doPad_ = doPad;
}

- (void)setPaddingChar:(char)c {
  paddingChar_ = c;
  reverseCharMap_[(int)c] = kPaddingChar;
}

- (NSString *)encode:(NSData *)inData {
  return [self encode:inData error:NULL];
}

- (NSString *)encode:(NSData *)inData error:(NSError **)error {
  NSUInteger inLen = [inData length];
  if (inLen <= 0) {
    return @"";
  }
  unsigned char *inBuf = (unsigned char *)[inData bytes];
  NSUInteger inPos = 0;

  NSUInteger outLen = (inLen * 8 + shift_ - 1) / shift_;
  if (doPad_) {
    outLen = ((outLen + padLen_ - 1) / padLen_) * padLen_;
  }
  NSMutableData *outData = [NSMutableData dataWithLength:outLen];
  unsigned char *outBuf = (unsigned char *)[outData mutableBytes];
  NSUInteger outPos = 0;

  unsigned int buffer = inBuf[inPos++];
  int bitsLeft = 8;
  while (bitsLeft > 0 || inPos < inLen) {
    if (bitsLeft < shift_) {
      if (inPos < inLen) {
        buffer <<= 8;
        buffer |= (inBuf[inPos++] & 0xff);
        bitsLeft += 8;
      } else {
        int pad = shift_ - bitsLeft;
        buffer <<= pad;
        bitsLeft += pad;
      }
    }
    int idx = (buffer >> (bitsLeft - shift_)) & mask_;
    bitsLeft -= shift_;
    outBuf[outPos++] = charMap_[idx];
  }

  if (doPad_) {
    while (outPos < outLen)
      outBuf[outPos++] = paddingChar_;
  }

  [outData setLength:outPos];

  NSString *value = [[[NSString alloc] initWithData:outData
                                           encoding:NSASCIIStringEncoding] autorelease];
  if (!value) {
    if (error) {
      *error = [NSError errorWithDomain:GTMStringEncodingErrorDomain
                                   code:GTMStringEncodingErrorUnableToConverToAscii
                               userInfo:nil];

    }
  }
  return value;
}

- (NSString *)encodeString:(NSString *)inString {
  return [self encodeString:inString error:NULL];
}

- (NSString *)encodeString:(NSString *)inString error:(NSError **)error {
  NSData *data = [inString dataUsingEncoding:NSUTF8StringEncoding];
  if (!data) {
    if (error) {
      *error = [NSError errorWithDomain:GTMStringEncodingErrorDomain
                                   code:GTMStringEncodingErrorUnableToConverToUTF8
                               userInfo:nil];

    }
    return nil;
  }
  return [self encode:data error:error];
}

- (NSData *)decode:(NSString *)inString {
  return [self decode:inString error:NULL];
}

- (NSData *)decode:(NSString *)inString error:(NSError **)error {
  char *inBuf = (char *)[inString cStringUsingEncoding:NSASCIIStringEncoding];
  if (!inBuf) {
    if (error) {
      *error = [NSError errorWithDomain:GTMStringEncodingErrorDomain
                                   code:GTMStringEncodingErrorUnableToConverToAscii
                               userInfo:nil];

    }
    return nil;
  }
  NSUInteger inLen = strlen(inBuf);

  NSUInteger outLen = inLen * shift_ / 8;
  NSMutableData *outData = [NSMutableData dataWithLength:outLen];
  unsigned char *outBuf = (unsigned char *)[outData mutableBytes];
  NSUInteger outPos = 0;

  unsigned int buffer = 0;
  int bitsLeft = 0;
  BOOL expectPad = NO;
  for (NSUInteger i = 0; i < inLen; i++) {
    int val = reverseCharMap_[(int)inBuf[i]];
    switch (val) {
      case kIgnoreChar:
        break;
      case kPaddingChar:
        expectPad = YES;
        break;
      case kUnknownChar: {
        if (error) {
          NSDictionary *userInfo =
              [NSDictionary dictionaryWithObject:[NSNumber numberWithUnsignedInteger:i]
                                          forKey:GTMStringEncodingBadCharacterIndexKey];
          *error = [NSError errorWithDomain:GTMStringEncodingErrorDomain
                                       code:GTMStringEncodingErrorUnknownCharacter
                                   userInfo:userInfo];
        }
        return nil;
      }
      default:
        if (expectPad) {
          if (error) {
            NSDictionary *userInfo =
                [NSDictionary dictionaryWithObject:[NSNumber numberWithUnsignedInteger:i]
                                            forKey:GTMStringEncodingBadCharacterIndexKey];
            *error = [NSError errorWithDomain:GTMStringEncodingErrorDomain
                                         code:GTMStringEncodingErrorExpectedPadding
                                     userInfo:userInfo];
          }
          return nil;
        }
        buffer <<= shift_;
        buffer |= val & mask_;
        bitsLeft += shift_;
        if (bitsLeft >= 8) {
          outBuf[outPos++] = (unsigned char)(buffer >> (bitsLeft - 8));
          bitsLeft -= 8;
        }
        break;
    }
  }

  if (bitsLeft && buffer & ((1 << bitsLeft) - 1)) {
    if (error) {
      *error = [NSError errorWithDomain:GTMStringEncodingErrorDomain
                                   code:GTMStringEncodingErrorIncompleteTrailingData
                               userInfo:nil];

    }
    return nil;
  }

  // Shorten buffer if needed due to padding chars
  [outData setLength:outPos];

  return outData;
}

- (NSString *)stringByDecoding:(NSString *)inString {
  return [self stringByDecoding:inString error:NULL];
}

- (NSString *)stringByDecoding:(NSString *)inString error:(NSError **)error {
  NSData *ret = [self decode:inString error:error];
  NSString *value = nil;
  if (ret) {
    value = [[[NSString alloc] initWithData:ret
                                   encoding:NSUTF8StringEncoding] autorelease];
  }
  return value;
}

@end
