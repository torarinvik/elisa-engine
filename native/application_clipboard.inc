#include <SDL3/SDL_clipboard.h>
#include <cstring>
#include <string>

// Plain-text clipboard for the application host (included from
// application_input_events.inc). Text crosses the ABI as UTF-8 bytes with an
// explicit length; SDL owns the platform pasteboard.

namespace {

// Rejects malformed or overlong UTF-8, surrogates and embedded NUL bytes.
bool clipboard_text_valid(const uint8_t* text, uint32_t length) {
    uint32_t i = 0;
    while (i < length) {
        const uint8_t lead = text[i];
        if (lead == 0) return false;
        uint32_t extra = 0;
        uint32_t code = 0;
        if (lead < 0x80) { i++; continue; }
        if (lead >= 0xC2 && lead <= 0xDF) { extra = 1; code = lead & 0x1F; }
        else if (lead >= 0xE0 && lead <= 0xEF) { extra = 2; code = lead & 0x0F; }
        else if (lead >= 0xF0 && lead <= 0xF4) { extra = 3; code = lead & 0x07; }
        else return false;
        if (length - i <= extra) return false;
        for (uint32_t k = 1; k <= extra; k++) {
            const uint8_t next = text[i + k];
            if ((next & 0xC0) != 0x80) return false;
            code = (code << 6) | (next & 0x3F);
        }
        if ((extra == 2 && code < 0x800) || (extra == 3 && (code < 0x10000 || code > 0x10FFFF)) ||
            (code >= 0xD800 && code <= 0xDFFF)) return false;
        i += extra + 1;
    }
    return true;
}

}  // namespace

extern "C" int32_t elisa_application_v1_set_clipboard_text(const uint8_t* text, uint32_t length) {
    if (text == nullptr && length != 0) return ELISA_APPLICATION_INVALID_ARGUMENT;
    if (length != 0 && !clipboard_text_valid(text, length)) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    const std::string owned(reinterpret_cast<const char*>(text == nullptr ? reinterpret_cast<const uint8_t*>("") : text), length);
    return SDL_SetClipboardText(owned.c_str()) ? ELISA_APPLICATION_OK : ELISA_APPLICATION_UNSUPPORTED;
}

// Copies the clipboard's text into `out`. `length` always receives the full
// byte length; QUEUE_FULL means `capacity` was too small and nothing was
// copied. An empty or non-text clipboard reads as zero bytes.
extern "C" int32_t elisa_application_v1_clipboard_text(uint8_t* out, uint32_t capacity, uint32_t* length) {
    if (length == nullptr || (out == nullptr && capacity != 0)) return ELISA_APPLICATION_INVALID_ARGUMENT;
    *length = 0;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    char* text = SDL_GetClipboardText();
    if (text == nullptr) return ELISA_APPLICATION_UNSUPPORTED;
    const size_t size = std::strlen(text);
    if (size > UINT32_MAX) { SDL_free(text); return ELISA_APPLICATION_QUEUE_FULL; }
    *length = uint32_t(size);
    if (size > capacity) { SDL_free(text); return ELISA_APPLICATION_QUEUE_FULL; }
    std::memcpy(out, text, size);
    SDL_free(text);
    return ELISA_APPLICATION_OK;
}
