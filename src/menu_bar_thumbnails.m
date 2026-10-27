#import "menu_bar_thumbnails.h"
#include <float.h>
#include <stdint.h>

@interface YBMenuBarThumbnailEntry : NSObject {
@public
    CGImageRef image;
    NSTimeInterval capturedAt;
    NSTimeInterval lastAccess;
    size_t cost;
}
@end

@implementation YBMenuBarThumbnailEntry
- (void)dealloc
{
    if (image) CGImageRelease(image);
    [super dealloc];
}
@end

@implementation YBMenuBarThumbnailCache {
    NSMutableDictionary *_entries;
    size_t _totalCost;
}

- (id)init
{
    self = [super init];
    if (self) _entries = [[NSMutableDictionary alloc] init];
    return self;
}

- (void)dealloc
{
    [_entries release];
    [super dealloc];
}

- (void)removeKey:(NSString *)key
{
    YBMenuBarThumbnailEntry *entry = [_entries objectForKey:key];
    if (!entry) return;
    _totalCost -= entry->cost;
    [_entries removeObjectForKey:key];
}

- (void)pruneExpired:(NSTimeInterval)now
{
    NSArray *keys = [[_entries allKeys] copy];
    for (NSString *key in keys) {
        YBMenuBarThumbnailEntry *entry = [_entries objectForKey:key];
        if (!entry || now - entry->capturedAt >= YB_MENU_BAR_THUMBNAIL_TTL_SECONDS) [self removeKey:key];
    }
    [keys release];
}

- (void)evictIfNeeded
{
    while ([_entries count] > YB_MENU_BAR_THUMBNAIL_MAX_ENTRIES ||
           _totalCost > YB_MENU_BAR_THUMBNAIL_MAX_COST) {
        NSString *oldestKey = nil;
        NSTimeInterval oldest = DBL_MAX;
        for (NSString *key in _entries) {
            YBMenuBarThumbnailEntry *entry = [_entries objectForKey:key];
            if (entry->lastAccess < oldest) { oldest = entry->lastAccess; oldestKey = key; }
        }
        if (!oldestKey) break;
        [self removeKey:oldestKey];
    }
}

- (CGImageRef)copyImageForKey:(NSString *)key now:(NSTimeInterval)now age:(NSTimeInterval *)age
{
    if (!key) return NULL;
    [self pruneExpired:now];
    YBMenuBarThumbnailEntry *entry = [_entries objectForKey:key];
    if (!entry || !entry->image) return NULL;
    entry->lastAccess = now;
    if (age) *age = MAX(0.0, now - entry->capturedAt);
    return CGImageRetain(entry->image);
}

- (BOOL)isFreshKey:(NSString *)key now:(NSTimeInterval)now
{
    NSTimeInterval age = 0;
    CGImageRef image = [self copyImageForKey:key now:now age:&age];
    if (image) CGImageRelease(image);
    return image && age < YB_MENU_BAR_THUMBNAIL_FRESH_SECONDS;
}

- (BOOL)insertImage:(CGImageRef)newImage forKey:(NSString *)key now:(NSTimeInterval)now
{
    if (!newImage || !key || ![key length]) return NO;
    size_t bytesPerRow = CGImageGetBytesPerRow(newImage);
    size_t height = CGImageGetHeight(newImage);
    if (height && bytesPerRow > SIZE_MAX / height) return NO;
    size_t cost = bytesPerRow * height;
    if (!cost || cost > YB_MENU_BAR_THUMBNAIL_MAX_COST) return NO;
    [self pruneExpired:now];
    [self removeKey:key];
    YBMenuBarThumbnailEntry *entry = [[[YBMenuBarThumbnailEntry alloc] init] autorelease];
    entry->image = CGImageRetain(newImage);
    entry->capturedAt = now;
    entry->lastAccess = now;
    entry->cost = cost;
    [_entries setObject:entry forKey:key];
    _totalCost += cost;
    [self evictIfNeeded];
    return [_entries objectForKey:key] == entry;
}

- (void)pruneToKeys:(NSSet<NSString *> *)keys now:(NSTimeInterval)now
{
    [self pruneExpired:now];
    if (!keys) return;
    NSArray *existing = [[_entries allKeys] copy];
    for (NSString *key in existing) {
        if (![keys containsObject:key]) [self removeKey:key];
    }
    [existing release];
}

- (void)purge
{
    [_entries removeAllObjects];
    _totalCost = 0;
}

#if defined(YB_MENU_BAR_TEST)
- (NSUInteger)testEntryCount { return [_entries count]; }
- (size_t)testTotalCost { return _totalCost; }
#endif
@end
