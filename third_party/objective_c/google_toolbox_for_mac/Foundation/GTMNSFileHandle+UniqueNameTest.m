//
//  GTMNSFileHandle+UniqueNameTest.m
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
#import "GTMNSFileHandle+UniqueName.h"

@interface GTMNSFileHandle_UniqueNameTest : GTMTestCase
@end

@implementation GTMNSFileHandle_UniqueNameTest

- (void)testFileHandleWithUniqueNameBasedOnFinalPath {
  NSFileHandle *handle
    = [NSFileHandle gtm_fileHandleWithUniqueNameBasedOn:nil
                                              finalPath:nil];
  XCTAssertNil(handle);

  // Try and create a file where we shouldn't be able to.
  NSString *path = nil;
  handle = [NSFileHandle gtm_fileHandleWithUniqueNameBasedOn:@"/System/HappyXXX.txt"
                                                   finalPath:&path];
  XCTAssertNil(handle);
  XCTAssertNil(path);

  NSFileManager *fm = [NSFileManager defaultManager];
  NSString *tempDir
    = [fm gtm_createTemporaryDirectoryBasedOn:@"GTMNSFileHandle_UniqueNameTestXXXXXX"];
  XCTAssertNotNil(tempDir);
  BOOL isDirectory = NO;
  XCTAssertTrue([fm fileExistsAtPath:tempDir isDirectory:&isDirectory] && isDirectory);

  // Test with extension
  handle = [NSFileHandle gtm_fileHandleWithUniqueNameBasedOn:@"HappyXXX.txt"
                                                 inDirectory:tempDir
                                                   finalPath:&path];
  XCTAssertNotNil(handle);
  XCTAssertEqualObjects([path pathExtension], @"txt");
  XCTAssertTrue([fm fileExistsAtPath:path]);

  // Test without extension
  handle = [NSFileHandle gtm_fileHandleWithUniqueNameBasedOn:@"HappyXXX"
                                                 inDirectory:tempDir
                                                   finalPath:&path];
  XCTAssertNotNil(handle);
  XCTAssertEqualObjects([path pathExtension], @"");
  XCTAssertTrue([fm fileExistsAtPath:path]);

  // Test passing in same name twice
  NSString *fullPath = [tempDir stringByAppendingPathComponent:@"HappyXXX"];
  NSString *newPath = nil;
  handle = [NSFileHandle gtm_fileHandleWithUniqueNameBasedOn:fullPath
                                                   finalPath:&newPath];
  XCTAssertNotNil(handle);
  XCTAssertNotNil(newPath);
  XCTAssertNotEqualObjects(path, newPath);
  XCTAssertTrue([fm fileExistsAtPath:newPath]);

  // Test passing in same name twice with no template
  fullPath = [tempDir stringByAppendingPathComponent:@"Sad"];
  newPath = nil;
  handle = [NSFileHandle gtm_fileHandleWithUniqueNameBasedOn:fullPath
                                                   finalPath:&newPath];
  XCTAssertNotNil(handle);
  XCTAssertNotNil(newPath);

  newPath = nil;
  handle = [NSFileHandle gtm_fileHandleWithUniqueNameBasedOn:fullPath
                                                   finalPath:&newPath];
  XCTAssertNil(handle);
  XCTAssertNil(newPath);

  [fm removeItemAtPath:tempDir error:nil];
}

- (void)testFileHandleWithUniqueNameBasedOnInDirectorySearchMaskFinalPath {
  NSFileManager *fm = [NSFileManager defaultManager];
  NSString *path = nil;
  NSFileHandle *handle
    = [NSFileHandle gtm_fileHandleWithUniqueNameBasedOn:nil
                                            inDirectory:NSCachesDirectory
                                             domainMask:NSUserDomainMask
                                              finalPath:&path];
  XCTAssertNil(handle);
  XCTAssertNil(path);

  handle  = [NSFileHandle gtm_fileHandleWithUniqueNameBasedOn:@"HappyXXX.txt"
                                                  inDirectory:NSCachesDirectory
                                                   domainMask:NSUserDomainMask
                                                    finalPath:&path];
  XCTAssertNotNil(handle);
  XCTAssertNotNil(path);
  XCTAssertTrue([fm fileExistsAtPath:path]);
  [fm removeItemAtPath:path error:nil];
}

@end

@interface GTMNSFileManager_UniqueNameTest : GTMTestCase
@end

@implementation GTMNSFileManager_UniqueNameTest

- (void)testCreateDirectoryWithUniqueNameBasedOn {
  NSFileManager *fm = [NSFileManager defaultManager];
  NSString *path
    = [fm gtm_createDirectoryWithUniqueNameBasedOn:@"/System/HappyXXX.txt"];
  XCTAssertNil(path);
}

- (void)testCreateDirectoryWithUniqueNameBasedOnInDirectorySearchMask {
  NSFileManager *fm = [NSFileManager defaultManager];
  NSString *path = [fm gtm_createDirectoryWithUniqueNameBasedOn:nil
                                                    inDirectory:NSCachesDirectory
                                                     domainMask:NSUserDomainMask];
  XCTAssertNil(path);

  path = [fm gtm_createDirectoryWithUniqueNameBasedOn:@"HappyXXX.txt"
                                          inDirectory:NSCachesDirectory
                                           domainMask:NSUserDomainMask];
  XCTAssertNotNil(path);
  BOOL isDirectory = NO;
  XCTAssertTrue([fm fileExistsAtPath:path isDirectory:&isDirectory] && isDirectory);
  NSError *error;
  XCTAssertTrue([fm removeItemAtPath:path error:&error], "%@", error);
}

@end

