//
//  GTMServiceManagement.c
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

// As per http://openp2p.com/pub/a/oreilly/ask_tim/2001/codepolicy.html
// The following functions
// open_devnull
// spc_sanitize_files
// spc_drop_privileges
// are derived from Chapter 1 of "Secure Programming Cookbook for C and C++" by
// John Viega and Matt Messier. Copyright 2003 O'Reilly & Associates.
// ISBN 0-596-00394-3

// Note: launch_data_t have different ownership semantics than CFType/NSObjects.
//       In general if you create one, you are responsible for releasing it.
//       However, if you add it to a collection (LAUNCH_DATA_DICTIONARY,
//       LAUNCH_DATA_ARRAY), you no longer own it, and are no longer
//       responsible for releasing it (you may be responsible for the array
//       or dictionary of course). A corrollary of this is that a
//       launch_data_t can only be in one collection at any given time.

#include "GTMServiceManagement.h"

#if MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_4

#include <CoreServices/CoreServices.h>
#include <paths.h>
#include <unistd.h>
#include <sys/stat.h>
#include <vproc.h>

#if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_10 && \
    MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_8
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMServiceManagement
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

typedef struct {
  CFMutableDictionaryRef dict;
  bool convert_non_standard_objects;
  CFErrorRef *error;
} GTMLToCFDictContext;

typedef struct {
  launch_data_t dict;
  CFErrorRef *error;
} GTMCFToLDictContext;

static bool IsOsYosemiteOrGreater() {
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_10
  // In 10.10, [[NSProcessInfo processInfo] operatingSystemVersion] exists,
  // but if we can assume 10.10 we already know the answer.
  return true;
#elif MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_8
  // Gestalt() is deprected in 10.8, and the recommended replacement is sysctl.
  // https://developer.apple.com/library/mac/releasenotes/General/CarbonCoreDeprecations/index.html#//apple_ref/doc/uid/TP40012224-CH1-SW16
  const size_t N = 128;
  char buffer[N];
  size_t buffer_size = N;
  int ctl_name[] = {CTL_KERN, KERN_OSRELEASE};
  if (sysctl(ctl_name, 2, buffer, &buffer_size, NULL, 0) != 0) {
    return false;
  }
  // The buffer now contains a string of the form XX.YY.ZZ, where
  // XX is the major kernel version component.
  char* period_pos = strchr(buffer, '.');
  if (!period_pos) {
    return false;
  }
  *period_pos = '\0';
  long kernel_version_major = strtol(buffer, NULL, 10);
  // Kernel version 14 corresponds to OS X 10.10 Yosemite.
  return kernel_version_major >= 14;
#else
  SInt32 version_major;
  SInt32 version_minor;
  __Require_noErr(Gestalt(gestaltSystemVersionMajor, &version_major),
      failedGestalt);
  __Require_noErr(Gestalt(gestaltSystemVersionMinor, &version_minor),
      failedGestalt);
  return version_major > 10 || (version_major == 10 && version_minor >= 10);
  failedGestalt:
    return false;
#endif
}

static CFErrorRef GTMCFLaunchCreateUnlocalizedError(CFIndex code,
                                                    CFStringRef format, ...) CF_FORMAT_FUNCTION(2, 3);

static CFErrorRef GTMCFLaunchCreateUnlocalizedError(CFIndex code,
                                                    CFStringRef format, ...) {
  CFDictionaryRef user_info = NULL;
  if (format) {
    va_list args;
    va_start(args, format);
    CFStringRef string
      = CFStringCreateWithFormatAndArguments(kCFAllocatorDefault,
                                             NULL,
                                             format,
                                             args);
    user_info = CFDictionaryCreate(kCFAllocatorDefault,
                                   (const void **)&kCFErrorDescriptionKey,
                                   (const void **)&string,
                                   1,
                                   &kCFTypeDictionaryKeyCallBacks,
                                   &kCFTypeDictionaryValueCallBacks);
    CFRelease(string);
    va_end(args);
  }
  CFErrorRef error = CFErrorCreate(kCFAllocatorDefault,
                                   kCFErrorDomainPOSIX,
                                   code,
                                   user_info);
  if (user_info) {
    CFRelease(user_info);
  }
  return error;
}

