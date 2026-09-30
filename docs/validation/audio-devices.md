# Audio playback devices

`AudioDevices` (src/audio/devices.elisa) lists playback devices through native/audio_devices.inc. Each call opens a short-lived miniaudio context, so listing works whether or not the mixer is running and does not disturb the open device. `count()` returns -1 when the backend cannot enumerate. `device(index)` returns the UTF-8 name, up to 256 bytes, and whether the device is the system default. An index past the current list is `Missing`, which covers a device unplugged between the two calls.

Validation: test/audio_devices_probe.elisa runs in application-native-smoke with codes 207–210. It checks that every listed device has a name, that exactly one is the default, and that an out-of-range index is `Missing`. A negative control that clears the default flag fails with 209, which shows real devices were listed on the test Mac. Opening the mixer on a chosen device, rather than the default, is not implemented yet.
