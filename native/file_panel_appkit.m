// Native open/save panels for AppKit hosts (NSOpenPanel / NSSavePanel).
// Modal on the main thread: the call returns once the person picks a file or
// cancels. Returned paths are NUL-terminated UTF-8 owned by the engine and
// never freed, so a caller may keep them as long as it likes (a path is a few
// hundred bytes per pick). NULL means cancelled, refused or not on the main
// thread. Link with -framework Cocoa -framework UniformTypeIdentifiers.
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <stdint.h>
#include <string.h>

// "glb;txt" -> content types; unknown extensions fall back to a dynamic type
// for that extension so the filter still applies.
static NSArray<UTType*>* elisa_panel_types(const char* extensions) {
    if (extensions == NULL || extensions[0] == 0) return nil;
    NSString* list = [NSString stringWithUTF8String:extensions];
    if (list == nil) return nil;
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (NSString* part in [list componentsSeparatedByString:@";"]) {
        if (part.length == 0) continue;
        UTType* type = [UTType typeWithFilenameExtension:part];
        if (type != nil) [types addObject:type];
    }
    return types.count ? types : nil;
}

static const char* elisa_panel_path(NSURL* url) {
    if (url == nil || !url.isFileURL) return NULL;
    const char* text = url.fileSystemRepresentation;
    if (text == NULL) return NULL;
    return strdup(text);
}

static void elisa_panel_prepare(NSSavePanel* panel, const char* extensions, const char* title) {
    NSArray<UTType*>* types = elisa_panel_types(extensions);
    if (types != nil) panel.allowedContentTypes = types;
    if (title != NULL && title[0] != 0) {
        NSString* text = [NSString stringWithUTF8String:title];
        if (text != nil) panel.message = text;
    }
}

const char* elisa_file_panel_open(const char* extensions, const char* title) {
    if (![NSThread isMainThread]) return NULL;
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        elisa_panel_prepare(panel, extensions, title);
        if ([panel runModal] != NSModalResponseOK) return NULL;
        return elisa_panel_path(panel.URLs.firstObject);
    }
}

// `suggested` may be a bare name or a full path; a full path also picks the
// starting folder.
const char* elisa_file_panel_save(const char* extensions, const char* suggested, const char* title) {
    if (![NSThread isMainThread]) return NULL;
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.canCreateDirectories = YES;
        elisa_panel_prepare(panel, extensions, title);
        if (suggested != NULL && suggested[0] != 0) {
            NSString* text = [NSString stringWithUTF8String:suggested];
            if (text != nil) {
                NSString* folder = text.stringByDeletingLastPathComponent;
                if (folder.length != 0) {
                    BOOL dir = NO;
                    NSString* full = folder.stringByStandardizingPath;
                    if ([[NSFileManager defaultManager] fileExistsAtPath:full isDirectory:&dir] && dir)
                        panel.directoryURL = [NSURL fileURLWithPath:full isDirectory:YES];
                }
                panel.nameFieldStringValue = text.lastPathComponent;
            }
        }
        if ([panel runModal] != NSModalResponseOK) return NULL;
        return elisa_panel_path(panel.URL);
    }
}

// Adds a file to the system's recent documents (Dock menu, File > Open
// Recent when the host has one). Ignored off the main thread.
void elisa_file_panel_note_recent(const char* path) {
    if (path == NULL || ![NSThread isMainThread]) return;
    @autoreleasepool {
        NSString* text = [NSString stringWithUTF8String:path];
        if (text == nil) return;
        [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL:[NSURL fileURLWithPath:text]];
    }
}
