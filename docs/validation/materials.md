# PBR material validation

`src/backend/material.elisa` defines the shared material contract for Wicked and
Godot: base color, normal, metallic/roughness, occlusion, and emissive asset
slots; scalar factors; alpha mode/cutoff; and double-sided policy. Texture slots
carry stable asset IDs, leaving queried GPU format and upload ownership to the
native adapter.

The contract rejects invalid factor ranges, invalid texture IDs, and masked
materials without a base-color texture. `test/material.elisa` covers channel
selection, factors, masked alpha, and invalid input. Color-space conversion,
normal-map transcode policy, and native material ABI encoding remain A06/R04.
