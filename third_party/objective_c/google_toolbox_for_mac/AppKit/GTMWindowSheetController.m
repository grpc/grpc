//
//  GTMWindowSheetController.m
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

#import "GTMWindowSheetController.h"

#import "GTMDefines.h"
#import "GTMTypeCasting.h"

@interface GTMWSCSheetInfo : NSObject {
 @public
  NSWindow* overlayWindow_;

  // delegate data
  GTM_WEAK id modalDelegate_;
  SEL didEndSelector_;
  void* contextInfo_;

  // sheet info
  CGFloat sheetAlpha_;
  NSRect sheetFrame_; // relative to overlay window
  BOOL sheetAutoresizesSubviews_;
}
@end

@implementation GTMWSCSheetInfo
@end

// The information about how to call up various AppKit-implemented sheets

struct GTMWSCSystemSheetInfo {
  NSString* className_;
  NSString* methodSignature_;
  NSUInteger modalForWindowIndex_;
  NSUInteger modalDelegateIndex_;
  NSUInteger didEndSelectorIndex_;
  NSUInteger contextInfoIndex_;
  // Callbacks invariably take three parameters. The first is always an id, the
  // third always a void*, but the second can be a BOOL (8 bits), an int (32
  // bits), or an id or NSInteger (64 bits in 64 bit mode). This is the size of
  // the argument in 64-bit mode.
  NSUInteger arg1OfEndSelectorSize_;
};

@interface GTMWindowSheetController (PrivateMethods)
- (void)beginSystemSheet:(id)systemSheet
                withInfo:(const struct GTMWSCSystemSheetInfo*)info
            modalForView:(NSView*)view
          withParameters:(NSArray*)params;
- (const struct GTMWSCSystemSheetInfo*)infoForSheet:(id)systemSheet;
- (void)notificationHappened:(NSNotification*)notification;
- (void)viewDidChangeSize:(NSView*)view;
- (NSRect)screenFrameOfView:(NSView*)view;
- (void)sheetDidEnd:(id)sheet
        returnCode8:(char)returnCode
        contextInfo:(void*)contextInfo;
- (void)sheetDidEnd:(id)sheet
       returnCode32:(int)returnCode
        contextInfo:(void*)contextInfo;
- (void)sheetDidEnd:(id)sheet
       returnCode64:(NSInteger)returnCode
        contextInfo:(void*)contextInfo;
- (void)sheetDidEnd:(id)sheet
         returnCode:(NSInteger)returnCode
        contextInfo:(void*)contextInfo
           arg1Size:(int)size;
- (void)systemRequestsVisibilityForWindow:(NSWindow*)window;
- (NSRect)window:(NSWindow*)window
willPositionSheet:(NSWindow*)sheet
       usingRect:(NSRect)defaultSheetRect;
@end

@interface GTMWSCOverlayWindow : NSWindow {
  GTMWindowSheetController* sheetController_;
}

- (id)initWithContentRect:(NSRect)contentRect
          sheetController:(GTMWindowSheetController*)sheetController;
- (void)makeKeyAndOrderFront:(id)sender;

@end

@implementation GTMWSCOverlayWindow

- (id)initWithContentRect:(NSRect)contentRect
          sheetController:(GTMWindowSheetController*)sheetController {
  self = [super initWithContentRect:contentRect
                          styleMask:NSBorderlessWindowMask
                            backing:NSBackingStoreBuffered
                              defer:NO];
  if (self != nil) {
    sheetController_ = sheetController;
    [self setOpaque:NO];
    [self setBackgroundColor:[NSColor clearColor]];
    [self setIgnoresMouseEvents:NO];
  }
  return self;
}

- (void)makeKeyAndOrderFront:(id)sender {
  [sheetController_ systemRequestsVisibilityForWindow:self];
}

@end

@implementation GTMWindowSheetController

- (id)initWithWindow:(NSWindow*)window
            delegate:(id <GTMWindowSheetControllerDelegate>)delegate {
  self = [super init];
  if (self != nil) {
    window_ = window;
    delegate_ = delegate;
    sheets_ = [[NSMutableDictionary alloc] init];
  }
  return self;
}

- (void)dealloc {
  _GTMDevAssert([sheets_ count] == 0,
                @"Deallocing a controller with sheets still active!");
  [[NSNotificationCenter defaultCenter] removeObserver:self];

  [sheets_ release];

  [super dealloc];
}

