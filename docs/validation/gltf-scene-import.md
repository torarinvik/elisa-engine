# Native glTF scene traversal

`native/asset_import.h` now traverses the validated cgltf document beyond mesh
triangle counts. The normalized import summary records nodes, primitives,
cameras, lights, skins, animation channels, and morph targets. It validates
metallic/roughness and base-color factors, alpha mode/cutoff, double-sided
metadata, and texture references; supported Basis texture extensions are
recognized while unknown required extensions, non-triangle primitives,
Draco-compressed primitives, bad node parents, and incomplete animation
channel references fail before the summary is accepted.

The Wicked gate imports the authored maze tile and a pinned Basis glTF material
fixture, proving node and primitive hierarchy, positions, indices, PBR factors,
alpha metadata, texture references, and normalized rejection counts before the
cooked package is consumed. GPU mesh/material creation and richer authored
scenes with cameras/lights/skins/animations remain follow-up A05 work.
