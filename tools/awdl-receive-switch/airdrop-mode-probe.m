#import <Foundation/Foundation.h>

@interface SFAirDropDiscoveryController : NSObject
- (NSInteger)discoverableMode;
@end

int main(void) {
    @autoreleasepool {
        Class cls = NSClassFromString(@"SFAirDropDiscoveryController");
        if (!cls) {
            fprintf(stderr, "SFAirDropDiscoveryController unavailable\n");
            return 2;
        }
        SFAirDropDiscoveryController *controller =
            [[SFAirDropDiscoveryController alloc] init];
        if (!controller) {
            fprintf(stderr, "controller unavailable\n");
            return 3;
        }
        [[NSRunLoop currentRunLoop]
            runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.25]];
        printf("%ld\n", (long)[controller discoverableMode]);
    }
    return 0;
}
