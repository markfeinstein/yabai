#ifndef MENU_BAR_THUMBNAILS_H
#define MENU_BAR_THUMBNAILS_H

#import <AppKit/AppKit.h>

#define YB_MENU_BAR_THUMBNAIL_TTL_SECONDS 300.0
#define YB_MENU_BAR_THUMBNAIL_FRESH_SECONDS 10.0
#define YB_MENU_BAR_THUMBNAIL_MAX_ENTRIES 16
#define YB_MENU_BAR_THUMBNAIL_MAX_COST (8u * 1024u * 1024u)

@interface YBMenuBarThumbnailCache : NSObject
- (CGImageRef)copyImageForKey:(NSString *)key now:(NSTimeInterval)now age:(NSTimeInterval *)age;
- (BOOL)insertImage:(CGImageRef)image forKey:(NSString *)key now:(NSTimeInterval)now;
- (BOOL)isFreshKey:(NSString *)key now:(NSTimeInterval)now;
- (void)pruneToKeys:(NSSet<NSString *> *)keys now:(NSTimeInterval)now;
- (void)purge;
#if defined(YB_MENU_BAR_TEST)
- (NSUInteger)testEntryCount;
- (size_t)testTotalCost;
#endif
@end

#endif
