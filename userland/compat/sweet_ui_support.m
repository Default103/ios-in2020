/* Native static-wallpaper recovery and targeted application lifecycle evidence. */
#import <UIKit/UIKit.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static void wallpaper_recover(void)
{
    NSString *marker = @"/private/var/mobile/Library/Preferences/com.in2020.wallpaper-color-requested.plist";
    if ([[NSFileManager defaultManager] fileExistsAtPath:marker]) return;
    NSData *data = [NSData dataWithContentsOfFile:
        @"/Library/Wallpaper/Stills/Any/365.Heritage_Stripe_Green.wallpaper/Wallpaper.plist"];
    NSDictionary *wall = data ? [NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL] : nil;
    if (![wall isKindOfClass:NSDictionary.class]) return;
    NSDictionary *assets = wall[@"assets"][@"lockAndHome"];
    NSString *hex = assets[@"default"][@"backgroundColor"], *darkHex = assets[@"dark"][@"backgroundColor"];
    if (![hex isKindOfClass:NSString.class] || hex.length != 7 || ![hex hasPrefix:@"#"]) return;
    unsigned long long rgb = 0, dark = 0;
    if (![[NSScanner scannerWithString:[hex substringFromIndex:1]] scanHexLongLong:&rgb]) return;
    if ([darkHex isKindOfClass:NSString.class] && darkHex.length == 7)
        (void)[[NSScanner scannerWithString:[darkHex substringFromIndex:1]] scanHexLongLong:&dark];
    dlopen("/System/Library/PrivateFrameworks/SpringBoardUIServices.framework/SpringBoardUIServices", RTLD_NOW);
    Class cls = NSClassFromString(@"SBSUIWallpaperService");
    SEL selector = sel_registerName("setWallpaperColor:darkColor:forLocations:");
    Method method = cls ? class_getClassMethod(cls, selector) : NULL;
    char result[8] = {0}, first[8] = {0}, second[8] = {0}, locations[8] = {0};
    if (method) {
        method_getReturnType(method, result, sizeof(result));
        method_getArgumentType(method, 2, first, sizeof(first));
        method_getArgumentType(method, 3, second, sizeof(second));
        method_getArgumentType(method, 4, locations, sizeof(locations));
    }
    if (!method || method_getNumberOfArguments(method) != 5 || result[0] != 'v' ||
        first[0] != '@' || second[0] != '@' || (locations[0] != 'Q' && locations[0] != 'q')) {
        fprintf(stderr, "sweet-ui: wallpaper service ABI mismatch; no request\n");
        return;
    }
    UIColor *(^color)(unsigned long long) = ^UIColor *(unsigned long long n) {
        return [UIColor colorWithRed:((n >> 16) & 255) / 255.0 green:((n >> 8) & 255) / 255.0
            blue:(n & 255) / 255.0 alpha:1];
    };
    @try {
        ((void (*)(id, SEL, id, id, NSUInteger))objc_msgSend)(cls, selector, color(rgb), color(dark), 3);
        NSDictionary *record = @{@"resource": @365, @"color": hex, @"darkColor": darkHex ?: @"#000000",
            @"requested": @YES, @"visuallyVerified": @NO};
        NSData *recordData = [NSPropertyListSerialization dataWithPropertyList:record
            format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
        BOOL recorded = [recordData writeToFile:marker options:NSDataWritingAtomic error:NULL];
        fprintf(stderr, "sweet-ui: native static wallpaper color requested: %s; marker=%d; visual result pending\n",
            hex.UTF8String, recorded);
    } @catch (NSException *e) {
        fprintf(stderr, "sweet-ui: native wallpaper request failed: %s\n", e.description.UTF8String);
    }
}

void sweet_install_ui_support(const char *name)
{
    fprintf(stderr, "sweet-ui: %s[%d] compatibility loaded; bundle=%s\n", name, getpid(),
        NSBundle.mainBundle.bundleIdentifier.UTF8String ?: "(none)");
    BOOL springboard = !strcmp(name, "SpringBoard");
    if (!springboard && strcmp(name, "Maps") && strcmp(name, "FaceTime")) return;
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    for (NSNotificationName notification in @[UIApplicationDidFinishLaunchingNotification, UIWindowDidBecomeVisibleNotification]) {
        [center addObserverForName:notification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) {
            static unsigned events;
            if (__atomic_fetch_add(&events, 1, __ATOMIC_RELAXED) < 24)
                fprintf(stderr, "sweet-ui: %s event=%s object=%s\n", getprogname(), n.name.UTF8String,
                    NSStringFromClass([n.object class]).UTF8String ?: "nil");
        }];
    }
    char args[1024] = {0}; size_t size = sizeof(args)-1;
    (void)sysctlbyname("kern.bootargs", args, &size, NULL, 0);
    if (springboard && strstr(args, "sweetwall=1"))
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            wallpaper_recover();
        });
}
