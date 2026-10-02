#pragma once

static bool valid_environment_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    for (size_t index = 0; index < 128; ++index) {
        const char c = name[index];
        if (c == '\0') return index > 0;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return false;
}

extern "C" const char* elisa_application_v1_environment_value(const char* name) {
    if (!valid_environment_name(name)) return "";
    const char* value = std::getenv(name);
    return value == nullptr ? "" : value;
}

extern "C" int64_t elisa_application_v1_environment_integer(const char* name, int64_t fallback) {
    const char* value = elisa_application_v1_environment_value(name);
    if (value[0] == '\0') return fallback;
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(value, &end, 10);
    if (end == value || *end != '\0' || errno != 0) return fallback;
    return static_cast<int64_t>(parsed);
}

extern "C" int32_t elisa_application_v1_save_screenshot(const char* path) {
    if (path == nullptr || path[0] == '\0' || std::strlen(path) > 4096) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    if (service.frame_count == 0) return ELISA_APPLICATION_INVALID_STATE;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return ELISA_APPLICATION_FRAME_FAILED;
    device->WaitForGPU();
    const wi::graphics::Texture presented = device->GetBackBuffer(&service.host.wicked().swapChain);
    if (!presented.IsValid()) return ELISA_APPLICATION_FRAME_FAILED;
    return probe::save_rgba_png(presented, std::string(path)) ? ELISA_APPLICATION_OK : ELISA_APPLICATION_FRAME_FAILED;
}

// Presented frame size in pixels (0 before the first frame), so a caller can
// size the buffer for elisa_application_v1_read_rgba.
static wi::graphics::Texture application_presented_texture() {
    ApplicationService& service = application_service();
    if (!service.initialized || service.frame_count == 0 || !on_owner_thread(service)) return {};
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return {};
    return device->GetBackBuffer(&service.host.wicked().swapChain);
}

extern "C" int64_t elisa_application_v1_presented_width(void) {
    std::lock_guard<std::mutex> guard(application_service().mutex);
    const wi::graphics::Texture presented = application_presented_texture();
    return presented.IsValid() ? int64_t(presented.GetDesc().width) : 0;
}

extern "C" int64_t elisa_application_v1_presented_height(void) {
    std::lock_guard<std::mutex> guard(application_service().mutex);
    const wi::graphics::Texture presented = application_presented_texture();
    return presented.IsValid() ? int64_t(presented.GetDesc().height) : 0;
}

// Copy the last presented frame out as tightly packed RGBA8, top row first
// (plan M01: the engine viewport shows it as a backdrop behind its overlays).
// Returns the bytes written, or 0 when there is no frame or `capacity` is
// too small.
extern "C" int64_t elisa_application_v1_read_rgba(uint8_t* out, int64_t capacity) {
    if (out == nullptr || capacity <= 0) return 0;
    std::lock_guard<std::mutex> guard(application_service().mutex);
    const wi::graphics::Texture presented = application_presented_texture();
    if (!presented.IsValid()) return 0;
    const auto desc = presented.GetDesc();
    const int64_t bytes = int64_t(desc.width) * int64_t(desc.height) * 4;
    if (bytes <= 0 || bytes > capacity) return 0;
    wi::graphics::GetDevice()->WaitForGPU();
    wi::vector<uint8_t> raw;
    if (!wi::helper::saveTextureToMemoryFile(presented, "RAW", raw) || int64_t(raw.size()) < bytes) return 0;
    std::memcpy(out, raw.data(), size_t(bytes));
    return bytes;
}

