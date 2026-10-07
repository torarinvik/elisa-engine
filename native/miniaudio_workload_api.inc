    uint32_t active_voices() const {
        std::lock_guard<std::mutex> guard(mutex_);
        uint32_t count = 0;
        for (const Voice& voice : voices_) count += voice.live ? 1 : 0;
        return count;
    }

    // Owner-thread diagnostic. The callback never queries or allocates here.
    // Buffer capacities include retained storage, excluding decoder/device
    // internals and allocator overhead. One lock covers the resource snapshot.
    WorkloadSnapshot workload_snapshot() const {
        std::lock_guard<std::mutex> guard(mutex_);
        WorkloadSnapshot result;
        for (const Clip& clip : clips_) {
            result.clips += clip.live ? 1 : 0;
            result.pcm_bytes += static_cast<uint64_t>(clip.samples.capacity()) * sizeof(int16_t);
        }
        for (const Voice& voice : voices_) result.voices += voice.live ? 1 : 0;
        result.streams = streams_.live_count();
        result.ring_bytes = streams_.allocated_ring_bytes();
        result.callbacks = data_callbacks_.load(std::memory_order_relaxed);
        result.contended_callbacks = contended_callbacks_.load(std::memory_order_relaxed);
        return result;
    }
