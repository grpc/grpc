//
//  GTMNSString+FindFolder.h
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

#import <Foundation/Foundation.h>

@interface NSString (GTMStringFindFolderAdditions)

// Create a path to a folder located with FindFolder
//
// Args:
//   theFolderType: one of the folder types in Folders.h
//                  (kPreferencesFolderType, etc)
//   theDomain: one of the domains in Folders.h (kLocalDomain, kUserDomain, etc)
//   doCreate: create the folder if it does not already exist
//
// Returns:
//   full path to folder, or nil if the folder doesn't exist or can't be created
//
+ (NSString *)gtm_stringWithPathForFolder:(OSType)theFolderType
                                 inDomain:(short)theDomain
                                 doCreate:(BOOL)doCreate;

// Create a path to a folder inside a folder located with FindFolder
//
// Args:
//   theFolderType: one of the folder types in Folders.h
//                  (kPreferencesFolderType, etc)
//   subfolderName: name of directory inside the Apple folder to be located or created
//   theDomain: one of the domains in Folders.h (kLocalDomain, kUserDomain, etc)
//   doCreate: create the folder if it does not already exist
//
// Returns:
//   full path to subdirectory, or nil if the folder doesn't exist or can't be created
//
+ (NSString *)gtm_stringWithPathForFolder:(OSType)theFolderType
                            subfolderName:(NSString *)subfolderName
                                 inDomain:(short)theDomain
                                 doCreate:(BOOL)doCreate;

@end