namespace {

void note_application_capture_submission(ApplicationService& service) {
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return;
    const uint64_t submitted_frame = device->GetFrameCount();
    for (CaptureRequest& capture : service.captures) {
        if (capture.state != CaptureRequestState::Free && !capture.submitted &&
            capture.device == device && submitted_frame > capture.device_frame_before) {
            capture.gpu_frame = submitted_frame;
            capture.submitted = true;
        }
    }
}

void flush_application_capture_requests(ApplicationService& service) {
    if (wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice()) {
        bool has_unsubmitted_capture = false;
        for (CaptureRequest& capture : service.captures) {
            if (capture.state != CaptureRequestState::Free && !capture.submitted && capture.device == device)
                has_unsubmitted_capture = true;
        }
        if (has_unsubmitted_capture) {
            device->SubmitCommandLists();
            const uint64_t submitted_frame = device->GetFrameCount();
            for (CaptureRequest& capture : service.captures) {
                if (capture.state != CaptureRequestState::Free && !capture.submitted && capture.device == device) {
                    capture.gpu_frame = submitted_frame;
                    capture.submitted = true;
                }
            }
        }
        device->WaitForGPU();
    }
    for (CaptureRequest& capture : service.captures) capture = CaptureRequest{};
}

void release_cancelled_captures(ApplicationService& service, wi::graphics::GraphicsDevice* device) {
    if (device == nullptr || !device->SupportsFrameCompletionQuery()) return;
    for (CaptureRequest& capture : service.captures) {
        if (capture.state == CaptureRequestState::Cancelled && capture.submitted && capture.device == device &&
            device->IsFrameComplete(capture.gpu_frame)) {
            capture = CaptureRequest{};
        }
    }
}

// Device loss (injected by the test hook; Wicked exposes no portable removal
// query): every outstanding ticket fails once, and its staging texture, source
// reference and timestamp resources are dropped at once. Wicked defers the
// actual GPU destruction, so in-flight copies stay safe on a live device.
int32_t fail_application_captures(ApplicationService& service) {
    int32_t failed = 0;
    for (CaptureRequest& capture : service.captures) {
        if (capture.state == CaptureRequestState::Pending) {
            const uint64_t ticket = capture.ticket;
            capture = CaptureRequest{};
            capture.state = CaptureRequestState::Failed;
            capture.ticket = ticket;
            ++failed;
        } else if (capture.state == CaptureRequestState::Cancelled) {
            capture = CaptureRequest{};
        }
    }
    service.last_capture_timing = CaptureGpuTiming{};
    return failed;
}

// Records begin/end timestamps around the copy and resolves them into a
// READBACK buffer in the same command list, so the query result rides the
// capture's own fence. Returns false (no timing) when the backend has none.
bool record_capture_timestamps(wi::graphics::GraphicsDevice* device, CaptureRequest& slot,
    wi::graphics::CommandList command, bool end) {
    if (!end) {
        if (device->GetTimestampFrequency() == 0) return false;
        wi::graphics::GPUQueryHeapDesc heap_desc;
        heap_desc.type = wi::graphics::GpuQueryType::TIMESTAMP;
        heap_desc.query_count = 2;
        wi::graphics::GPUBufferDesc buffer_desc;
        buffer_desc.size = 2 * sizeof(uint64_t);
        buffer_desc.usage = wi::graphics::Usage::READBACK;
        if (!device->CreateQueryHeap(&heap_desc, &slot.timestamps) ||
            !device->CreateBuffer(&buffer_desc, nullptr, &slot.timestamp_readback)) {
            slot.timestamps = {};
            slot.timestamp_readback = {};
            return false;
        }
        device->QueryReset(&slot.timestamps, 0, 2, command);
        device->QueryEnd(&slot.timestamps, 0, command);
        return true;
    }
    device->QueryEnd(&slot.timestamps, 1, command);
    device->QueryResolve(&slot.timestamps, 0, 2, &slot.timestamp_readback, 0, command);
    return true;
}

void keep_capture_timing(ApplicationService& service, const CaptureRequest& capture) {
    service.last_capture_timing = CaptureGpuTiming{};
    service.last_capture_timing.ticket = capture.ticket;
    if (!capture.timestamps_recorded || capture.timestamp_readback.mapped_data == nullptr) return;
    uint64_t stamps[2] = {};
    std::memcpy(stamps, capture.timestamp_readback.mapped_data, sizeof(stamps));
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr || stamps[1] < stamps[0]) return;
    service.last_capture_timing.ticks = stamps[1] - stamps[0];
    service.last_capture_timing.frequency = device->GetTimestampFrequency();
}

} // namespace

