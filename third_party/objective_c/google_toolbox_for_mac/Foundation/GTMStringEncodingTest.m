//
//  GTMStringEncodingTest.m
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

#import "GTMSenTestCase.h"
#import "GTMStringEncoding.h"

@interface GTMStringEncodingTest : GTMTestCase
@end

@implementation GTMStringEncodingTest

// Empty inputs should result in empty outputs.
- (void)testEmptyInputs {
  GTMStringEncoding *coder = [GTMStringEncoding stringEncodingWithString:@"01"];

  NSError *error = nil;
  XCTAssertEqualStrings([coder encode:[NSData data] error:&error], @"");
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualObjects([coder encodeString:@"" error:&error], @"");
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualObjects([coder decode:@"" error:&error], [NSData data]);
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualStrings([coder stringByDecoding:@"" error:&error], @"");
  XCTAssertNil(error);
}

// Invalid inputs should result in nil outputs.
- (void)testInvalidInputs {
  GTMStringEncoding *coder = [GTMStringEncoding stringEncodingWithString:@"01"];
  NSError *error = nil;

  XCTAssertNil([coder decode:nil error:&error]);
  XCTAssertEqual([error code], GTMStringEncodingErrorUnableToConverToAscii);
  XCTAssertNil([coder decode:@"banana" error:&error]);
  XCTAssertEqual([error code], GTMStringEncodingErrorUnknownCharacter);
  XCTAssertEqualObjects([[error userInfo] objectForKey:GTMStringEncodingBadCharacterIndexKey],
                        [NSNumber numberWithUnsignedInteger:0]);
}

// Ignored inputs should be silently ignored.
- (void)testIgnoreChars {
  GTMStringEncoding *coder = [GTMStringEncoding stringEncodingWithString:@"01"];
  [coder ignoreCharacters:@" \n-"];

  char aa = 0xaa;
  NSData *aaData = [NSData dataWithBytes:&aa length:sizeof(aa)];
  NSError *error = nil;
  XCTAssertEqualObjects([coder decode:@"10101010" error:&error], aaData);
  XCTAssertNil(error);
  error = nil;

  // Inputs with ignored characters
  XCTAssertEqualObjects([coder decode:@"1010 1010" error:&error], aaData);
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualObjects([coder decode:@"1010-1010" error:&error], aaData);
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualObjects([coder decode:@"1010\n1010" error:&error], aaData);
  XCTAssertNil(error);
  error = nil;

  // Invalid inputs
  XCTAssertNil([coder decode:@"1010+1010" error:&error]);
  XCTAssertEqual([error code], GTMStringEncodingErrorUnknownCharacter);
  XCTAssertEqualObjects([[error userInfo] objectForKey:GTMStringEncodingBadCharacterIndexKey],
                        [NSNumber numberWithUnsignedInteger:4]);
}

#define ASSERT_ENCODE_DECODE_STRING(coder, decoded, encoded) do { \
  XCTAssertEqualStrings([coder encodeString:decoded error:&error], encoded); \
  XCTAssertNil(error); \
  error = nil; \
  XCTAssertEqualStrings([coder stringByDecoding:encoded error:&error], decoded); \
  XCTAssertNil(error); \
  error = nil; \
} while (0)