- (void)beginSheet:(NSWindow*)sheet
      modalForView:(NSView*)view
     modalDelegate:(id)modalDelegate
    didEndSelector:(SEL)didEndSelector
       contextInfo:(void*)contextInfo {
  NSArray* params =
      [NSArray arrayWithObjects:sheet,
                                [NSNull null],
                                modalDelegate,
                                [NSValue valueWithPointer:didEndSelector],
                                [NSValue valueWithPointer:contextInfo],
                                nil];
  [self beginSystemSheet:[NSApplication sharedApplication]
            modalForView:view
          withParameters:params];
}

- (void)beginSystemSheet:(id)systemSheet
            modalForView:(NSView*)view
          withParameters:(NSArray*)params {
  const struct GTMWSCSystemSheetInfo* info = [self infoForSheet:systemSheet];
  if (info) {
    [self beginSystemSheet:systemSheet
                  withInfo:info
              modalForView:view
            withParameters:params];
  } // else already logged
}


- (BOOL)isSheetAttachedToView:(NSView*)view {
  NSValue* viewValue = [NSValue valueWithNonretainedObject:view];
  return [sheets_ objectForKey:viewValue] != nil;
}

- (NSArray*)viewsWithAttachedSheets {
  NSMutableArray* views = [NSMutableArray array];
  NSValue* key;
  for (key in sheets_) {
    [views addObject:[key nonretainedObjectValue]];
  }

  return views;
}

- (void)setActiveView:(NSView*)view {
  // Hide old sheet

  NSValue* oldViewValue = [NSValue valueWithNonretainedObject:activeView_];
  GTMWSCSheetInfo* oldSheetInfo = [sheets_ objectForKey:oldViewValue];
  if (oldSheetInfo) {
    NSWindow* overlayWindow = oldSheetInfo->overlayWindow_;
    _GTMDevAssert(overlayWindow, @"Old sheet info has no overlay window");
    NSWindow* sheetWindow = [overlayWindow attachedSheet];
    _GTMDevAssert(sheetWindow, @"Old sheet info has no active sheet");

    // Why do we hide things this way?
    // - Keeping it local but alpha 0 means we get good Expose behavior
    // - Resizing it to 0 means we get no blurring effect left over

    oldSheetInfo->sheetAlpha_ = [sheetWindow alphaValue];
    [sheetWindow setAlphaValue:(CGFloat)0.0];

    oldSheetInfo->sheetAutoresizesSubviews_ =
        [[sheetWindow contentView] autoresizesSubviews];
    [[sheetWindow contentView] setAutoresizesSubviews:NO];

    NSRect overlayFrame = [overlayWindow frame];
    oldSheetInfo->sheetFrame_ = [sheetWindow frame];
    oldSheetInfo->sheetFrame_.origin.x -= overlayFrame.origin.x;
    oldSheetInfo->sheetFrame_.origin.y -= overlayFrame.origin.y;
    [sheetWindow setFrame:NSZeroRect display:NO];

    [overlayWindow setIgnoresMouseEvents:YES];

    // Make sure the now invisible sheet doesn't keep keyboard focus
    [[overlayWindow parentWindow] makeKeyWindow];
  }

  activeView_ = view;

  // Show new sheet

  NSValue* newViewValue = [NSValue valueWithNonretainedObject:view];
  GTMWSCSheetInfo* newSheetInfo = [sheets_ objectForKey:newViewValue];
  if (newSheetInfo) {
    NSWindow* overlayWindow = newSheetInfo->overlayWindow_;
    _GTMDevAssert(overlayWindow, @"New sheet info has no overlay window");
    NSWindow* sheetWindow = [overlayWindow attachedSheet];
    _GTMDevAssert(sheetWindow, @"New sheet info has no active sheet");

    [overlayWindow setIgnoresMouseEvents:NO];

    NSRect overlayFrame = [overlayWindow frame];
    newSheetInfo->sheetFrame_.origin.x += overlayFrame.origin.x;
    newSheetInfo->sheetFrame_.origin.y += overlayFrame.origin.y;
    [sheetWindow setFrame:newSheetInfo->sheetFrame_ display:NO];

    [[sheetWindow contentView]
        setAutoresizesSubviews:newSheetInfo->sheetAutoresizesSubviews_];

    [sheetWindow setAlphaValue:newSheetInfo->sheetAlpha_];

    [self viewDidChangeSize:view];

    [overlayWindow makeKeyWindow];
  }
}

