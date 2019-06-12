//
//  GTMNSString+FindFolderTest.m
//
//  Copyright 2006-2008 Google Inc.
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
#import "GTMNSString+FindFolder.h"

@interface GTMNSString_FindFolderTest : GTMTestCase
@end

@implementation GTMNSString_FindFolderTest

- (void)testStringWithPathForFolder {
  // for gtm_stringWithPathForFolder:inDomain:doCreate:
  // the parameters all get passed through to FSFindFolder so there's no point testing
  // other combinations; our semantics will match FSFindFolder's
  NSString *prefsPath = [NSString gtm_stringWithPathForFolder:kPreferencesFolderType
                                                     inDomain:kUserDomain
                                                     doCreate:NO];
  NSString *realPrefsPath = [@"~/Library/Preferences" stringByExpandingTildeInPath];
  XCTAssertEqualObjects(realPrefsPath, prefsPath, @"Found incorrect prefs path");


  // test the subfolder method; it should return nil if we pass NO and the
  // subfolder doesn't already exist

  NSString *googCacheNoCreatePath = [NSString gtm_stringWithPathForFolder:kCachedDataFolderType
                                                            subfolderName:@"GTMUnitTestDuzntExist"
                                                                 inDomain:kUserDomain
                                                                 doCreate:NO];
  XCTAssertNil(googCacheNoCreatePath, @"Should not exist: %@", googCacheNoCreatePath);

  // test creating ~/Library/Cache/GTMUnitTestCreated

  NSString *folderName = @"GTMUnitTestCreated";
  NSString *gtmCachePath = [NSString gtm_stringWithPathForFolder:kCachedDataFolderType
                                                   subfolderName:folderName
                                                        inDomain:kUserDomain
                                                        doCreate:YES];
  NSString *testPath = [NSString gtm_stringWithPathForFolder:kCachedDataFolderType
                                                    inDomain:kUserDomain
                                                    doCreate:NO];
  NSString *testPathAppended = [testPath stringByAppendingPathComponent:folderName];
  XCTAssertEqualObjects(gtmCachePath, testPathAppended, @"Unexpected path name");

  NSFileManager* fileMgr = [NSFileManager defaultManager];
  BOOL isDir = NO;
  BOOL pathExists = [fileMgr fileExistsAtPath:gtmCachePath isDirectory:&isDir] && isDir;
  XCTAssertTrue(pathExists, @"Path %@ is not existing like it should", gtmCachePath);

  // test finding it again w/o having to create it
  NSString *gtmCachePath2 = [NSString gtm_stringWithPathForFolder:kCachedDataFolderType
                                                    subfolderName:folderName
                                                         inDomain:kUserDomain
                                                         doCreate:NO];
  XCTAssertEqualObjects(gtmCachePath2, gtmCachePath);

#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
  NSError *error = nil;
  BOOL didRemove = [fileMgr removeItemAtPath:gtmCachePath error:&error];
  XCTAssertTrue(didRemove, @"Error removing %@ (%@)", gtmCachePath, error);
#else
  BOOL didRemove = [fileMgr removeFileAtPath:gtmCachePath
                                     handler:nil];
  XCTAssertTrue(didRemove, @"Error removing %@", gtmCachePath);
#endif  // MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
}

@end
