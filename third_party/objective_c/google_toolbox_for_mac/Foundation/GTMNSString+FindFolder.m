//
//  GTMNSString+FindFolder.m
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

#import "GTMNSString+FindFolder.h"
#import "GTMDefines.h"

@implementation NSString (GTMStringFindFolderAdditions)

+ (NSString *)gtm_stringWithPathForFolder:(OSType)theFolderType
                                 inDomain:(short)theDomain
                                 doCreate:(BOOL)doCreate {

  NSString* folderPath = nil;
  FSRef folderRef;

  OSErr err = FSFindFolder(theDomain, theFolderType, doCreate, &folderRef);
  if (err == noErr) {

    CFURLRef folderURL = CFURLCreateFromFSRef(kCFAllocatorSystemDefault,
                                              &folderRef);
    if (folderURL) {
      folderPath = GTMCFAutorelease(CFURLCopyFileSystemPath(folderURL,
                                                            kCFURLPOSIXPathStyle));
      CFRelease(folderURL);
    }
  }
  return folderPath;
}

+ (NSString *)gtm_stringWithPathForFolder:(OSType)theFolderType
                            subfolderName:(NSString *)subfolderName
                                 inDomain:(short)theDomain
                                 doCreate:(BOOL)doCreate {
  NSString *resultPath = nil;
  NSString *subdirPath = nil;
  NSString *parentFolderPath = [self gtm_stringWithPathForFolder:theFolderType
                                                        inDomain:theDomain
                                                        doCreate:doCreate];
  if (parentFolderPath) {

    // find the path to the subdirectory
    subdirPath = [parentFolderPath stringByAppendingPathComponent:subfolderName];

    NSFileManager* fileMgr = [NSFileManager defaultManager];
    BOOL isDir = NO;
    if ([fileMgr fileExistsAtPath:subdirPath isDirectory:&isDir] && isDir) {
      // it already exists
      resultPath = subdirPath;
    } else if (doCreate) {
      parentFolderPath = [parentFolderPath stringByResolvingSymlinksInPath];
      NSDictionary* attrs = nil;
      BOOL createdSubDir = NO;
      // create the subdirectory with the parent folder's attributes
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
      NSError *error = nil;
      attrs = [fileMgr attributesOfItemAtPath:parentFolderPath error:&error];
      if (error) {
        _GTMDevLog(@"Error %@ getting attributes of %@",
                   error, parentFolderPath);
      }
      createdSubDir = [fileMgr createDirectoryAtPath:subdirPath
                         withIntermediateDirectories:YES
                                          attributes:attrs
                                               error:&error];
      if (error) {
        _GTMDevLog(@"Error %@ creating directory at %@", error, subdirPath);
      }
#else
      attrs = [fileMgr fileAttributesAtPath:parentFolderPath traverseLink:YES];
      createdSubDir = [fileMgr createDirectoryAtPath:subdirPath
                                          attributes:attrs];
#endif  // MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
      if (createdSubDir) {
        resultPath = subdirPath;
      }
    }
  }
  return resultPath;
}

@end