@end

@implementation GTMWindowSheetController (PrivateMethods)

- (void)beginSystemSheet:(id)systemSheet
                withInfo:(const struct GTMWSCSystemSheetInfo*)info
            modalForView:(NSView*)view
          withParameters:(NSArray*)params {
  _GTMDevAssert([view window] == window_,
                @"Cannot show a sheet for a window for which we are not "
                @"managing sheets");
  _GTMDevAssert(![self isSheetAttachedToView:view],
                @"Cannot show another sheet for a view while already managing "
                @"one");
  _GTMDevAssert(info, @"Missing info for the type of sheet");

  GTMWSCSheetInfo* sheetInfo = [[[GTMWSCSheetInfo alloc] init] autorelease];

  sheetInfo->modalDelegate_ = [params objectAtIndex:info->modalDelegateIndex_];
  sheetInfo->didEndSelector_ =
      [[params objectAtIndex:info->didEndSelectorIndex_] pointerValue];
  sheetInfo->contextInfo_ =
      [[params objectAtIndex:info->contextInfoIndex_] pointerValue];

  _GTMDevAssert([sheetInfo->modalDelegate_
                  respondsToSelector:sheetInfo->didEndSelector_],
                @"Delegate does not respond to the specified selector");

  [view setPostsFrameChangedNotifications:YES];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(notificationHappened:)
             name:NSViewFrameDidChangeNotification
           object:view];

  sheetInfo->overlayWindow_ =
      [[GTMWSCOverlayWindow alloc]
       initWithContentRect:[self screenFrameOfView:view]
           sheetController:self];

  [sheets_ setObject:sheetInfo
              forKey:[NSValue valueWithNonretainedObject:view]];

  [window_ addChildWindow:sheetInfo->overlayWindow_
                  ordered:NSWindowAbove];

  SEL methodSelector = NSSelectorFromString((NSString*)info->methodSignature_);
  NSInvocation* invocation =
      [NSInvocation invocationWithMethodSignature:
       [systemSheet methodSignatureForSelector:methodSelector]];
  [invocation setSelector:methodSelector];
  for (NSUInteger i = 0; i < [params count]; ++i) {
    // Remember that args 0 and 1 are the target and selector, thus the |i+2|s
    if (i == info->modalForWindowIndex_) {
      [invocation setArgument:&sheetInfo->overlayWindow_ atIndex:i+2];
    } else if (i == info->modalDelegateIndex_) {
      [invocation setArgument:&self atIndex:i+2];
    } else if (i == info->didEndSelectorIndex_) {
      if (info->arg1OfEndSelectorSize_ == 64)
        [invocation setArgument:&@selector(sheetDidEnd:returnCode64:contextInfo:)
                        atIndex:i+2];
      else if (info->arg1OfEndSelectorSize_ == 32)
        [invocation setArgument:&@selector(sheetDidEnd:returnCode32:contextInfo:)
                        atIndex:i+2];
      else if (info->arg1OfEndSelectorSize_ == 8)
        [invocation setArgument:&@selector(sheetDidEnd:returnCode8:contextInfo:)
                        atIndex:i+2];
    } else if (i == info->contextInfoIndex_) {
      [invocation setArgument:&view atIndex:i+2];
    } else {
      id param = [params objectAtIndex:i];
      if ([param isKindOfClass:[NSValue class]]) {
        char buffer[16];
        [param getValue:buffer];
        [invocation setArgument:buffer atIndex:i+2];
      } else {
        [invocation setArgument:&param atIndex:i+2];
      }
    }
  }
  [invocation invokeWithTarget:systemSheet];

  _GTMDevAssert(!activeView_ || activeView_ == view,
                @"You have to call setActiveView:view before "
                 "calling beginSheet:modalForView:view");
  activeView_ = view;
}