static void GTMConvertCFDictEntryToLaunchDataDictEntry(const void *key,
                                                       const void *value,
                                                       void *context) {
  GTMCFToLDictContext *local_context = (GTMCFToLDictContext *)context;
  if (*(local_context->error)) return;

  launch_data_t launch_value
    = GTMLaunchDataCreateFromCFType(value, local_context->error);
  if (launch_value) {
    launch_data_t launch_key
      = GTMLaunchDataCreateFromCFType(key, local_context->error);
    if (launch_key) {
      bool goodInsert
        = launch_data_dict_insert(local_context->dict,
                                  launch_value,
                                  launch_data_get_string(launch_key));
      if (!goodInsert) {
        *(local_context->error)
          = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                              CFSTR("launch_data_dict_insert "
                                                    "failed key: %@ value: %@"),
                                              key,
                                              value);
        launch_data_free(launch_value);
      }
      launch_data_free(launch_key);
    }
  }
}

static void GTMConvertLaunchDataDictEntryToCFDictEntry(const launch_data_t value,
                                                       const char *key,
                                                       void *context) {
  GTMLToCFDictContext *local_context = (GTMLToCFDictContext *)context;
  if (*(local_context->error)) return;

  CFTypeRef cf_value
    = GTMCFTypeCreateFromLaunchData(value,
                                    local_context->convert_non_standard_objects,
                                    local_context->error);
  if (cf_value) {
    CFStringRef cf_key = CFStringCreateWithCString(kCFAllocatorDefault,
                                                   key,
                                                   kCFStringEncodingUTF8);
    if (cf_key) {
      CFDictionarySetValue(local_context->dict, cf_key, cf_value);
      CFRelease(cf_key);
    } else {
      *(local_context->error)
        = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                            CFSTR("Unable to create key %s"),
                                            key);
    }
    CFRelease(cf_value);
  }
}

static launch_data_t GTMPerformOnLabel(const char *verb,
                                       CFStringRef jobLabel,
                                       CFErrorRef *error) {
  launch_data_t resp = NULL;
  launch_data_t label = GTMLaunchDataCreateFromCFType(jobLabel, error);
  if (*error == NULL) {
    launch_data_t msg = launch_data_alloc(LAUNCH_DATA_DICTIONARY);
    launch_data_dict_insert(msg, label, verb);
    resp = launch_msg(msg);
    launch_data_free(msg);
    if (!resp) {
      *error = GTMCFLaunchCreateUnlocalizedError(errno, CFSTR(""));
    }
  }
  return resp;
}

