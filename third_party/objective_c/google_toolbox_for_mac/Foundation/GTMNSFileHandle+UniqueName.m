//
//  GTMNSFileHandle+UniqueName.m
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

#import "GTMNSFileHandle+UniqueName.h"

#include <unistd.h>

// Export a nonsense symbol to suppress a libtool warning when this is linked alone in a static lib.
__attribute__((visibility("default")))
    char GTMFileHandleUniqueNameExportToSuppressLibToolWarning = 0;


@implementation NSFileHandle (GTMFileHandleUniqueNameAdditions)

+ (id)gtm_fileHandleWithUniqueNameBasedOn:(NSString *)pathTemplate
                                finalPath:(NSString **)path {
  if (!pathTemplate) return nil;
  NSString *extension = [pathTemplate pathExtension];
  char *pathTemplateCString = strdup([pathTemplate fileSystemRepresentation]);
  if (!pathTemplateCString) return nil;
  int len = (int)[extension length];
  if (len > 0) {
    // Suffix length needs to include a period.
    len++;
  }
  int fileDescriptor = mkstemps(pathTemplateCString, len);
  if (fileDescriptor == -1) {
    free(pathTemplateCString);
    return nil;
  }
  NSFileHandle *handle
    = [[[NSFileHandle alloc] initWithFileDescriptor:fileDescriptor
                                     closeOnDealloc:YES] autorelease];
  if (handle && path) {
    *path = [NSString stringWithUTF8String:pathTemplateCString];
  }
  free(pathTemplateCString);
  return handle;
}

+ (id)gtm_fileHandleWithUniqueNameBasedOn:(NSString *)nameTemplate
                              inDirectory:(NSString *)directory
                                finalPath:(NSString **)path {
  NSString *fullPath = [directory stringByAppendingPathComponent:nameTemplate];
  return [self gtm_fileHandleWithUniqueNameBasedOn:fullPath finalPath:path];
}

+ (id)gtm_fileHandleWithUniqueNameBasedOn:(NSString *)nameTemplate
                              inDirectory:(NSSearchPathDirectory)directory
                               domainMask:(NSSearchPathDomainMask)mask
                                finalPath:(NSString **)path {
  NSArray *searchPaths = NSSearchPathForDirectoriesInDomains(directory,
                                                             mask,
                                                             YES);
  if ([searchPaths count] == 0) return nil;
  NSString *searchPath = [searchPaths objectAtIndex:0];
  return [self gtm_fileHandleWithUniqueNameBasedOn:nameTemplate
                                       inDirectory:searchPath
                                         finalPath:path];
}

+ (id)gtm_fileHandleForTemporaryFileBasedOn:(NSString *)nameTemplate
                                  finalPath:(NSString **)path {
  return [self gtm_fileHandleWithUniqueNameBasedOn:nameTemplate
                                       inDirectory:NSTemporaryDirectory()
                                         finalPath:path];
}

@end

@implementation NSFileManager (GTMFileManagerUniqueNameAdditions)

- (NSString *)gtm_createDirectoryWithUniqueNameBasedOn:(NSString *)pathTemplate {
  if (!pathTemplate) return nil;
  char *pathTemplateCString = strdup([pathTemplate fileSystemRepresentation]);
  if (!pathTemplateCString) return nil;
  char *outCName = mkdtemp(pathTemplateCString);
  NSString *outName = outCName ? [NSString stringWithUTF8String:outCName] : nil;
  free(pathTemplateCString);
  return outName;
}

- (NSString *)gtm_createDirectoryWithUniqueNameBasedOn:(NSString *)nameTemplate
                                       inDirectory:(NSString *)directory {
  NSString *fullPath = [directory stringByAppendingPathComponent:nameTemplate];
  return [self gtm_createDirectoryWithUniqueNameBasedOn:fullPath];
}

- (NSString *)gtm_createDirectoryWithUniqueNameBasedOn:(NSString *)nameTemplate
                                           inDirectory:(NSSearchPathDirectory)directory
                                            domainMask:(NSSearchPathDomainMask)mask {
  NSArray *searchPaths = NSSearchPathForDirectoriesInDomains(directory,
                                                             mask,
                                                             YES);
  if ([searchPaths count] == 0) return nil;
  NSString *searchPath = [searchPaths objectAtIndex:0];
  return [self gtm_createDirectoryWithUniqueNameBasedOn:nameTemplate
                                            inDirectory:searchPath];
}

- (NSString *)gtm_createTemporaryDirectoryBasedOn:(NSString *)nameTemplate {
  return [self gtm_createDirectoryWithUniqueNameBasedOn:nameTemplate
                                            inDirectory:NSTemporaryDirectory()];
}

@end