- (const struct GTMWSCSystemSheetInfo*)infoForSheet:(id)systemSheet {
  static const struct GTMWSCSystemSheetInfo kGTMWSCSystemSheetInfoData[] =
  {
    {
      @"ABIdentityPicker",
      @"beginSheetModalForWindow:modalDelegate:didEndSelector:contextInfo:",
      0, 1, 2, 3, 64,
    },
    {
      @"CBIdentityPicker",
      @"runModalForWindow:modalDelegate:didEndSelector:contextInfo:",
      0, 1, 2, 3, 64,
    },
    {
      @"DRSetupPanel",
      @"beginSetupSheetForWindow:modalDelegate:didEndSelector:contextInfo:",
      0, 1, 2, 3, 32,
    },
    {
      @"NSAlert",
      @"beginSheetModalForWindow:modalDelegate:didEndSelector:contextInfo:",
      0, 1, 2, 3, 32,
    },
    {
      @"NSApplication",
      @"beginSheet:modalForWindow:modalDelegate:didEndSelector:contextInfo:",
      1, 2, 3, 4, 64,
    },
    {
      @"IKFilterBrowserPanel",
      @"beginSheetWithOptions:modalForWindow:modalDelegate:didEndSelector:contextInfo:",
      1, 2, 3, 4, 32,
    },
    {
      @"IKPictureTaker",
      @"beginPictureTakerSheetForWindow:withDelegate:didEndSelector:contextInfo:",
      0, 1, 2, 3, 64,
    },
    {
      @"IOBluetoothDeviceSelectorController",
      @"beginSheetModalForWindow:modalDelegate:didEndSelector:contextInfo:",
      0, 1, 2, 3, 32,
    },
    {
      @"IOBluetoothObjectPushUIController",
      @"beginSheetModalForWindow:modalDelegate:didEndSelector:contextInfo:",
      0, 1, 2, 3, 32,
    },
    {
      @"IOBluetoothServiceBrowserController",
      @"beginSheetModalForWindow:modalDelegate:didEndSelector:contextInfo:",
      0, 1, 2, 3, 32,
    },
    {
      @"NSOpenPanel",
      @"beginSheetForDirectory:file:types:modalForWindow:modalDelegate:didEndSelector:contextInfo:",
      3, 4, 5, 6, 32,
    },
    {
      @"NSPageLayout",
      @"beginSheetWithPrintInfo:modalForWindow:delegate:didEndSelector:contextInfo:",
      1, 2, 3, 4, 32,
    },
    {
      @"NSPrintOperation",
      @"runOperationModalForWindow:delegate:didRunSelector:contextInfo:",
      0, 1, 2, 3, 8,
    },
    {
      @"NSPrintPanel",
      @"beginSheetWithPrintInfo:modalForWindow:delegate:didEndSelector:contextInfo:",
      1, 2, 3, 4, 32,
    },
    {
      @"NSSavePanel",
      @"beginSheetForDirectory:file:modalForWindow:modalDelegate:didEndSelector:contextInfo:",
      2, 3, 4, 5, 32,
    },
    {
      @"SFCertificatePanel",
      @"beginSheetForWindow:modalDelegate:didEndSelector:contextInfo:certificates:showGroup:",
      0, 1, 2, 3, 32,
    },
    {
      @"SFCertificateTrustPanel",
      @"beginSheetForWindow:modalDelegate:didEndSelector:contextInfo:trust:message:",
      0, 1, 2, 3, 32,
    },
    {
      @"SFChooseIdentityPanel",
      @"beginSheetForWindow:modalDelegate:didEndSelector:contextInfo:identities:message:",
      0, 1, 2, 3, 32,
    },
    {
      @"SFKeychainSettingsPanel",
      @"beginSheetForWindow:modalDelegate:didEndSelector:contextInfo:settings:keychain:",
      0, 1, 2, 3, 32,
    },
    {
      @"SFKeychainSavePanel",
      @"beginSheetForDirectory:file:modalForWindow:modalDelegate:didEndSelector:contextInfo:",
      2, 3, 4, 5, 32,
    },
  };

  static const size_t kGTMWSCSystemSheetInfoDataSize =
      sizeof(kGTMWSCSystemSheetInfoData)/sizeof(kGTMWSCSystemSheetInfoData[0]);

  for (size_t i = 0; i < kGTMWSCSystemSheetInfoDataSize; ++i) {
    Class testClass =
      NSClassFromString(kGTMWSCSystemSheetInfoData[i].className_);
    if (testClass && [systemSheet isKindOfClass:testClass]) {
      return &kGTMWSCSystemSheetInfoData[i];
    }
  }

  _GTMDevLog(@"Failed to find info for sheet of type %@", [systemSheet class]);
  return nil;
}

- (void)notificationHappened:(NSNotification*)notification {
  NSView *view = GTM_STATIC_CAST(NSView, [notification object]);
  [self viewDidChangeSize:view];
}