extern "C" int32_t elisa_application_v1_request_screenshot(const char* path, uint64_t* ticket) {
    if (path == nullptr || path[0] == '\0' || std::strlen(path) > 4096 || ticket == nullptr)
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    if (service.frame_count == 0) return ELISA_APPLICATION_INVALID_STATE;

    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return ELISA_APPLICATION_FRAME_FAILED;
    if (!device->SupportsFrameCompletionQuery()) return ELISA_APPLICATION_UNSUPPORTED;
    release_cancelled_captures(service, device);
    CaptureRequest* slot = nullptr;
    for (CaptureRequest& capture : service.captures) {
        if (capture.state == CaptureRequestState::Free) {
            slot = &capture;
            break;
        }
    }
    if (slot == nullptr) return ELISA_APPLICATION_QUEUE_FULL;

    const wi::graphics::Texture source = device->GetBackBuffer(&service.host.wicked().swapChain);
    if (!source.IsValid()) return ELISA_APPLICATION_FRAME_FAILED;
    const wi::graphics::TextureDesc source_desc = source.GetDesc();
    elisa::capture::PixelOrder pixel_order;
    switch (source_desc.format) {
    case wi::graphics::Format::R8G8B8A8_UNORM:
    case wi::graphics::Format::R8G8B8A8_UNORM_SRGB:
        pixel_order = elisa::capture::PixelOrder::RGBA;
        break;
    case wi::graphics::Format::B8G8R8A8_UNORM:
    case wi::graphics::Format::B8G8R8A8_UNORM_SRGB:
        pixel_order = elisa::capture::PixelOrder::BGRA;
        break;
    case wi::graphics::Format::R10G10B10A2_UNORM:
#ifdef __APPLE__
        // Wicked's Metal backend stores this format as BGR10A2Unorm.
        pixel_order = elisa::capture::PixelOrder::BGR10A2;
#else
        pixel_order = elisa::capture::PixelOrder::RGB10A2;
#endif
        break;
    default:
        return ELISA_APPLICATION_UNSUPPORTED;
    }
    if (source_desc.width == 0 || source_desc.height == 0 ||
        source_desc.width > elisa::capture::MAX_RGBA_BYTES / 4 ||
        source_desc.height > elisa::capture::MAX_RGBA_BYTES / (size_t(source_desc.width) * 4))
        return ELISA_APPLICATION_INVALID_ARGUMENT;

    wi::graphics::TextureDesc staging_desc = source_desc;
    staging_desc.usage = wi::graphics::Usage::READBACK;
    staging_desc.layout = wi::graphics::ResourceState::COPY_DST;
    staging_desc.bind_flags = wi::graphics::BindFlag::NONE;
    staging_desc.misc_flags = wi::graphics::ResourceMiscFlag::NONE;
    wi::graphics::Texture staging;
    if (!device->CreateTexture(&staging_desc, nullptr, &staging)) return ELISA_APPLICATION_FRAME_FAILED;

    const wi::graphics::CommandList command = device->BeginCommandList(wi::graphics::QUEUE_GRAPHICS);
    const wi::graphics::GPUBarrier to_copy = wi::graphics::GPUBarrier::Image(
        &source, source_desc.layout, wi::graphics::ResourceState::COPY_SRC);
    device->Barrier(&to_copy, 1, command);
    CaptureRequest timing_slot;
    const bool timed = record_capture_timestamps(device, timing_slot, command, false);
    device->CopyResource(&staging, &source, command);
    if (timed) record_capture_timestamps(device, timing_slot, command, true);
    const wi::graphics::GPUBarrier restore = wi::graphics::GPUBarrier::Image(
        &source, wi::graphics::ResourceState::COPY_SRC, source_desc.layout);
    device->Barrier(&restore, 1, command);
    uint64_t next_ticket = service.next_capture_ticket++;
    if (next_ticket == 0) next_ticket = service.next_capture_ticket++;
    *slot = CaptureRequest{};
    slot->state = CaptureRequestState::Pending;
    slot->ticket = next_ticket;
    slot->device_frame_before = device->GetFrameCount();
    slot->application_frame = service.frame_count;
    slot->width = source_desc.width;
    slot->height = source_desc.height;
    slot->pixel_order = pixel_order;
    slot->device = device;
    slot->source = source;
    slot->staging = std::move(staging);
    slot->timestamps = std::move(timing_slot.timestamps);
    slot->timestamp_readback = std::move(timing_slot.timestamp_readback);
    slot->timestamps_recorded = timed;
    slot->path = path;
    *ticket = next_ticket;
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_poll_screenshot(
    uint64_t ticket, uint64_t* frame_count, uint32_t* width, uint32_t* height) {
    if (ticket == 0 || frame_count == nullptr || width == nullptr || height == nullptr)
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;

    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    CaptureRequest* capture = nullptr;
    for (CaptureRequest& request : service.captures) {
        if (request.state != CaptureRequestState::Free && request.ticket == ticket) {
            capture = &request;
            break;
        }
    }
    if (capture == nullptr) return ELISA_APPLICATION_TICKET_NOT_FOUND;
    if (capture->state == CaptureRequestState::Failed) {
        *capture = CaptureRequest{};
        return ELISA_APPLICATION_CAPTURE_DEVICE_LOST;
    }
    if (capture->state == CaptureRequestState::Cancelled) {
        if (capture->submitted && device != nullptr && device == capture->device &&
            device->SupportsFrameCompletionQuery() &&
            device->IsFrameComplete(capture->gpu_frame)) {
            *capture = CaptureRequest{};
        }
        return ELISA_APPLICATION_CAPTURE_CANCELLED;
    }
    if (device == nullptr || device != capture->device || !device->SupportsFrameCompletionQuery()) {
        return ELISA_APPLICATION_FRAME_FAILED;
    }
    if (!capture->submitted) {
        device->SubmitCommandLists();
        const uint64_t submitted_frame = device->GetFrameCount();
        for (CaptureRequest& request : service.captures) {
            if (request.state != CaptureRequestState::Free && !request.submitted && request.device == device) {
                request.gpu_frame = submitted_frame;
                request.submitted = true;
            }
        }
    }
    if (!device->IsFrameComplete(capture->gpu_frame)) return ELISA_APPLICATION_CAPTURE_PENDING;

    if (capture->staging.mapped_subresources == nullptr || capture->staging.mapped_subresource_count == 0) {
        *capture = CaptureRequest{};
        return ELISA_APPLICATION_FRAME_FAILED;
    }
    const wi::graphics::SubresourceData& mapped = capture->staging.mapped_subresources[0];
    const size_t byte_count = mapped.slice_pitch != 0 ? mapped.slice_pitch : capture->staging.mapped_size;
    const bool saved = mapped.data_ptr != nullptr && elisa::capture::save_rgba_png(
        static_cast<const uint8_t*>(mapped.data_ptr), byte_count, capture->width, capture->height,
        mapped.row_pitch, capture->pixel_order, capture->path);
    if (!saved) {
        *capture = CaptureRequest{};
        return ELISA_APPLICATION_FRAME_FAILED;
    }
    *frame_count = capture->application_frame;
    *width = capture->width;
    *height = capture->height;
    keep_capture_timing(service, *capture);
    *capture = CaptureRequest{};
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_cancel_screenshot(uint64_t ticket) {
    if (ticket == 0) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    release_cancelled_captures(service, device);
    for (CaptureRequest& capture : service.captures) {
        if (capture.state == CaptureRequestState::Pending && capture.ticket == ticket) {
            capture.state = CaptureRequestState::Cancelled;
            return ELISA_APPLICATION_OK;
        }
    }
    return ELISA_APPLICATION_TICKET_NOT_FOUND;
}

// GPU timestamp ticks spent on the copy of the most recently completed ticket.
// UNSUPPORTED when the backend recorded no timestamps for it.
extern "C" int32_t elisa_application_v1_screenshot_gpu_ticks(uint64_t ticket, uint64_t* ticks, uint64_t* frequency) {
    if (ticket == 0 || ticks == nullptr || frequency == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    if (service.last_capture_timing.ticket != ticket) return ELISA_APPLICATION_TICKET_NOT_FOUND;
    if (service.last_capture_timing.frequency == 0) return ELISA_APPLICATION_UNSUPPORTED;
    *ticks = service.last_capture_timing.ticks;
    *frequency = service.last_capture_timing.frequency;
    return ELISA_APPLICATION_OK;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE) || defined(ELISA_APPLICATION_TEST_PROBE)
// Injects a capture-device failure; returns how many pending tickets failed.
extern "C" int32_t elisa_application_v1_test_fail_capture_device(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    return fail_application_captures(service);
}

// Slots still holding a staging texture, source reference or query resource.
extern "C" int32_t elisa_application_v1_test_capture_resource_count(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    int32_t held = 0;
    for (const CaptureRequest& capture : service.captures) {
        if (capture.staging.IsValid() || capture.source.IsValid() || capture.timestamps.IsValid() ||
            capture.timestamp_readback.IsValid())
            ++held;
    }
    return held;
}
#endif