- (void)testBinary {
  GTMStringEncoding *coder = [GTMStringEncoding binaryStringEncoding];
  NSError *error = nil;

  ASSERT_ENCODE_DECODE_STRING(coder, @"", @"");
  ASSERT_ENCODE_DECODE_STRING(coder, @"f", @"01100110");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fo", @"0110011001101111");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foo", @"011001100110111101101111");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foob", @"011001100110111101101111"
                                               "01100010");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fooba", @"011001100110111101101111"
                                                "0110001001100001");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foobar", @"011001100110111101101111"
                                                 "011000100110000101110010");

  // All values, generated with:
  // perl -le 'print unpack "B*", join("", map chr, 0..255)'
  NSString *allValues = @""
  "0000000000000001000000100000001100000100000001010000011000000111000010000000"
  "1001000010100000101100001100000011010000111000001111000100000001000100010010"
  "0001001100010100000101010001011000010111000110000001100100011010000110110001"
  "1100000111010001111000011111001000000010000100100010001000110010010000100101"
  "0010011000100111001010000010100100101010001010110010110000101101001011100010"
  "1111001100000011000100110010001100110011010000110101001101100011011100111000"
  "0011100100111010001110110011110000111101001111100011111101000000010000010100"
  "0010010000110100010001000101010001100100011101001000010010010100101001001011"
  "0100110001001101010011100100111101010000010100010101001001010011010101000101"
  "0101010101100101011101011000010110010101101001011011010111000101110101011110"
  "0101111101100000011000010110001001100011011001000110010101100110011001110110"
  "1000011010010110101001101011011011000110110101101110011011110111000001110001"
  "0111001001110011011101000111010101110110011101110111100001111001011110100111"
  "1011011111000111110101111110011111111000000010000001100000101000001110000100"
  "1000010110000110100001111000100010001001100010101000101110001100100011011000"
  "1110100011111001000010010001100100101001001110010100100101011001011010010111"
  "1001100010011001100110101001101110011100100111011001111010011111101000001010"
  "0001101000101010001110100100101001011010011010100111101010001010100110101010"
  "1010101110101100101011011010111010101111101100001011000110110010101100111011"
  "0100101101011011011010110111101110001011100110111010101110111011110010111101"
  "1011111010111111110000001100000111000010110000111100010011000101110001101100"
  "0111110010001100100111001010110010111100110011001101110011101100111111010000"
  "1101000111010010110100111101010011010101110101101101011111011000110110011101"
  "1010110110111101110011011101110111101101111111100000111000011110001011100011"
  "1110010011100101111001101110011111101000111010011110101011101011111011001110"
  "1101111011101110111111110000111100011111001011110011111101001111010111110110"
  "111101111111100011111001111110101111101111111100111111011111111011111111";
  char allValuesBytes[256];
  for (NSUInteger i = 0; i < sizeof(allValuesBytes); i++)
    allValuesBytes[i] = i;
  NSData *allValuesData = [NSData dataWithBytes:&allValuesBytes
                                         length:sizeof(allValuesBytes)];

  XCTAssertEqualObjects([coder decode:allValues error:&error], allValuesData);
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualStrings([coder encode:allValuesData error:&error], allValues);
  XCTAssertNil(error);
}

- (void)testBase64 {
  // RFC4648 test vectors
  GTMStringEncoding *coder = [GTMStringEncoding rfc4648Base64StringEncoding];
  NSError *error = nil;

  ASSERT_ENCODE_DECODE_STRING(coder, @"", @"");
  ASSERT_ENCODE_DECODE_STRING(coder, @"f", @"Zg==");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fo", @"Zm8=");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foo", @"Zm9v");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foob", @"Zm9vYg==");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fooba", @"Zm9vYmE=");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foobar", @"Zm9vYmFy");

  // All values, generated with:
  // python -c 'import base64; print base64.b64encode("".join([chr(x) for x in range(0, 256)]))'
  NSString *allValues = @""
  "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4"
  "OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3Bx"
  "cnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmq"
  "q6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj"
  "5OXm5+jp6uvs7e7v8PHy8/T19vf4+fr7/P3+/w==";
  char allValuesBytes[256];
  for (NSUInteger i = 0; i < sizeof(allValuesBytes); i++)
    allValuesBytes[i] = i;
  NSData *allValuesData = [NSData dataWithBytes:&allValuesBytes
                                         length:sizeof(allValuesBytes)];

  XCTAssertEqualObjects([coder decode:allValues error:&error], allValuesData);
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualStrings([coder encode:allValuesData error:&error], allValues);
  XCTAssertNil(error);
}

- (void)testBase64Websafe {
  // RFC4648 test vectors
  GTMStringEncoding *coder =
      [GTMStringEncoding rfc4648Base64WebsafeStringEncoding];

  NSError *error = nil;
  ASSERT_ENCODE_DECODE_STRING(coder, @"", @"");
  ASSERT_ENCODE_DECODE_STRING(coder, @"f", @"Zg==");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fo", @"Zm8=");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foo", @"Zm9v");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foob", @"Zm9vYg==");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fooba", @"Zm9vYmE=");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foobar", @"Zm9vYmFy");

  // All values, generated with:
  // python -c 'import base64; print base64.urlsafe_b64encode("".join([chr(x) for x in range(0, 256)]))'
  NSString *allValues = @""
  "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4"
  "OTo7PD0-P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3Bx"
  "cnN0dXZ3eHl6e3x9fn-AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmq"
  "q6ytrq-wsbKztLW2t7i5uru8vb6_wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t_g4eLj"
  "5OXm5-jp6uvs7e7v8PHy8_T19vf4-fr7_P3-_w==";
  char allValuesBytes[256];
  for (NSUInteger i = 0; i < sizeof(allValuesBytes); i++)
    allValuesBytes[i] = i;
  NSData *allValuesData = [NSData dataWithBytes:&allValuesBytes
                                         length:sizeof(allValuesBytes)];

  XCTAssertEqualObjects([coder decode:allValues error:&error], allValuesData);
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualStrings([coder encode:allValuesData error:&error], allValues);
  XCTAssertNil(error);
}