- (void)viewDidChangeSize:(NSView*)view {
  GTMWSCSheetInfo* sheetInfo =
      [sheets_ objectForKey:[NSValue valueWithNonretainedObject:view]];
  if (!sheetInfo)
    return;

  if (view != activeView_)
    return;

  NSWindow* overlayWindow = sheetInfo->overlayWindow_;
  if (!overlayWindow)
    return;

  [overlayWindow setFrame:[self screenFrameOfView:view] display:YES];
  [[overlayWindow attachedSheet] makeKeyWindow];
}

- (NSRect)screenFrameOfView:(NSView*)view {
  NSRect viewFrame = [view convertRect:[view bounds] toView:nil];
  viewFrame = [[view window] convertRectToScreen:viewFrame];
  return viewFrame;
}

- (void)sheetDidEnd:(id)sheet
        returnCode8:(char)returnCode
        contextInfo:(void*)contextInfo {
  [self sheetDidEnd:sheet
         returnCode:returnCode
        contextInfo:contextInfo
           arg1Size:8];
}

- (void)sheetDidEnd:(id)sheet
       returnCode32:(int)returnCode
        contextInfo:(void*)contextInfo {
  [self sheetDidEnd:sheet
         returnCode:returnCode
        contextInfo:contextInfo
           arg1Size:32];
}

- (void)sheetDidEnd:(id)sheet
       returnCode64:(NSInteger)returnCode
        contextInfo:(void*)contextInfo {
  [self sheetDidEnd:sheet
         returnCode:returnCode
        contextInfo:contextInfo
           arg1Size:64];
}

- (void)sheetDidEnd:(id)sheet
         returnCode:(NSInteger)returnCode
        contextInfo:(void*)contextInfo
           arg1Size:(int)size {
  NSValue* viewKey = [NSValue valueWithNonretainedObject:(NSView*)contextInfo];
  // Retain a reference to sheetInfo so we can use it after it is
  // removed from sheets_.
  GTMWSCSheetInfo* sheetInfo =
      [[[sheets_ objectForKey:viewKey] retain] autorelease];
  _GTMDevAssert(sheetInfo, @"Could not find information about the sheet that "
                           @"just ended");
  _GTMDevAssert(size == 8 || size == 32 || size == 64,
                @"Incorrect size information in the sheet entry; don't know "
                @"how big the second parameter is");

  // Can't turn off view's frame notifications as we don't know if someone else
  // wants them.
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:NSViewFrameDidChangeNotification
              object:contextInfo];

  // We clean up the sheet before calling the callback so that the
  // callback is free to fire another sheet if it so desires.
  [window_ removeChildWindow:sheetInfo->overlayWindow_];
  [sheetInfo->overlayWindow_ release];
  [sheets_ removeObjectForKey:viewKey];

  NSInvocation* invocation =
      [NSInvocation invocationWithMethodSignature:
       [sheetInfo->modalDelegate_
        methodSignatureForSelector:sheetInfo->didEndSelector_]];
  [invocation setSelector:sheetInfo->didEndSelector_];
  // Remember that args 0 and 1 are the target and selector
  [invocation setArgument:&sheet atIndex:2];
  if (size == 64) {
    [invocation setArgument:&returnCode atIndex:3];
  } else if (size == 32) {
    int shortReturnCode = (int)returnCode;
    [invocation setArgument:&shortReturnCode atIndex:3];
  } else if (size == 8) {
    char charReturnCode = returnCode;
    [invocation setArgument:&charReturnCode atIndex:3];
  }
  [invocation setArgument:&sheetInfo->contextInfo_ atIndex:4];
  [invocation invokeWithTarget:sheetInfo->modalDelegate_];
}

- (void)systemRequestsVisibilityForWindow:(NSWindow*)window {
  NSValue* key;
  for (key in sheets_) {
    GTMWSCSheetInfo* sheetInfo = [sheets_ objectForKey:key];
    if (sheetInfo->overlayWindow_ == window) {
      NSView* view = [key nonretainedObjectValue];
      [delegate_ gtm_systemRequestsVisibilityForView:view];
    }
  }
}

- (NSRect)window:(NSWindow*)window
willPositionSheet:(NSWindow*)sheet
       usingRect:(NSRect)defaultSheetRect {
  // Ensure that the sheets come out of the very top of the overlay windows.
  NSRect windowFrame = [window frame];
  defaultSheetRect.origin.y = windowFrame.size.height;
  return defaultSheetRect;
}

@end
