//
//  GTMStackTrace.m
//
//  Copyright 2007-2008 Google Inc.
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

#include <stdlib.h>
#include <dlfcn.h>
#include <mach-o/nlist.h>
#include <objc/runtime.h>

#include "GTMStackTrace.h"

struct GTMClassDescription {
  const char *class_name;
  Method *class_methods;
  unsigned int class_method_count;
  Method *instance_methods;
  unsigned int instance_method_count;
};

#pragma mark Private utility functions

static struct GTMClassDescription *GTMClassDescriptions(NSUInteger *total_count) {
  int class_count = objc_getClassList(nil, 0);
  struct GTMClassDescription *class_descs
    = calloc(class_count, sizeof(struct GTMClassDescription));
  if (class_descs) {
    Class *classes = calloc(class_count, sizeof(Class));
    if (classes) {
      objc_getClassList(classes, class_count);
      for (int i = 0; i < class_count; ++i) {
        class_descs[i].class_methods
          = class_copyMethodList(object_getClass(classes[i]),
                                 &class_descs[i].class_method_count);
        class_descs[i].instance_methods
          = class_copyMethodList(classes[i],
                                 &class_descs[i].instance_method_count);
        class_descs[i].class_name = class_getName(classes[i]);
      }
      free(classes);
    } else {
      // COV_NF_START - Don't know how to force this in a unittest
      free(class_descs);
      class_descs = NULL;
      class_count = 0;
      // COV_NF_END
    }
  }
  if (total_count) {
    *total_count = class_count;
  }
  return class_descs;
}

static void GTMFreeClassDescriptions(struct GTMClassDescription *class_descs,
                                     NSUInteger count) {
  if (!class_descs) return;
  for (NSUInteger i = 0; i < count; ++i) {
    if (class_descs[i].instance_methods) {
      free(class_descs[i].instance_methods);
    }
    if (class_descs[i].class_methods) {
      free(class_descs[i].class_methods);
    }
  }
  free(class_descs);
}

static NSUInteger GTMGetStackAddressDescriptorsForAddresses(void *pcs[],
                                                            struct GTMAddressDescriptor outDescs[],
                                                            NSUInteger count) {
  if (count < 1 || !pcs || !outDescs) return 0;

  NSUInteger class_desc_count;

  // Get our obj-c class descriptions. This is expensive, so we do it once
  // at the top. We go through this because dladdr doesn't work with
  // obj methods.
  struct GTMClassDescription *class_descs
    = GTMClassDescriptions(&class_desc_count);
  if (class_descs == NULL) {
    class_desc_count = 0;
  }

  // Iterate through the stack.
  for (NSUInteger i = 0; i < count; ++i) {
    const char *class_name = NULL;
    BOOL is_class_method = NO;
    size_t smallest_diff = SIZE_MAX;
    struct GTMAddressDescriptor *currDesc = &outDescs[i];
    currDesc->address = pcs[i];
    Method best_method = NULL;
    // Iterate through all the classes we know of.
    for (NSUInteger j = 0; j < class_desc_count; ++j) {
      // First check the class methods.
      for (NSUInteger k = 0; k < class_descs[j].class_method_count; ++k) {
        void *imp = (void *)method_getImplementation(class_descs[j].class_methods[k]);
        if (imp <= currDesc->address) {
          size_t diff = (size_t)currDesc->address - (size_t)imp;
          if (diff < smallest_diff) {
            best_method = class_descs[j].class_methods[k];
            class_name = class_descs[j].class_name;
            is_class_method = YES;
            smallest_diff = diff;
          }
        }
      }
      // Then check the instance methods.
      for (NSUInteger k = 0; k < class_descs[j].instance_method_count; ++k) {
        void *imp = (void *)method_getImplementation(class_descs[j].instance_methods[k]);
        if (imp <= currDesc->address) {
          size_t diff = (size_t)currDesc->address - (size_t)imp;
          if (diff < smallest_diff) {
            best_method = class_descs[j].instance_methods[k];
            class_name = class_descs[j].class_name;
            is_class_method = NO;
            smallest_diff = diff;
          }
        }
      }
    }

    // If we have one, store it off.
    if (best_method) {
      currDesc->symbol = sel_getName(method_getName(best_method));
      currDesc->is_class_method = is_class_method;
      currDesc->class_name = class_name;
    }
    Dl_info info = { NULL, NULL, NULL, NULL };

    // Check to see if the one returned by dladdr is better.
    dladdr(currDesc->address, &info);
    if ((size_t)currDesc->address - (size_t)info.dli_saddr < smallest_diff) {
      currDesc->symbol = info.dli_sname;
      currDesc->is_class_method = NO;
      currDesc->class_name = NULL;
    }
    currDesc->filename = info.dli_fname;
    if (!currDesc->symbol) {
      currDesc->symbol = "???";
      currDesc->is_class_method = NO;
      currDesc->class_name = NULL;
    }
  }
  GTMFreeClassDescriptions(class_descs, class_desc_count);
  return count;
}