launch_data_t GTMLaunchDataCreateFromCFType(CFTypeRef cf_type_ref,
                                            CFErrorRef *error) {
  launch_data_t result = NULL;
  CFErrorRef local_error = NULL;
  if (cf_type_ref == NULL) {
    local_error = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                                    CFSTR("NULL CFType"));
    goto exit;
  }

  CFTypeID cf_type = CFGetTypeID(cf_type_ref);
  if (cf_type == CFStringGetTypeID()) {
    CFIndex length = CFStringGetLength(cf_type_ref);
    CFIndex max_length
      = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    char *buffer = calloc(max_length, sizeof(char));
    size_t buffer_size = max_length * sizeof(char);
    if (buffer) {
      if (CFStringGetCString(cf_type_ref,
                             buffer,
                             buffer_size,
                             kCFStringEncodingUTF8)) {
        result = launch_data_alloc(LAUNCH_DATA_STRING);
        launch_data_set_string(result, buffer);
      } else {
        local_error
          = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                              CFSTR("CFStringGetCString failed %@"),
                                              cf_type_ref);
      }
      free(buffer);
    } else {
      local_error = GTMCFLaunchCreateUnlocalizedError(ENOMEM,
                                                      CFSTR("calloc of %lu failed"),
                                                      (unsigned long)buffer_size);
    }
  } else if (cf_type == CFBooleanGetTypeID()) {
    result = launch_data_alloc(LAUNCH_DATA_BOOL);
    launch_data_set_bool(result, CFBooleanGetValue(cf_type_ref));
  } else if (cf_type == CFArrayGetTypeID()) {
    CFIndex count = CFArrayGetCount(cf_type_ref);
    result = launch_data_alloc(LAUNCH_DATA_ARRAY);
    for (CFIndex i = 0; i < count; i++) {
      CFTypeRef array_value = CFArrayGetValueAtIndex(cf_type_ref, i);
      if (array_value) {
        launch_data_t launch_value
          = GTMLaunchDataCreateFromCFType(array_value, &local_error);
        if (local_error) break;
        launch_data_array_set_index(result, launch_value, i);
      }
    }
  } else if (cf_type == CFDictionaryGetTypeID()) {
    result = launch_data_alloc(LAUNCH_DATA_DICTIONARY);
    GTMCFToLDictContext context = { result, &local_error };
    CFDictionaryApplyFunction(cf_type_ref,
                              GTMConvertCFDictEntryToLaunchDataDictEntry,
                              &context);
  } else if (cf_type == CFDataGetTypeID()) {
    result = launch_data_alloc(LAUNCH_DATA_OPAQUE);
    launch_data_set_opaque(result,
                           CFDataGetBytePtr(cf_type_ref),
                           CFDataGetLength(cf_type_ref));
  } else if (cf_type == CFNumberGetTypeID()) {
    CFNumberType cf_number_type = CFNumberGetType(cf_type_ref);
    switch (cf_number_type) {
      case kCFNumberSInt8Type:
      case kCFNumberSInt16Type:
      case kCFNumberSInt32Type:
      case kCFNumberSInt64Type:
      case kCFNumberCharType:
      case kCFNumberShortType:
      case kCFNumberIntType:
      case kCFNumberLongType:
      case kCFNumberLongLongType:
      case kCFNumberCFIndexType:
      case kCFNumberNSIntegerType:{
        long long value;
        if (CFNumberGetValue(cf_type_ref, kCFNumberLongLongType, &value)) {
          result = launch_data_alloc(LAUNCH_DATA_INTEGER);
          launch_data_set_integer(result, value);
        } else {
          local_error
            = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                                CFSTR("Unknown to convert: %@"),
                                                cf_type_ref);
        }
        break;
      }

      case kCFNumberFloat32Type:
      case kCFNumberFloat64Type:
      case kCFNumberFloatType:
      case kCFNumberDoubleType:
      case kCFNumberCGFloatType: {
        double value;
        if (CFNumberGetValue(cf_type_ref, kCFNumberDoubleType, &value)) {
          result = launch_data_alloc(LAUNCH_DATA_REAL);
          launch_data_set_real(result, value);
        } else {
          local_error
            = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                                CFSTR("Unknown to convert: %@"),
                                                cf_type_ref);
        }
        break;
      }

      default:
        local_error
          = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                              CFSTR("Unknown CFNumberType %lld"),
                                              (long long)cf_number_type);
        break;
    }
  } else {
    local_error
      = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                          CFSTR("Unknown CFTypeID %lu"),
                                          (unsigned long)cf_type);
  }

exit:
  if (error) {
    *error = local_error;
  } else if (local_error) {
#ifdef DEBUG
    CFShow(local_error);
#endif //  DEBUG
    CFRelease(local_error);
  }
  return result;
}