- (void)testBase32 {
  // RFC4648 test vectors
  GTMStringEncoding *coder = [GTMStringEncoding rfc4648Base32StringEncoding];

  NSError *error = nil;
  ASSERT_ENCODE_DECODE_STRING(coder, @"", @"");
  ASSERT_ENCODE_DECODE_STRING(coder, @"f", @"MY======");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fo", @"MZXQ====");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foo", @"MZXW6===");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foob", @"MZXW6YQ=");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fooba", @"MZXW6YTB");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foobar", @"MZXW6YTBOI======");

  // All values, generated with:
  // python -c 'import base64; print base64.b32encode("".join([chr(x) for x in range(0, 256)]))'
  NSString *allValues = @""
  "AAAQEAYEAUDAOCAJBIFQYDIOB4IBCEQTCQKRMFYYDENBWHA5DYPSAIJCEMSCKJRHFAUSUKZMFUXC"
  "6MBRGIZTINJWG44DSOR3HQ6T4P2AIFBEGRCFIZDUQSKKJNGE2TSPKBIVEU2UKVLFOWCZLJNVYXK6"
  "L5QGCYTDMRSWMZ3INFVGW3DNNZXXA4LSON2HK5TXPB4XU634PV7H7AEBQKBYJBMGQ6EITCULRSGY"
  "5D4QSGJJHFEVS2LZRGM2TOOJ3HU7UCQ2FI5EUWTKPKFJVKV2ZLNOV6YLDMVTWS23NN5YXG5LXPF5"
  "X274BQOCYPCMLRWHZDE4VS6MZXHM7UGR2LJ5JVOW27MNTWW33TO55X7A4HROHZHF43T6R2PK5PWO"
  "33XP6DY7F47U6X3PP6HZ7L57Z7P674======";
  char allValuesBytes[256];
  for (NSUInteger i = 0; i < sizeof(allValuesBytes); i++)
    allValuesBytes[i] = i;
  NSData *allValuesData = [NSData dataWithBytes:&allValuesBytes
                                         length:sizeof(allValuesBytes)];

  XCTAssertEqualObjects([coder decode:allValues error:&error], allValuesData);
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualStrings([coder encode:allValuesData error:&error], allValues);
  XCTAssertNil(error);
}

- (void)testBase32Hex {
  // RFC4648 test vectors
  GTMStringEncoding *coder = [GTMStringEncoding rfc4648Base32HexStringEncoding];

  NSError *error = nil;
  ASSERT_ENCODE_DECODE_STRING(coder, @"", @"");
  ASSERT_ENCODE_DECODE_STRING(coder, @"f", @"CO======");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fo", @"CPNG====");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foo", @"CPNMU===");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foob", @"CPNMUOG=");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fooba", @"CPNMUOJ1");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foobar", @"CPNMUOJ1E8======");

  // All values, generated with:
  // python -c 'import base64; print base64.b32encode("".join([chr(x) for x in range(0, 256)]))' | tr A-Z2-7 0-9A-V
  NSString *allValues = @""
  "000G40O40K30E209185GO38E1S8124GJ2GAHC5OO34D1M70T3OFI08924CI2A9H750KIKAPC5KN2"
  "UC1H68PJ8D9M6SS3IEHR7GUJSFQ085146H258P3KGIAA9D64QJIFA18L4KQKALB5EM2PB9DLONAU"
  "BTG62OJ3CHIMCPR8D5L6MR3DDPNN0SBIEDQ7ATJNF1SNKURSFLV7V041GA1O91C6GU48J2KBHI6O"
  "T3SGI699754LIQBPH6CQJEE9R7KVK2GQ58T4KMJAFA59LALQPBDELUOB3CLJMIQRDDTON6TBNF5T"
  "NQVS1GE2OF2CBHM7P34SLIUCPN7CVK6HQB9T9LEMQVCDJMMRRJETTNV0S7HE7P75SRJUHQFATFME"
  "RRNFU3OV5SVKUNRFFU7PVBTVPVFUVS======";
  char allValuesBytes[256];
  for (NSUInteger i = 0; i < sizeof(allValuesBytes); i++)
    allValuesBytes[i] = i;
  NSData *allValuesData = [NSData dataWithBytes:&allValuesBytes
                                         length:sizeof(allValuesBytes)];

  XCTAssertEqualObjects([coder decode:allValues error:&error], allValuesData);
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualStrings([coder encode:allValuesData error:&error], allValues);
  XCTAssertNil(error);
}

