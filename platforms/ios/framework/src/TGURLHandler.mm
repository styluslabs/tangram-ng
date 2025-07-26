//
//  TGURLHandler.mm
//  TangramMap
//
//  Created by Karim Naaji on 11/23/16.
//  Updated by Matt Blair on 7/16/18.
//  Copyright (c) 2017 Mapzen. All rights reserved.
//

#import "TGURLHandler.h"

@interface TGDefaultURLHandler()

@property (strong, nonatomic) NSURLSession* session;

@end

@implementation TGDefaultURLHandler

#pragma mark - Initializers

- (instancetype)init
{
    self = [super init];

    if (self) {
        [self setupWithConfiguration:[TGDefaultURLHandler defaultConfiguration]];
    }

    return self;
}

- (instancetype)initWithConfiguration:(NSURLSessionConfiguration *)configuration
{
    self = [super init];

    if (self) {
        [self setupWithConfiguration:configuration];
    }

    return self;
}

- (void)setupWithConfiguration:(NSURLSessionConfiguration *)configuration {
    self.session = [NSURLSession sessionWithConfiguration:configuration];
}

#pragma mark - Class Methods

+ (NSURLSessionConfiguration*)defaultConfiguration
{
    NSURLSessionConfiguration* configuration = [NSURLSessionConfiguration defaultSessionConfiguration];

    configuration.timeoutIntervalForRequest = 30;
    configuration.URLCache = [[NSURLCache alloc] initWithMemoryCapacity:4*1024*1024
                                                           diskCapacity:30*1024*1024
                                                               diskPath:@"/tangram_cache"];

    return configuration;
}

#pragma mark - Instance Methods

- (NSUInteger)downloadRequestAsync:(NSURL *)url headers:(NSString*)headers payload:(NSData*)payload completionHandler:(TGDownloadCompletionHandler)completionHandler
{
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
    if([payload length] > 0) {
        [request setHTTPMethod:@"POST"];
        [request setHTTPBody:payload];
    }
    if([headers length] > 0) {
        NSArray* hdrs = [headers componentsSeparatedByString:@"\r\n"];
        for (NSString* hdr in hdrs) {
            NSRange colon = [hdr rangeOfString:@": "];
            if(colon.location == NSNotFound) continue;
            NSString* key = [hdr substringToIndex:colon.location];
            NSString* val = [hdr substringFromIndex:colon.location + 2];
            [request setValue:val forHTTPHeaderField:key];
        }
    }

    NSURLSessionDataTask* dataTask = [self.session dataTaskWithRequest:request
                                                 completionHandler:completionHandler];

    [dataTask resume];

    return [dataTask taskIdentifier];
}

- (void)cancelDownloadRequestAsync:(NSUInteger)taskIdentifier
{
    [self.session getTasksWithCompletionHandler:^(NSArray* dataTasks, NSArray* uploadTasks, NSArray* downloadTasks) {
        for (NSURLSessionTask* task in dataTasks) {
            if ([task taskIdentifier] == taskIdentifier || taskIdentifier == (NSUInteger)-1) {
                [task cancel];
                break;
            }
        }
    }];
}

@end