CFTypeRef GTMCFTypeCreateFromLaunchData(launch_data_t ldata,
                                        bool convert_non_standard_objects,
                                        CFErrorRef *error) {
  CFTypeRef cf_type_ref = NULL;
  CFErrorRef local_error = NULL;
  if (ldata == NULL) {
    local_error = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                                    CFSTR("NULL ldata"));
    goto exit;
  }

  launch_data_type_t ldata_type = launch_data_get_type(ldata);
  switch (ldata_type) {
    case LAUNCH_DATA_STRING:
      cf_type_ref
        = CFStringCreateWithCString(kCFAllocatorDefault,
                                    launch_data_get_string(ldata),
                                    kCFStringEncodingUTF8);
      break;

    case LAUNCH_DATA_INTEGER: {
      long long value = launch_data_get_integer(ldata);
      cf_type_ref = CFNumberCreate(kCFAllocatorDefault,
                                   kCFNumberLongLongType,
                                   &value);
      break;
    }

    case LAUNCH_DATA_REAL: {
      double value = launch_data_get_real(ldata);
      cf_type_ref = CFNumberCreate(kCFAllocatorDefault,
                                   kCFNumberDoubleType,
                                   &value);
      break;
    }

    case LAUNCH_DATA_BOOL: {
      bool value = launch_data_get_bool(ldata);
      cf_type_ref = value ? kCFBooleanTrue : kCFBooleanFalse;
      CFRetain(cf_type_ref);
      break;
    }

    case LAUNCH_DATA_OPAQUE: {
      // Must get the data before we get the size.
      // Otherwise the size will come back faulty on macOS 10.11.6.
      // Radar: 28509492 launch_data_get_opaque_size gives wrong size
      void *data = launch_data_get_opaque(ldata);
      size_t size = launch_data_get_opaque_size(ldata);
      cf_type_ref = CFDataCreate(kCFAllocatorDefault, data, size);
      break;
    }

    case LAUNCH_DATA_ARRAY: {
      size_t count = launch_data_array_get_count(ldata);
      cf_type_ref = CFArrayCreateMutable(kCFAllocatorDefault,
                                         count,
                                         &kCFTypeArrayCallBacks);
      if (cf_type_ref) {
        for (size_t i = 0; !local_error && i < count; i++) {
          launch_data_t l_sub_data = launch_data_array_get_index(ldata, i);
          CFTypeRef cf_sub_type
            = GTMCFTypeCreateFromLaunchData(l_sub_data,
                                            convert_non_standard_objects,
                                            &local_error);
          if (cf_sub_type) {
            CFArrayAppendValue((CFMutableArrayRef)cf_type_ref, cf_sub_type);
            CFRelease(cf_sub_type);
          }
        }
      }
      break;
    }

    case LAUNCH_DATA_DICTIONARY:
      cf_type_ref = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                              0,
                                              &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
      if (cf_type_ref) {
        GTMLToCFDictContext context = {
          (CFMutableDictionaryRef)cf_type_ref,
          convert_non_standard_objects,
          &local_error
        };
        launch_data_dict_iterate(ldata,
                                 GTMConvertLaunchDataDictEntryToCFDictEntry,
                                 &context);
      }
      break;

    case LAUNCH_DATA_FD:
      if (convert_non_standard_objects) {
        int file_descriptor = launch_data_get_fd(ldata);
        cf_type_ref = CFNumberCreate(kCFAllocatorDefault,
                                     kCFNumberIntType,
                                     &file_descriptor);
      }
      break;

    case LAUNCH_DATA_MACHPORT:
      if (convert_non_standard_objects) {
        mach_port_t port = launch_data_get_machport(ldata);
        cf_type_ref = CFNumberCreate(kCFAllocatorDefault,
                                     kCFNumberIntType,
                                     &port);
      }
      break;

    default:
      local_error =
        GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                          CFSTR("Unknown launchd type %d"),
                                          ldata_type);
      break;
  }
exit:
  if (error) {
    *error = local_error;
  } else if (local_error) {
#ifdef DEBUG
    CFShow(local_error);
#endif //  DEBUG
    CFRelease(local_error);
  }
  return cf_type_ref;
}

// open the standard file descs to devnull.
static int open_devnull(int fd) {
  FILE *f = NULL;

  if (fd == STDIN_FILENO) {
    f = freopen(_PATH_DEVNULL, "rb", stdin);
  }
  else if (fd == STDOUT_FILENO) {
    f = freopen(_PATH_DEVNULL, "wb", stdout);
  }
  else if (fd == STDERR_FILENO) {
    f = freopen(_PATH_DEVNULL, "wb", stderr);
  }
  return (f && fileno(f) == fd);
}

void spc_sanitize_files(void) {
  int standard_fds[] = { STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO };
  int standard_fds_count
    = (int)(sizeof(standard_fds) / sizeof(standard_fds[0]));

  // Make sure all open descriptors other than the standard ones are closed
  int fds = getdtablesize();
  for (int i = standard_fds_count; i < fds; ++i) close(i);

  // Verify that the standard descriptors are open.  If they're not, attempt to
  // open them using /dev/null.  If any are unsuccessful, abort.
  for (int i = 0; i < standard_fds_count; ++i) {
    struct stat st;
    int fd = standard_fds[i];
    if (fstat(fd, &st) == -1 && (errno != EBADF || !open_devnull(fd))) {
      abort();
    }
  }
}

