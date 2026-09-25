# Render-quality profile persistence

`src/runtime/quality_settings.elisa` exposes `QualitySettings::profile_to_blob`,
`profile_from_blob`, `save_user_data`, and `load_user_data` to Elisa projects.
The module uses the existing `Save::Blob` and `UserData` APIs; it adds no native
shim and leaves `UserData::initialize` under application control. Persisted
values are decoded into a local profile and returned only after every value and
the complete `Quality::Profile` pass validation.

The quality schema is version 1 and uses the stable key `render-quality` in
each application's user-data directory. Its fourteen fields are stored in this
order: quality level, tonemapper, upscaler, render scale, bloom, bloom
threshold, FXAA, temporal AA, ambient occlusion, screen-space reflections,
fog, depth effects, shadow quality, and sun shadow receiver bias. Enum fields
use explicit `0/1/2` codes and booleans use `0/1`; floats use signed millionths
so the blob contains only portable signed 64-bit fields. The envelope retains
the Save magic marker and record count. Version, marker, exact field count,
enum codes, finite/ranged floats, and supported feature combinations are all
checked before a profile is returned.

Call `UserData::initialize` once for the stable application ID before calling
the convenience load/save functions. A missing record remains
`UserDataError.NotFound`, allowing the game to choose its own initial profile.
Unknown profile versions, malformed blobs, invalid fields, and unsupported
combinations are reported without partially applying a profile.

`test/user_data_probe.elisa` verifies stable wire codes, all-field round-trip
within fixed-point precision, corrupt and wrong-version rejection, invalid
combinations on decode and encode, and user-data save/reload. The focused
`test/quality_settings_native_main.elisa` runs independently of the full
renderer lifecycle fixture. The application smoke script includes this test
before the broader native scenarios.
