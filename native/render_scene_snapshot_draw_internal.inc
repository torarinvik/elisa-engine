bool configure_snapshot_draw(RenderSceneService &state, wi::ecs::Entity entity,
                             const elisa::assets::CookedGeometry &geometry,
                             const SnapshotSlotMaterials &materials,
                             const SnapshotMaterialAssetSlot &material_asset,
                             bool casts_shadow, bool has_placements,
                             size_t placement_index) {
  wi::scene::MeshComponent *mesh = state.scene->meshes.GetComponent(entity);
  wi::scene::MaterialComponent *material =
      state.scene->materials.GetComponent(entity);
  wi::scene::ObjectComponent *object =
      state.scene->objects.GetComponent(entity);
  if (mesh == nullptr || material == nullptr || object == nullptr)
    return false;

  size_t index_start = 0;
  size_t index_count = geometry.indices.size();
  size_t subset_start = 0;
  size_t subset_count = geometry.subsets.size();
  if (has_placements) {
    if (placement_index >= geometry.mesh_placements.size())
      return false;
    const auto &placement = geometry.mesh_placements[placement_index];
    index_start = placement.index_start;
    index_count = placement.index_count;
    subset_start = placement.subset_start;
    subset_count = placement.subset_count;
  } else if (placement_index != 0) {
    return false;
  }

  size_t emitted_subset = 0;
  size_t covered_indices = 0;
  for (size_t index = subset_start; index < subset_start + subset_count;
       ++index) {
    const auto &cooked = geometry.subsets[index];
    const size_t cooked_end = size_t(cooked.index_start) + cooked.index_count;
    const size_t begin = has_placements
                             ? std::max<size_t>(cooked.index_start, index_start)
                             : cooked.index_start;
    const size_t end = has_placements
                           ? std::min(cooked_end, index_start + index_count)
                           : cooked_end;
    if (begin >= end)
      continue;
    if (emitted_subset >= mesh->subsets.size() ||
        cooked.material_slot >= materials.size() ||
        materials[cooked.material_slot] == nullptr ||
        materials[cooked.material_slot]->material_entity ==
            wi::ecs::INVALID_ENTITY)
      return false;
    wi::scene::MeshComponent::MeshSubset &subset =
        mesh->subsets[emitted_subset++];
    subset.materialID = materials[cooked.material_slot]->material_entity;
    covered_indices += end - begin;
  }
  if (emitted_subset != mesh->subsets.size() || covered_indices != index_count)
    return false;
  mesh->CreateRenderData();

  material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_PBR;
  material->SetBaseColor(
      XMFLOAT4(material_asset.base_color[0], material_asset.base_color[1],
               material_asset.base_color[2], material_asset.base_color[3]));
  material->SetRoughness(material_asset.roughness);
  material->SetMetalness(material_asset.metallic);
  material->SetEmissiveColor(
      XMFLOAT4(material_asset.emissive[0], material_asset.emissive[1],
               material_asset.emissive[2], DEFAULT_SNAPSHOT_EMISSIVE_STRENGTH));
  material->SetAlphaRef(material_asset.alpha_mode ==
                                ELISA_RENDER_SCENE_ALPHA_MASK
                            ? material_asset.alpha_cutoff
                            : OPAQUE_SNAPSHOT_ALPHA_REF);
  material->SetDoubleSided(material_asset.double_sided);
  material->userBlendMode =
      material_asset.alpha_mode == ELISA_RENDER_SCENE_ALPHA_BLEND
          ? wi::enums::BLENDMODE_ALPHA
          : wi::enums::BLENDMODE_OPAQUE;
  material->SetCastShadow(casts_shadow);
  if (!apply_snapshot_material_textures(state, material_asset, *material))
    return false;
  object->SetCastShadow(casts_shadow);
  return true;
}

