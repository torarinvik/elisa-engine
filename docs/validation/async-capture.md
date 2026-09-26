# Asynchronous screenshot capture

`Application::request_screenshot` records a copy of the currently presented SDL3 back buffer into one of four retained READBACK textures. It returns a ticket without submitting command lists or waiting for the GPU. Normal application rendering submits the copy with the next frame; `Application::poll_screenshot` can submit pending capture commands when the caller needs progress before another frame. A completed poll writes the PNG and returns the application frame number and captured dimensions. `Application::cancel_screenshot` drops the output request but keeps the staging and source textures alive until Metal signals completion.

The current completion query uses Wicked's existing per-queue Metal shared events. Vulkan and DX12 report that they do not support the query, so capture requests fail with the unsupported status on those backends. Swapchain resize snapshots are retained by the request, but device-loss recovery and resize stress are not yet covered. The queue is owner-thread only and bounded to four pending or unconsumed tickets.

The native application smoke fills the queue, checks that a fifth request is rejected, cancels one ticket, advances frames, polls a remaining ticket, and validates the resulting PNG. The CPU encoder handles 8-bit RGBA/BGRA and packed `R10G10B10A2_UNORM` output. Run the focused encoder check with:

```sh
python3 scripts/rgba_png_self_test.py
```