void spc_drop_privileges(void) {
  gid_t newgid = getgid(), oldgid = getegid();
  uid_t newuid = getuid(), olduid = geteuid();

  // If root privileges are to be dropped, be sure to pare down the ancillary
  // groups for the process before doing anything else because the setgroups()
  // system call requires root privileges.  Drop ancillary groups regardless of
  // whether privileges are being dropped temporarily or permanently.
  if (!olduid) setgroups(1, &newgid);

  if (newgid != oldgid) {
    if (setregid(-1, newgid) == -1) {
      abort();
    }
  }

  if (newuid != olduid) {
    if (setregid(-1, newuid) == -1) {
      abort();
    }
  }

  // verify that the changes were successful
  if (newgid != oldgid && (setegid(oldgid) != -1 || getegid() != newgid)) {
    abort();
  }
  if (newuid != olduid && (seteuid(olduid) != -1 || geteuid() != newuid)) {
    abort();
  }
}

Boolean GTMSMJobSubmit(CFDictionaryRef cf_job, CFErrorRef *error) {
  // We launch our jobs using launchctl instead of doing it by hand
  // because launchctl does a whole pile of parsing of the job internally
  // to handle the sockets cases that we don't want to duplicate here.
  int fd = -1;
  CFDataRef xmlData = NULL;
  CFErrorRef local_error = NULL;

  if (!cf_job) {
    local_error
      = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                          CFSTR("NULL Job."),
                                          NULL);
    goto exit;
  }

  CFStringRef jobLabel = CFDictionaryGetValue(cf_job,
                                              CFSTR(LAUNCH_JOBKEY_LABEL));
  if (!jobLabel) {
    local_error
      = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                          CFSTR("Job missing label."),
                                          NULL);
    goto exit;
  }

  CFDictionaryRef jobDict = GTMSMJobCopyDictionary(jobLabel);
  if (jobDict) {
    CFRelease(jobDict);
    local_error
      = GTMCFLaunchCreateUnlocalizedError(EEXIST,
                                          CFSTR("Job already exists %@."),
                                          jobLabel);
    goto exit;
  }

  xmlData = CFPropertyListCreateXMLData(NULL, cf_job);
  if (!xmlData) {
    local_error
      = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                          CFSTR("Invalid Job %@."),
                                          jobLabel);
    goto exit;
  }

  char fileName[] = _PATH_TMP "GTMServiceManagement.XXXXXX.plist";
  fd = mkstemps(fileName, 6);
  if (fd == -1) {
    local_error
      = GTMCFLaunchCreateUnlocalizedError(errno,
                                          CFSTR("Unable to create %s."),
                                          fileName);
    goto exit;
  }
  write(fd, CFDataGetBytePtr(xmlData), CFDataGetLength(xmlData));
  close(fd);

  pid_t childpid = fork();
  if (childpid == -1) {
    local_error
      = GTMCFLaunchCreateUnlocalizedError(errno,
                                          CFSTR("Unable to fork."),
                                          NULL);
    goto exit;
  }
  if (childpid != 0) {
    // Parent process
    int status = 0;
    pid_t pid = -1;
    do {
      pid = waitpid(childpid, &status, 0);
    } while (pid == -1 && errno == EINTR);
    if (pid == -1) {
      local_error
        = GTMCFLaunchCreateUnlocalizedError(errno,
                                            CFSTR("waitpid failed."));
        goto exit;
    } else if (WEXITSTATUS(status)) {
      local_error
        = GTMCFLaunchCreateUnlocalizedError(ECHILD,
                                            CFSTR("Child exit status: %d "
                                                  "pid: %d"),
                                            WEXITSTATUS(status), childpid);
      goto exit;
    }
  } else {
    // Child Process
    spc_sanitize_files();
    spc_drop_privileges();
    const char *args[] = { "launchctl", "load", fileName, NULL };
    execve("/bin/launchctl", (char* const*)args, NULL);
    abort();
  }
exit:
  if (xmlData) {
    CFRelease(xmlData);
  }
  if (fd != -1) {
    unlink(fileName);
  }
  if (error) {
    *error = local_error;
  } else if (local_error) {
#ifdef DEBUG
    CFShow(local_error);
#endif //  DEBUG
    CFRelease(local_error);
  }
  return local_error == NULL;
}

