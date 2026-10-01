#pragma once
// R06: world bounds of a baked navmesh, over every vertex of its ground
// polygons (off-mesh link polygons are skipped). Navigation already works in
// Elisa coordinates, so no handedness conversion is needed here.
#include "navmesh_service.h"

#include <cfloat>

namespace probe::nav {

inline bool navmesh_bounds(const dtNavMesh* mesh, float minimum[3], float maximum[3]) {
    if (mesh == nullptr) return false;
    for (int axis = 0; axis < 3; ++axis) {
        minimum[axis] = FLT_MAX;
        maximum[axis] = -FLT_MAX;
    }
    bool any = false;
    for (int index = 0; index < mesh->getMaxTiles(); ++index) {
        const dtMeshTile* tile = mesh->getTile(index);
        if (tile == nullptr || tile->header == nullptr) continue;
        for (int poly = 0; poly < tile->header->polyCount; ++poly) {
            const dtPoly& polygon = tile->polys[poly];
            if (polygon.getType() != DT_POLYTYPE_GROUND) continue;
            for (int vertex = 0; vertex < polygon.vertCount; ++vertex) {
                const float* v = &tile->verts[polygon.verts[vertex] * 3];
                for (int axis = 0; axis < 3; ++axis) {
                    if (v[axis] < minimum[axis]) minimum[axis] = v[axis];
                    if (v[axis] > maximum[axis]) maximum[axis] = v[axis];
                }
                any = true;
            }
        }
    }
    return any;
}

} // namespace probe::nav