static NSString *GTMStackTraceFromAddressDescriptors(struct GTMAddressDescriptor descs[],
                                                     NSUInteger count) {
  NSMutableString *trace = [NSMutableString string];

  for (NSUInteger i = 0; i < count; i++) {
    // Newline between all the lines
    if (i) {
      [trace appendString:@"\n"];
    }
    NSString *fileName = nil;
    if (descs[i].filename) {
      fileName = [NSString stringWithCString:descs[i].filename
                                    encoding:NSUTF8StringEncoding];
      fileName = [fileName lastPathComponent];
    } else {
      fileName = @"??";
    }
    if (descs[i].class_name) {
      [trace appendFormat:@"#%-2lu %-35s %#0*lX %s[%s %s]",
       (unsigned long)i,
       [fileName UTF8String],
       // sizeof(void*) * 2 is the length of the hex address (32 vs 64) and + 2
       // for the 0x prefix
       (int)(sizeof(void *) * 2 + 2),
       (unsigned long)descs[i].address,
       (descs[i].is_class_method ? "+" : "-"),
       descs[i].class_name,
       (descs[i].symbol ? descs[i].symbol : "??")];
    } else {
      [trace appendFormat:@"#%-2lu %-35s %#0*lX %s()",
       (unsigned long)i,
       [fileName UTF8String],
       // sizeof(void*) * 2 is the length of the hex address (32 vs 64) and + 2
       // for the 0x prefix
       (int)(sizeof(void *) * 2 + 2),
       (unsigned long)descs[i].address,
       (descs[i].symbol ? descs[i].symbol : "??")];
    }
  }
  return trace;
}

#pragma mark Public functions

NSUInteger GTMGetStackAddressDescriptors(struct GTMAddressDescriptor outDescs[],
                                         NSUInteger count) {
  if (count < 1 || !outDescs) return 0;
  NSUInteger result = 0;
  NSArray *addresses = [NSThread callStackReturnAddresses];
  NSUInteger addrCount = [addresses count];
  if (addrCount) {
    void **pcs = calloc(addrCount, sizeof(void*));
    if (pcs) {
      void **pcsScanner = pcs;
      for (NSNumber *address in addresses) {
        NSUInteger addr = [address unsignedIntegerValue];
        *pcsScanner = (void *)addr;
        ++pcsScanner;
      }
      if (count < addrCount) {
        addrCount = count;
      }
      // Fill in the desc structures
      result = GTMGetStackAddressDescriptorsForAddresses(pcs, outDescs, addrCount);
    }
    free(pcs);
  }

  return result;
}

NSString *GTMStackTrace(void) {
  // If we don't have enough frames, return an empty string
  NSString *result = @"";
  NSArray *addresses = [NSThread callStackReturnAddresses];
  NSUInteger count = [addresses count];
  if (count) {
    void **pcs = calloc(count, sizeof(void*));
    struct GTMAddressDescriptor *descs
      = calloc(count, sizeof(struct GTMAddressDescriptor));
    if (pcs && descs) {
      void **pcsScanner = pcs;
      for (NSNumber *address in addresses) {
        NSUInteger addr = [address unsignedIntegerValue];
        *pcsScanner = (void *)addr;
        ++pcsScanner;
      }
      // Fill in the desc structures
      count = GTMGetStackAddressDescriptorsForAddresses(pcs, descs, count);
      // Build the trace
      // We skip 1 frame because the +[NSThread callStackReturnAddresses] will
      // start w/ this frame.
      const size_t kTracesToStrip = 1;
      if (count > kTracesToStrip) {
        result = GTMStackTraceFromAddressDescriptors(&descs[kTracesToStrip],
                                                     (count - kTracesToStrip));
      }
    }
    free(pcs);
    free(descs);
  }

  return result;
}


NSString *GTMStackTraceFromException(NSException *e) {
  NSString *trace = @"";

  // collect the addresses
  NSArray *addresses = [e callStackReturnAddresses];
  NSUInteger count = [addresses count];
  if (count) {
    void **pcs = calloc(count, sizeof(void*));
    struct GTMAddressDescriptor *descs
      = calloc(count, sizeof(struct GTMAddressDescriptor));
    if (pcs && descs) {
      void **pcsScanner = pcs;
      for (NSNumber *address in addresses) {
        NSUInteger addr = [address unsignedIntegerValue];
        *pcsScanner = (void *)addr;
        ++pcsScanner;
      }
      // Fill in the desc structures
      count = GTMGetStackAddressDescriptorsForAddresses(pcs, descs, count);
      // Build the trace
      trace = GTMStackTraceFromAddressDescriptors(descs, count);
    }
    free(pcs);
    free(descs);
  }

  return trace;
}

