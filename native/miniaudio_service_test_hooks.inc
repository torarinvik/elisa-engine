// Test-only members of probe::audio::Service, included inside the class when
// ELISA_AUDIO_TEST_PROBE is defined. They let harnesses simulate device loss
// and open failures that real hardware cannot be made to produce on demand.

// Delivers a device "stopped" notification as the backend would on loss.
void request_device_recovery_for_test() {
    ma_device_notification notification{};
    notification.pDevice = &device_;
    notification.type = ma_device_notification_type_stopped;
    notification_callback(&notification);
}

// The next `count` device opens fail after their context is created.
void fail_device_inits_for_test(uint32_t count) { injected_init_failures_ = count; }

// Data callbacks delivered to this service since construction.
uint64_t data_callbacks_for_test() const { return data_callbacks_.load(std::memory_order_relaxed); }

// Devices open across every Service in the process.
static int open_devices_for_test() { return open_devices_.load(std::memory_order_acquire); }

bool initialized_for_test() const { std::lock_guard<std::mutex> guard(mutex_); return initialized_; }
