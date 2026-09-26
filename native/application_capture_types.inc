enum class CaptureRequestState : uint8_t { Free, Pending, Cancelled };
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
    std::string path;
};

struct ApplicationService;
void note_application_capture_submission(ApplicationService& service);
void flush_application_capture_requests(ApplicationService& service);
