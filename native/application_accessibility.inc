#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <cstring>
#include <string>
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

// Screen-reader announcements for the application host (included from
// application_input_events.inc after application_clipboard.inc, whose UTF-8
// check it reuses). A control is spoken as "label, value, role, N of M". On
// macOS the text is posted to the window as an NSAccessibility announcement
// request, which VoiceOver speaks when it is running; elsewhere the request
// is refused with UNSUPPORTED. The last composed text is kept for the smoke.

#if defined(__APPLE__)
// AppKit's C entry point and keys; NSString constants are toll-free CFStrings.
extern "C" void NSAccessibilityPostNotificationWithUserInfo(void* element, CFStringRef notification, CFDictionaryRef user_info);
extern "C" const CFStringRef NSAccessibilityAnnouncementRequestedNotification;
extern "C" const CFStringRef NSAccessibilityAnnouncementKey;
extern "C" const CFStringRef NSAccessibilityPriorityKey;
#endif

namespace {

constexpr uint32_t kAnnouncementPartBytes = 256;

std::string& last_announcement() {
    static std::string text;
    return text;
}

// Appends a NUL-terminated UTF-8 part with a ", " separator; empty parts are
// skipped. False for an overlong or malformed part.
bool append_part(std::string& out, const char* part) {
    if (part == nullptr) return true;
    const size_t length = strnlen(part, kAnnouncementPartBytes + 1);
    if (length > kAnnouncementPartBytes) return false;
    if (length == 0) return true;
    if (!clipboard_text_valid(reinterpret_cast<const uint8_t*>(part), static_cast<uint32_t>(length))) return false;
    if (!out.empty()) out += ", ";
    out.append(part, length);
    return true;
}

int32_t post_announcement(SDL_Window* window, const std::string& text, int32_t priority) {
#if defined(__APPLE__)
    void* ns_window = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (ns_window == nullptr) return ELISA_APPLICATION_UNSUPPORTED;
    CFStringRef spoken = CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
        static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false);
    if (spoken == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    // NSAccessibilityPriorityLevel: low 10, medium 50, high 90.
    const int level = priority >= 2 ? 90 : (priority == 1 ? 50 : 10);
    CFNumberRef rank = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &level);
    const void* keys[] = {NSAccessibilityAnnouncementKey, NSAccessibilityPriorityKey};
    const void* values[] = {spoken, rank};
    CFDictionaryRef info = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    NSAccessibilityPostNotificationWithUserInfo(ns_window, NSAccessibilityAnnouncementRequestedNotification, info);
    CFRelease(info);
    CFRelease(rank);
    CFRelease(spoken);
    return ELISA_APPLICATION_OK;
#else
    (void)window; (void)text; (void)priority;
    return ELISA_APPLICATION_UNSUPPORTED;
#endif
}

}  // namespace

extern "C" int32_t elisa_application_v1_announce_control(const char* label, const char* value, const char* role,
    int32_t position, int32_t count, int32_t priority) {
    if (label == nullptr || label[0] == 0) return ELISA_APPLICATION_INVALID_ARGUMENT;
    if (count < 0 || position < 0 || position > count || (count == 0) != (position == 0)) return ELISA_APPLICATION_INVALID_ARGUMENT;
    if (priority < 0 || priority > 2) return ELISA_APPLICATION_INVALID_ARGUMENT;
    std::string text;
    if (!append_part(text, label) || !append_part(text, value) || !append_part(text, role)) return ELISA_APPLICATION_INVALID_ARGUMENT;
    if (count > 0) text += ", " + std::to_string(position) + " of " + std::to_string(count);
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    if (service.host.window() == nullptr) return ELISA_APPLICATION_INVALID_STATE;
    const int32_t status = post_announcement(service.host.window(), text, priority);
    if (status == ELISA_APPLICATION_OK) last_announcement() = text;
    return status;
}

// Copies the last posted announcement; QUEUE_FULL when `capacity` is short.
extern "C" int32_t elisa_application_v1_last_announcement(uint8_t* out, uint32_t capacity, uint32_t* length) {
    if (length == nullptr || (out == nullptr && capacity != 0)) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    const std::string& text = last_announcement();
    *length = static_cast<uint32_t>(text.size());
    if (text.size() > capacity) return ELISA_APPLICATION_QUEUE_FULL;
    if (!text.empty()) std::memcpy(out, text.data(), text.size());
    return ELISA_APPLICATION_OK;
}