CFDictionaryRef GTMSMCopyJobCheckInDictionary(CFErrorRef *error) {
  CFErrorRef local_error = NULL;
  CFDictionaryRef check_in_dict = NULL;
  launch_data_t msg = launch_data_new_string(LAUNCH_KEY_CHECKIN);
  launch_data_t resp = launch_msg(msg);
  launch_data_free(msg);
  if (resp) {
    launch_data_type_t resp_type = launch_data_get_type(resp);
    switch (resp_type) {
      case LAUNCH_DATA_DICTIONARY:
        check_in_dict = GTMCFTypeCreateFromLaunchData(resp, true, &local_error);
        break;

      case LAUNCH_DATA_ERRNO: {
        int e = launch_data_get_errno(resp);
        if (e) {
          local_error = GTMCFLaunchCreateUnlocalizedError(e, CFSTR(""));
        }
        break;
      }

      default:
        local_error
          = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                              CFSTR("unknown response from launchd %d"),
                                              resp_type);
        break;
    }
    launch_data_free(resp);
  } else {
    local_error = GTMCFLaunchCreateUnlocalizedError(errno, CFSTR(""));
  }
  if (error) {
    *error = local_error;
  } else if (local_error) {
#ifdef DEBUG
    CFShow(local_error);
#endif //  DEBUG
    CFRelease(local_error);
  }
  return check_in_dict;
}

Boolean GTMSMJobRemove(CFStringRef jobLabel, CFErrorRef *error) {
  CFErrorRef local_error = NULL;
  launch_data_t resp = GTMPerformOnLabel(LAUNCH_KEY_REMOVEJOB,
                                         jobLabel,
                                         &local_error);
  if (resp) {
    launch_data_type_t resp_type = launch_data_get_type(resp);
    switch (resp_type) {
      case LAUNCH_DATA_ERRNO: {
        int e = launch_data_get_errno(resp);

        // In OSX 10.10+, launch_msg(LAUNCH_KEY_REMOVEJOB, ...) returns the
        // error EINPROGRESS if the job was running at the time it was removed.
        // This should be considered a success as it was on earlier OS versions.
        if (e == EINPROGRESS && IsOsYosemiteOrGreater()) {
          break;
        }

        if (e) {
          local_error = GTMCFLaunchCreateUnlocalizedError(e, CFSTR(""));
        }
        break;
      }

      default:
        local_error
          = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                              CFSTR("unknown response from launchd %d"),
                                              resp_type);
        break;
    }
    launch_data_free(resp);
  }
  if (error) {
    *error = local_error;
  } else if (local_error) {
#ifdef DEBUG
    CFShow(local_error);
#endif //  DEBUG
    CFRelease(local_error);
  }
  return local_error == NULL;
}

CFDictionaryRef GTMSMJobCopyDictionary(CFStringRef jobLabel) {
  CFDictionaryRef dict = NULL;
  CFErrorRef error = NULL;
  launch_data_t resp = GTMPerformOnLabel(LAUNCH_KEY_GETJOB,
                                         jobLabel,
                                         &error);
  if (resp) {
    launch_data_type_t ldata_Type = launch_data_get_type(resp);
    if (ldata_Type == LAUNCH_DATA_DICTIONARY) {
      dict = GTMCFTypeCreateFromLaunchData(resp, true, &error);
    } else {
      error = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                                CFSTR("Unknown launchd type %d"),
                                                ldata_Type);
    }
    launch_data_free(resp);
  }
  if (error) {
#ifdef DEBUG
    CFShow(error);
#endif //  DEBUG
    CFRelease(error);
  }
  return dict;
}

CFDictionaryRef GTMSMCopyAllJobDictionaries(void) {
  CFDictionaryRef dict = NULL;
  launch_data_t msg = launch_data_new_string(LAUNCH_KEY_GETJOBS);
  launch_data_t resp = launch_msg(msg);
  launch_data_free(msg);
  CFErrorRef error = NULL;

  if (resp) {
    launch_data_type_t ldata_Type = launch_data_get_type(resp);
    if (ldata_Type == LAUNCH_DATA_DICTIONARY) {
      dict = GTMCFTypeCreateFromLaunchData(resp, true, &error);
    } else {
      error = GTMCFLaunchCreateUnlocalizedError(EINVAL,
                                                CFSTR("Unknown launchd type %d"),
                                                ldata_Type);
    }
    launch_data_free(resp);
  } else {
    error
      = GTMCFLaunchCreateUnlocalizedError(errno, CFSTR(""));
  }
  if (error) {
#ifdef DEBUG
    CFShow(error);
#endif //  DEBUG
    CFRelease(error);
  }
  return dict;
}

#pragma clang diagnostic pop

#endif //  if MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_4
