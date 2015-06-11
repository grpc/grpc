//
//  main.m
//  HelloWorld
//
//  Created by Michael Lumish on 6/11/15.
//  Copyright (c) 2015 Google. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"

#import <HelloWorld/Helloworld.pbrpc.h>

static NSString * const kHostAddress = @"http://localhost:50051";

int main(int argc, char * argv[]) {
  @autoreleasepool {
    HLWGreeter *client = [[HLWGreeter alloc] initWithHost:kHostAddress];
    HLWHelloRequest *request = [HLWHelloRequest message];
    request.name = @"Objective C";
    [client sayHelloWithRequest:request handler:^(HLWHelloReply *response, NSError *error) {
      NSLog(@"%@", response.message);
    }];
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
  }
}
