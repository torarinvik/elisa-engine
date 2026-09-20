# Animation submission validation

`native/animation_submission_bridge.h` is a narrow Wicked adapter for Elisa
poses. Each handle owns two bounded pose buffers, validates every matrix and
morph weight, applies the active buffer to armature transforms and mesh morph
targets, and refuses retirement until the caller marks the submission complete.
The handle carries owner and generation checks so a pose cannot cross bridge
instances or outlive its logical animation resource.

Evidence: the SDL3/Wicked gate creates an armature and morph target, submits a
bone palette and weight, checks the applied Wicked components, rejects a foreign
handle, and verifies explicit completion before destruction. Multi-instance
render bounds and Elisa-driven runtime scheduling remain open R08 work.
