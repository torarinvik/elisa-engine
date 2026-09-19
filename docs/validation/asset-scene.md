# Normalized asset scene validation

`src/assets/scene.elisa` is the ownership boundary for native cgltf imports. It
stores stable node IDs and parent links, primitive mesh/material identities,
triangle/index counts, cameras, lights, skin metadata, and morph-target counts
without retaining parser pointers. Missing parents, duplicate IDs, cycles,
invalid primitive dimensions, and unsupported extensions are rejected.

`test/asset_scene.elisa` covers a two-node hierarchy, a skinned/morphed
primitive, camera/light records, unsupported-extension rejection, and an invalid
parent. Native cgltf traversal still has to populate every supported glTF
primitive and preserve diagnostics for unhandled extensions.
