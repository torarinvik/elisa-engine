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
        pixel_order = elisa::capture::PixelOrder::RGB10A2;
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
    device->CopyResource(&staging, &source, command);
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
