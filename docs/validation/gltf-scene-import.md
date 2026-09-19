# Native glTF scene traversal

`native/asset_import.h` now traverses the validated cgltf document beyond mesh
triangle counts. The normalized import summary records nodes, primitives,
cameras, lights, skins, animation channels, and morph targets, and rejects
required extensions, non-triangle primitives, Draco-compressed primitives, bad
node parents, and incomplete animation channel references before the summary is
accepted.

The Wicked gate imports the authored maze tile and proves its node and
primitive hierarchy, positions, indices, and normalized rejection count before
the cooked package is consumed. GPU mesh creation and richer authored scenes
with cameras/lights/skins/animations remain follow-up A05 work.
