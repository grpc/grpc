//
//  GTMWindowSheetController.h
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

#import <Cocoa/Cocoa.h>
#import "GTMDefines.h"

// A class to manage multiple sheets for a window. Use it for tab-style
// interfaces, where each tab might need its own sheet.
//
// While Cocoa can send notifications for when views resize, it does not do so
// for views appearing/disappearing. The owner is responsible for calling
// -setActiveView: appropriately as the visible views change.
//
// Notes on usage:
// - Cocoa isn't used to sheets being (ab)used in the way we use them here and
//   makes sure we know it by providing slight visual anomalies like showing the
//   close box as disabled but not actually disabling it, and not showing
//   shadows for sheets. That's something you'll have to live with.
// - YOU are responsible for making sure that all sheets are closed before
//   the windows containing them closes. That means:
//   - You MUST implement the window delegate method -windowShouldClose: for any
//     window using this class. In it, call -viewsWithAttachedSheets to see if
//     there are any views with sheets attached to them. If there are, switch to
//     that view and do not allow the window to close.
//   - You MUST implement GTMWindowSheetControllerDelegate's method
//     -gtm_systemRequestsVisibilityForView:. When that method is called, the
//     system is trying to quit but realizes that there is a sheet on a window
//     that prevents it from doing so. In such a case, switch to that view.
//     (The quit is already prevented from happening so you don't need to worry
//     about it.)
//   - You MUST implement the application delegate method
//     -applicationShouldTerminate:. In it, for every window that might have a
//     sheet, call -viewsWithAttachedSheets to see if there are any views with
//     sheets attached to them. If there are, switch to that view and do not
//     allow the application to quit.
//   I hope you see a pattern here.

@protocol GTMWindowSheetControllerDelegate
- (void)gtm_systemRequestsVisibilityForView:(NSView*)view;
@end


@interface GTMWindowSheetController : NSObject {
 @private
  GTM_WEAK NSWindow* window_;
  GTM_WEAK NSView* activeView_;
  GTM_WEAK id <GTMWindowSheetControllerDelegate> delegate_;

  NSMutableDictionary* sheets_;  // NSValue*(NSView*) -> SheetInfo*
}

// Initializes the class for use.
//
// Args:
//     window: The window for which to manage sheets. All views must be
//             contained by this window.
//   delegate: The delegate for this sheet controller.
//
- (id)initWithWindow:(NSWindow*)window
            delegate:(id <GTMWindowSheetControllerDelegate>)delegate;

// Starts a view modal session for a sheet. Intentionally similar to
// -[NSApplication
//    beginSheet:modalForWindow:modalDelegate:didEndSelector:contextInfo:].
// You must only call this method if the currently active view is |view| or
// |nil|; this means you can call this method only after creating the
// GTMWindowSheetController or after calling -setActiveView:view.
//
// Args:
//            sheet: The window object representing the sheet you want to
//                   display.
//             view: The view object to which you want to attach the sheet.
//    modalDelegate: The delegate object that defines your didEndSelector
//                   method.
//   didEndSelector: The method on the modalDelegate that will be called when
//                   the sheet’s modal session has ended. This method must be
//                   defined on the object in the modalDelegate parameter and
//                   have the following signature:
//                     - (void)sheetDidEnd:(NSWindow *)sheet
//                              returnCode:(NSInteger)returnCode
//                             contextInfo:(void *)contextInfo;
//      contextInfo: A pointer to the context info you want passed to the
//                   didEndSelector method when the sheet’s modal session ends.
//
- (void)beginSheet:(NSWindow*)sheet
      modalForView:(NSView*)view
     modalDelegate:(id)modalDelegate
    didEndSelector:(SEL)didEndSelector
       contextInfo:(void *)contextInfo;

// Starts a view modal session for a system sheet. Just about any AppKit class
// that has an instance method named something like -beginSheetModalForWindow...
// will work with this method.
//
// Args:
//     systemSheet: The object that will show a sheet when triggered
//                  appropriately.
//            view: The view object to which you want to attach the sheet.
//   modalDelegate: The delegate object that defines your didEndSelector
//                  method.
//          params: The parameters of the -beginSheetModalForWindow... selector.
//                  For the parameter named "window", insert [NSNull null] into
//                  the array instead.
//
- (void)beginSystemSheet:(id)systemSheet
            modalForView:(NSView*)view
          withParameters:(NSArray*)params;

// Returns a BOOL value indicating whether the specified view has a sheet
// attached to it (hidden or not).
//
//  Args:
//    view: The view object to which a sheet might be attached.
//
//  Returns:
//    Whether or not a sheet is indeed attached to that view.
//
- (BOOL)isSheetAttachedToView:(NSView*)view;

// Returns a list of views that have sheets attached (hidden or not).
//
//  Returns:
//    An array of views that have sheets.
//
- (NSArray*)viewsWithAttachedSheets;

// Sets the specified view as active. The sheet (if there is one) for the active
// view is shown; sheets for all other views are hidden.
//
//  Args:
//    view: The view object to which a sheet is attached.
//
- (void)setActiveView:(NSView*)view;
@end
