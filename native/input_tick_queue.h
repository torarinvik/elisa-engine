#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace probe {

// Preserve discrete input events until simulation ticks consume them. The
// bounded queue makes overload explicit instead of silently coalescing taps.
class InputTickQueue {
public:
    static constexpr std::size_t CAPACITY = 32;

    bool enqueue(int32_t action) {
        if (action < 0 || count_ == CAPACITY) return false;
        const std::size_t tail = (head_ + count_) % CAPACITY;
        actions_[tail] = action;
        ++count_;
        return true;
    }

    bool dequeue(int32_t& action) {
        if (count_ == 0) return false;
        action = actions_[head_];
        head_ = (head_ + 1) % CAPACITY;
        --count_;
        return true;
    }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

    std::size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }

private:
    std::array<int32_t, CAPACITY> actions_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

} // namespace probe