wi::ecs::Entity create_animated_snapshot_entity(
    RenderSceneService &state, SnapshotStageRow &row,
    const SnapshotMeshAssetSlot &mesh_asset,
    const SnapshotSlotMaterials &materials,
    const SnapshotMaterialAssetSlot &material_asset, const std::string &name,
    int32_t &failure_status, std::vector<wi::ecs::Entity> &joint_entities) {
  wi::ecs::Entity entity = state.scene->Entity_CreateCube(name);
  if (entity == wi::ecs::INVALID_ENTITY)
    return entity;
  const auto &geometry = mesh_asset.geometry;
  const bool has_placements = !geometry.mesh_placements.empty();
  const size_t placement_count =
      has_placements ? geometry.mesh_placements.size() : 1;
  const bool casts_shadow = snapshot_subsets_cast_shadow(geometry, materials);
  const auto discard_root = [&]() {
    for (wi::ecs::Entity joint : joint_entities)
      state.scene->Entity_Remove(joint);
    joint_entities.clear();
    if (entity != wi::ecs::INVALID_ENTITY)
      state.scene->Entity_Remove(entity);
    entity = wi::ecs::INVALID_ENTITY;
    return entity;
  };

  try {
    const bool root_configured =
        has_placements
            ? elisa::rendering::configure_cooked_mesh(
                  *state.scene, entity, geometry, row.position[0],
                  row.position[1], row.position[2], row.rotation[0],
                  row.rotation[1], row.rotation[2], row.rotation[3],
                  row.scale[0], row.scale[1], row.scale[2],
                  material_asset.base_color[0], material_asset.base_color[1],
                  material_asset.base_color[2], material_asset.base_color[3],
                  &joint_entities, 0)
            : elisa::rendering::configure_cooked_mesh(
                  *state.scene, entity, geometry, row.position[0],
                  row.position[1], row.position[2], row.rotation[0],
                  row.rotation[1], row.rotation[2], row.rotation[3],
                  row.scale[0], row.scale[1], row.scale[2],
                  material_asset.base_color[0], material_asset.base_color[1],
                  material_asset.base_color[2], material_asset.base_color[3],
                  &joint_entities);
    if (!root_configured ||
        !configure_snapshot_draw(state, entity, geometry, materials,
                                 material_asset, casts_shadow, has_placements,
                                 0)) {
      if (!root_configured)
        failure_status = ELISA_RENDER_SCENE_BACKEND_FAILED;
      else
        failure_status = ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
      return discard_root();
    }

    const wi::ecs::Entity shared_armature =
        geometry.skin_joints.empty() || geometry.skin_cluster_joints.empty()
            ? wi::ecs::INVALID_ENTITY
            : entity;
    row.created_placement_entities.reserve(placement_count - 1);
    for (size_t placement = 1; placement < placement_count; ++placement) {
      const wi::ecs::Entity child = state.scene->Entity_CreateCube(
          name + "_placement_" + std::to_string(placement));
      if (child == wi::ecs::INVALID_ENTITY)
        return discard_root();
      row.created_placement_entities.push_back(child);
      if (!elisa::rendering::configure_cooked_mesh(
              *state.scene, child, geometry, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
              1.0f, 1.0f, 1.0f, 1.0f, material_asset.base_color[0],
              material_asset.base_color[1], material_asset.base_color[2],
              material_asset.base_color[3], nullptr, placement,
              shared_armature)) {
        failure_status = ELISA_RENDER_SCENE_BACKEND_FAILED;
        return discard_root();
      }
      if (!configure_snapshot_draw(state, child, geometry, materials,
                                   material_asset, casts_shadow, true,
                                   placement)) {
        failure_status = ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        return discard_root();
      }
      state.scene->Component_Attach(child, entity, true);
      const wi::scene::TransformComponent *parent_transform =
          state.scene->transforms.GetComponent(entity);
      wi::scene::TransformComponent *child_transform =
          state.scene->transforms.GetComponent(child);
      if (parent_transform == nullptr || child_transform == nullptr) {
        failure_status = ELISA_RENDER_SCENE_BACKEND_FAILED;
        return discard_root();
      }
      child_transform->UpdateTransform_Parented(*parent_transform);
    }
    row.created_joint_entities = std::move(joint_entities);
    row.created_skin_joints = geometry.skin_joints;
    row.created_animation_clips = geometry.animation_clips;
    row.created_morph_default_weights = geometry.morph_default_weights;
    return entity;
  } catch (...) {
    discard_root();
    return wi::ecs::INVALID_ENTITY;
  }
}
