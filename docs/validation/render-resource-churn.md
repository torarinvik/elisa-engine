# Render resource churn

`render-resource-churn-smoke` (test/render_resource_churn_native_main.elisa) runs six rounds
against a live WickedEngine scene. Each round creates 64 box instances and 8 point lights,
renders two frames, destroys them all, and renders two more frames. The new
`RenderScene::resource_counts()` (native `elisa_render_scene_v1_resource_counts`) reports live
instance slots, live lights and scene entities. The smoke checks that the peak counts are the
baseline plus what was created and that the counts go back to the baseline after each round.
It also checks that destroying the same instance twice is refused.

A control that skips destroying the lights fails with status 47.
This covers resource churn only. The many-instance and lit-material fixtures for Q01 are still open.