- (void)testHex {
  // RFC4648 test vectors
  GTMStringEncoding *coder = [GTMStringEncoding hexStringEncoding];

  NSError *error = nil;
  ASSERT_ENCODE_DECODE_STRING(coder, @"", @"");
  ASSERT_ENCODE_DECODE_STRING(coder, @"f", @"66");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fo", @"666F");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foo", @"666F6F");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foob", @"666F6F62");
  ASSERT_ENCODE_DECODE_STRING(coder, @"fooba", @"666F6F6261");
  ASSERT_ENCODE_DECODE_STRING(coder, @"foobar", @"666F6F626172");

  // All Values, generated with:
  // python -c 'import binascii; print binascii.b2a_hex("".join([chr(x) for x in range(0, 256)])).upper()'
  NSString *allValues = @""
  "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F202122232425"
  "262728292A2B2C2D2E2F303132333435363738393A3B3C3D3E3F404142434445464748494A4B"
  "4C4D4E4F505152535455565758595A5B5C5D5E5F606162636465666768696A6B6C6D6E6F7071"
  "72737475767778797A7B7C7D7E7F808182838485868788898A8B8C8D8E8F9091929394959697"
  "98999A9B9C9D9E9FA0A1A2A3A4A5A6A7A8A9AAABACADAEAFB0B1B2B3B4B5B6B7B8B9BABBBCBD"
  "BEBFC0C1C2C3C4C5C6C7C8C9CACBCCCDCECFD0D1D2D3D4D5D6D7D8D9DADBDCDDDEDFE0E1E2E3"
  "E4E5E6E7E8E9EAEBECEDEEEFF0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF";
  char allValuesBytes[256];
  for (NSUInteger i = 0; i < sizeof(allValuesBytes); i++)
    allValuesBytes[i] = i;
  NSData *allValuesData = [NSData dataWithBytes:&allValuesBytes
                                         length:sizeof(allValuesBytes)];

  XCTAssertEqualObjects([coder decode:allValues error:&error], allValuesData);
  XCTAssertNil(error);
  error = nil;
  XCTAssertEqualStrings([coder encode:allValuesData error:&error], allValues);
  XCTAssertNil(error);
  error = nil;

  // Lower case
  XCTAssertEqualObjects([coder decode:[allValues lowercaseString] error:&error],
                        allValuesData);
  XCTAssertNil(error);
  error = nil;

  // Extra tests from GTMNSData+HexTest.m
  NSString *testString = @"1C2F0032F40123456789ABCDEF";
  char testBytes[] = { 0x1c, 0x2f, 0x00, 0x32, 0xf4, 0x01, 0x23,
                       0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
  NSData *testData = [NSData dataWithBytes:testBytes length:sizeof(testBytes)];
  error = nil;
  XCTAssertEqualStrings([coder encode:testData error:&error], testString);
  XCTAssertEqualObjects([coder decode:testString error:&error], testData);

  // Invalid inputs
  XCTAssertNil([coder decode:@"1c2f003" error:&error]);
  XCTAssertEqual([error code], GTMStringEncodingErrorIncompleteTrailingData);
  XCTAssertNil([coder decode:@"1c2f00ft" error:&error]);
  XCTAssertEqual([error code], GTMStringEncodingErrorUnknownCharacter);
  XCTAssertEqualObjects([[error userInfo] objectForKey:GTMStringEncodingBadCharacterIndexKey],
                        [NSNumber numberWithUnsignedInteger:7]);
  XCTAssertNil([coder decode:@"abcd<C3><A9>f" error:&error]);
  XCTAssertEqual([error code], GTMStringEncodingErrorUnknownCharacter);
  XCTAssertEqualObjects([[error userInfo] objectForKey:GTMStringEncodingBadCharacterIndexKey],
                        [NSNumber numberWithUnsignedInteger:4]);
}

@end
