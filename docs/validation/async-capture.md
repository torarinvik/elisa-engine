# Asynchronous screenshot capture

`Application::request_screenshot` records a copy of the currently presented SDL3 back buffer into one of four retained READBACK textures. It returns a ticket without submitting command lists or waiting for the GPU. Normal application rendering submits the copy with the next frame; `Application::poll_screenshot` can submit pending capture commands when the caller needs progress before another frame. A completed poll writes the PNG and returns the application frame number and captured dimensions. `Application::cancel_screenshot` drops the output request but keeps the staging and source textures alive until the backend signals completion.

Metal, Vulkan, and DX12 use their per-queue frame completion events/fences. The queue is owner-thread only and bounded to four pending or unconsumed tickets. A backend without a nonblocking completion query returns the unsupported status.

## GPU query readback

Each request also writes two GPU timestamps around the copy and resolves them into a 16-byte READBACK buffer in the same command list, so the query result completes on the capture's own fence. After a completed poll, `Application::screenshot_gpu_nanoseconds(ticket)` returns the copy's GPU time. The tick-to-nanosecond conversion lives in the float-free `ApplicationCaptureTiming` module and is proved by `proof/application_capture_timing.elisa`: no division by zero, no overflow, zero for an unknown frequency, and a result that fits `i64`. A backend that reports no timestamp frequency returns `SCREENSHOT_TIMING_UNSUPPORTED` (-10) instead of a fabricated value. Only the most recently completed ticket keeps its timing. Occlusion and pipeline-statistics queries are not routed through the queue.

## Device failure

If the device fails, every outstanding ticket moves to a failed state at once. Its staging texture, source reference and timestamp resources are released at that point. The next poll of a failed ticket returns `SCREENSHOT_DEVICE_LOST` (-12) once, and later polls report the ticket as unknown. Cancelled tickets are dropped. Wicked's resource destruction is deferred, so copies still in flight on a live device remain safe. Wicked has no portable device-removed query, so in tests the failure is injected with the probe-only hook `elisa_application_v1_test_fail_capture_device`; `elisa_application_v1_test_capture_resource_count` reports how many slots still hold GPU resources.

## Evidence (2026-10-02, macOS 27.0, Apple M5, SDL3/Metal)

`application-async-capture-smoke` (`test/application_capture_async_main.elisa`) covers the following:

- Saturation and cancellation: four requests fill the queue, a fifth is rejected, and one ticket is cancelled.
- A single resize: the window goes from 320x200 to 400x260 while tickets are pending.
- Repeated resize: three tickets are pending while the window is resized to 480x300, then 360x240, then 440x280. They are polled out of order, and each reports the frame and pixel size it was requested on. The last ticket's GPU copy time is positive, and an earlier ticket's timing is reported as not found.
- Device failure: one submitted and one unsubmitted ticket are pending when the failure is injected. The hook reports two failed tickets, the resource count drops from 2 to 0, and each poll returns device-lost once and then not-found. Cancelling a failed ticket is rejected. A fresh capture then completes with its own frame and size, and no slot keeps resources afterwards.

The final empty-frame PNG (880x560) decodes to opaque black.

Authored-scene reference: render group 239 (`test/render_graph_async_capture_native.elisa`, cases 38–43) requests one ticket after the authored render graph recovers from its injected pass failure, keeps presenting frames while the ticket is pending, and checks the reported frame and size. `scripts/compare_render_graph_references.py` checks the PNG against the synchronous `recovered` capture (peak 0.0000, mean 0.0000) and against the committed `docs/validation/references/render-graph/async.png` (peak 0.0000), using the lighting references' 160x100 reduction and tolerances.

This scene exposed a channel-order bug. Wicked's Metal backend stores `R10G10B10A2_UNORM` as `BGR10A2Unorm`, so the earlier decoder swapped red and blue in every colored async capture. The all-black empty frame had hidden it. Captures now use `PixelOrder::BGR10A2` on Apple, and `scripts/rgba_png_self_test.py` checks both packed orders.

## Platform limits

Vulkan and DX12 runtime coverage cannot run on this Mac. Those backends compile against the same fence and timestamp-query interfaces but are not exercised. The non-Apple path assumes `R10G10B10A2_UNORM` is stored in RGB order. Real device-removal detection depends on the backend; only the injected path is tested.

The CPU encoder handles 8-bit RGBA/BGRA and packed RGB/BGR 10-bit output. Run the focused encoder check with:

```sh
python3 scripts/rgba_png_self_test.py
```
