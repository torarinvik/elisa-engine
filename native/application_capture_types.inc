// Failed: the device was lost (or a test injected it); resources are already
// released and the next poll reports the failure once.
enum class CaptureRequestState : uint8_t { Free, Pending, Cancelled, Failed };
constexpr size_t APPLICATION_CAPTURE_QUEUE_CAPACITY = 4;
struct CaptureRequest {
    CaptureRequestState state = CaptureRequestState::Free;
    uint64_t ticket = 0;
    uint64_t gpu_frame = 0;
    uint64_t device_frame_before = 0;
    uint64_t application_frame = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    elisa::capture::PixelOrder pixel_order = elisa::capture::PixelOrder::RGBA;
    bool submitted = false;
    wi::graphics::GraphicsDevice* device = nullptr;
    wi::graphics::Texture source;
    wi::graphics::Texture staging;
    // Two GPU timestamps around the copy, resolved into a READBACK buffer on
    // the same command list; absent when the backend lacks timestamp queries.
    wi::graphics::GPUQueryHeap timestamps;
    wi::graphics::GPUBuffer timestamp_readback;
    bool timestamps_recorded = false;
    std::string path;
};

struct CaptureGpuTiming {
    uint64_t ticket = 0;
    uint64_t ticks = 0;
    uint64_t frequency = 0;
};

struct ApplicationService;
void note_application_capture_submission(ApplicationService& service);
void flush_application_capture_requests(ApplicationService& service);
