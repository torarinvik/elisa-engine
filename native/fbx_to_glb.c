// FBX take -> editable GLB (plan M06). ufbx loads the FBX through the same
// axis and unit conversion as the cook path (right-handed, Y up, metres),
// then every animation stack is baked to per-node linear T/R/S key tracks.
// The GLB holds the node hierarchy, animation stacks, and (when present) the
// largest bounded skinned triangle mesh so Studio can view either skeleton
// or character surface from one derived take.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ufbx.h"

enum {
  FBX_GLB_OK = 0,
  FBX_GLB_LOAD_FAILED = -1,
  FBX_GLB_BAKE_FAILED = -2,
  FBX_GLB_NO_MEMORY = -3,
  FBX_GLB_WRITE_FAILED = -4,
  FBX_GLB_NOT_FINITE = -5,
  FBX_GLB_SKIN_FAILED = -6,
};

typedef struct {
  char *data;
  size_t length, capacity;
  int failed;
} Bytes;

static void put(Bytes *b, const void *data, size_t n) {
  if (b->failed) return;
  if (b->length + n > b->capacity) {
    size_t grown = b->capacity ? b->capacity * 2 : 4096;
    while (grown < b->length + n) grown *= 2;
    char *next = realloc(b->data, grown);
    if (!next) { b->failed = 1; return; }
    b->data = next;
    b->capacity = grown;
  }
  memcpy(b->data + b->length, data, n);
  b->length += n;
}

static void text(Bytes *b, const char *s) { put(b, s, strlen(s)); }

static void textf(Bytes *b, const char *format, double a, double c, double d, double e) {
  char buffer[256];
  int n = snprintf(buffer, sizeof buffer, format, a, c, d, e);
  if (n > 0) put(b, buffer, (size_t)n);
}

static void name(Bytes *b, ufbx_string s) {
  text(b, "\"");
  for (size_t k = 0; k < s.length; k++) {
    unsigned char c = (unsigned char)s.data[k];
    char esc[8];
    if (c == '"' || c == '\\') { esc[0] = '\\'; esc[1] = (char)c; put(b, esc, 2); }
    else if (c < 0x20) { snprintf(esc, sizeof esc, "\\u%04x", c); put(b, esc, 6); }
    else put(b, &c, 1);
  }
  text(b, "\"");
}

static int finite_f(double v) { return isfinite(v) && fabs(v) < 3.0e38; }

typedef struct {
  Bytes json_accessors, json_views, bin;
  size_t accessors;
  int bad;
} Out;

// Append `count` rows of `width` floats; returns the accessor index.
static size_t accessor(Out *o, const float *values, size_t count, size_t width, int with_bounds) {
  static const char *types[] = {"", "SCALAR", "", "VEC3", "VEC4"};
  size_t offset = o->bin.length;
  put(&o->bin, values, count * width * sizeof(float));
  if (o->accessors) { text(&o->json_accessors, ","); text(&o->json_views, ","); }
  textf(&o->json_views, "{\"buffer\":0,\"byteOffset\":%.0f,\"byteLength\":%.0f}", (double)offset,
        (double)(count * width * sizeof(float)), 0, 0);
  textf(&o->json_accessors, "{\"bufferView\":%.0f,\"componentType\":5126,\"count\":%.0f,", (double)o->accessors,
        (double)count, 0, 0);
  text(&o->json_accessors, "\"type\":\"");
  text(&o->json_accessors, types[width]);
  text(&o->json_accessors, "\"");
  if (with_bounds) {
    float lo = values[0], hi = values[0];
    for (size_t k = 1; k < count; k++) { if (values[k] < lo) lo = values[k]; if (values[k] > hi) hi = values[k]; }
    textf(&o->json_accessors, ",\"min\":[%.9g],\"max\":[%.9g]", lo, hi, 0, 0);
  }
  text(&o->json_accessors, "}");
  for (size_t k = 0; k < count * width; k++) if (!finite_f(values[k])) o->bad = 1;
  return o->accessors++;
}

static size_t raw_accessor(Out *o, const void *bytes, size_t byte_count,
                           size_t count, int component_type, const char *type) {
  size_t offset = o->bin.length;
  put(&o->bin, bytes, byte_count);
  if (o->accessors) { text(&o->json_accessors, ","); text(&o->json_views, ","); }
  textf(&o->json_views, "{\"buffer\":0,\"byteOffset\":%.0f,\"byteLength\":%.0f}",
        (double)offset, (double)byte_count, 0, 0);
  textf(&o->json_accessors, "{\"bufferView\":%.0f,\"componentType\":%.0f,\"count\":%.0f,\"type\":\"",
        (double)o->accessors, (double)component_type, (double)count, 0);
  text(&o->json_accessors, type);
  text(&o->json_accessors, "\"}");
  return o->accessors++;
}

// One baked track: times plus 3- or 4-wide values. Returns the sampler index.
static void track(Out *o, Bytes *samplers, Bytes *channels, size_t *sampler_count, size_t node, const char *path,
                  const double *times, const double *values, size_t count, size_t width, size_t stride) {
  float *t = malloc(count * sizeof(float)), *v = malloc(count * width * sizeof(float));
  if (!t || !v) { free(t); free(v); o->bad = 2; return; }
  for (size_t k = 0; k < count; k++) {
    t[k] = (float)times[k * stride];
    for (size_t c = 0; c < width; c++) v[k * width + c] = (float)values[k * stride + c];
  }
  size_t input = accessor(o, t, count, 1, 1), output = accessor(o, v, count, width, 0);
  free(t);
  free(v);
  if (*sampler_count) { text(samplers, ","); text(channels, ","); }
  textf(samplers, "{\"input\":%.0f,\"output\":%.0f,\"interpolation\":\"LINEAR\"}", (double)input, (double)output, 0, 0);
  textf(channels, "{\"sampler\":%.0f,\"target\":{\"node\":%.0f,\"path\":", (double)*sampler_count, (double)node, 0, 0);
  text(channels, "\"");
  text(channels, path);
  text(channels, "\"}}");
  (*sampler_count)++;
}

// ufbx_baked_vec3 / _quat rows are {double time; value; flags}; read them as
// doubles with the row stride.
#define ROW_DOUBLES(type) (sizeof(type) / sizeof(double))

enum {
  FBX_GLB_MAX_SKIN_TRIANGLES = 1000000,
  FBX_GLB_MAX_SKIN_JOINTS = 256,
  FBX_GLB_MAX_VERTEX_INFLUENCES = 8,
};

typedef struct {
  float *positions;
  uint16_t *joints;
  uint16_t *joints1;
  float *weights;
  float *weights1;
  uint32_t *indices;
  float *inverse_binds;
  size_t vertex_count, joint_count;
  size_t node_index;
  ufbx_node *mesh_node;
  ufbx_skin_deformer *skin;
  size_t position_accessor, joint_accessor, joint1_accessor, weight_accessor, weight1_accessor, index_accessor, bind_accessor;
  int present;
} SkinnedMesh;

static int finite_matrix(const ufbx_matrix *m) {
  for (size_t i = 0; i < 12; i++) if (!finite_f(m->v[i])) return 0;
  return 1;
}

// Select and decode the largest skinned mesh with the same corner-indexed
// semantics used by the bounded engine cooker. The skin arrays are cluster
// indexed, as required by glTF JOINTS_0 / skin.joints.
static int build_skinned_mesh(ufbx_scene *scene, Out *o, SkinnedMesh *out) {
  ufbx_node *selected = NULL;
  size_t selected_triangles = 0;
  for (size_t i = 1; i < scene->nodes.count; i++) {
    ufbx_node *node = scene->nodes.data[i];
    if (!node || !node->mesh || node->mesh->skin_deformers.count == 0) continue;
    if (node->mesh->num_triangles > selected_triangles) {
      selected = node;
      selected_triangles = node->mesh->num_triangles;
    }
  }
  if (!selected) return 0;
  ufbx_mesh *mesh = selected->mesh;
  if (mesh->skin_deformers.count != 1 || selected_triangles == 0 ||
      selected_triangles > FBX_GLB_MAX_SKIN_TRIANGLES || selected_triangles > SIZE_MAX / 3) return -1;
  ufbx_skin_deformer *skin = mesh->skin_deformers.data[0];
  if (!skin || skin->clusters.count == 0 || skin->clusters.count > FBX_GLB_MAX_SKIN_JOINTS ||
      skin->vertices.count != mesh->num_vertices) return -1;
  const size_t vertices = selected_triangles * 3;
  if (vertices > SIZE_MAX / (sizeof(float) * 11 + sizeof(uint16_t) * 8 + sizeof(uint32_t))) return -1;
  out->positions = calloc(vertices * 3, sizeof(float));
  out->joints = calloc(vertices * 4, sizeof(uint16_t));
  out->joints1 = calloc(vertices * 4, sizeof(uint16_t));
  out->weights = calloc(vertices * 4, sizeof(float));
  out->weights1 = calloc(vertices * 4, sizeof(float));
  out->indices = calloc(vertices, sizeof(uint32_t));
  out->inverse_binds = calloc(skin->clusters.count * 16, sizeof(float));
  if (!out->positions || !out->joints || !out->joints1 || !out->weights || !out->weights1 || !out->indices || !out->inverse_binds) return -2;
  out->vertex_count = vertices;
  out->joint_count = skin->clusters.count;
  out->node_index = selected->typed_id - 1;
  out->mesh_node = selected;
  out->skin = skin;

  for (size_t joint = 0; joint < skin->clusters.count; joint++) {
    const ufbx_skin_cluster *cluster = skin->clusters.data[joint];
    if (!cluster || !cluster->bone_node || cluster->bone_node->typed_id == 0) return -3;
    ufbx_matrix inverse_bind = cluster->geometry_to_bone;
    if (!finite_matrix(&inverse_bind)) return -3;
    float *dst = out->inverse_binds + joint * 16;
    for (size_t column = 0; column < 3; column++) {
      dst[column * 4] = (float)inverse_bind.cols[column].x;
      dst[column * 4 + 1] = (float)inverse_bind.cols[column].y;
      dst[column * 4 + 2] = (float)inverse_bind.cols[column].z;
      dst[column * 4 + 3] = 0.0f;
    }
    dst[12] = (float)inverse_bind.cols[3].x;
    dst[13] = (float)inverse_bind.cols[3].y;
    dst[14] = (float)inverse_bind.cols[3].z;
    dst[15] = 1.0f;
  }

  uint32_t *triangles = malloc(mesh->max_face_triangles * 3 * sizeof(uint32_t));
  if (!triangles) return -2;
  size_t vertex = 0;
  for (size_t face_index = 0; face_index < mesh->faces.count; face_index++) {
    size_t triangle_count = ufbx_triangulate_face(triangles, mesh->max_face_triangles * 3,
        mesh, mesh->faces.data[face_index]);
    if (triangle_count == 0 || triangle_count > mesh->max_face_triangles) { free(triangles); return -3; }
    for (size_t corner = 0; corner < triangle_count * 3; corner++) {
      if (vertex >= vertices) { free(triangles); return -3; }
      uint32_t index = triangles[corner];
      if (index >= mesh->vertex_position.indices.count || index >= mesh->vertex_indices.count) {
        free(triangles); return -3;
      }
      ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
      float *position = out->positions + vertex * 3;
      position[0] = (float)p.x; position[1] = (float)p.y; position[2] = (float)p.z;
      if (!finite_f(position[0]) || !finite_f(position[1]) || !finite_f(position[2])) { free(triangles); return -3; }
      uint32_t source_vertex = mesh->vertex_indices.data[index];
      if (source_vertex >= skin->vertices.count) { free(triangles); return -3; }
      ufbx_skin_vertex influences = skin->vertices.data[source_vertex];
      if (influences.weight_begin > skin->weights.count ||
          influences.num_weights > skin->weights.count - influences.weight_begin) { free(triangles); return -3; }
      uint16_t row_joints[FBX_GLB_MAX_VERTEX_INFLUENCES] = {0};
      float row_weights[FBX_GLB_MAX_VERTEX_INFLUENCES] = {0};
      size_t row_count = 0;
      for (size_t w = 0; w < influences.num_weights; w++) {
        const ufbx_skin_weight *weight = &skin->weights.data[influences.weight_begin + w];
        if (weight->cluster_index >= skin->clusters.count || !isfinite(weight->weight) || weight->weight < 0) { free(triangles); return -3; }
        if (weight->weight == 0.0) continue;
        if (row_count >= FBX_GLB_MAX_VERTEX_INFLUENCES) { free(triangles); return -3; }
        size_t slot = row_count;
        while (slot > 0 && weight->weight > row_weights[slot - 1]) {
          row_weights[slot] = row_weights[slot - 1];
          row_joints[slot] = row_joints[slot - 1];
          slot--;
        }
        row_weights[slot] = (float)weight->weight;
        row_joints[slot] = (uint16_t)weight->cluster_index;
        row_count++;
      }
      double total = 0.0;
      for (size_t k = 0; k < row_count; k++) total += row_weights[k];
      if (!(total > 1.0e-20) || !isfinite(total)) { free(triangles); return -3; }
      for (size_t k = 0; k < row_count; k++) {
        row_weights[k] = (float)(row_weights[k] / total);
        if (k < 4) {
          out->joints[vertex * 4 + k] = row_joints[k];
          out->weights[vertex * 4 + k] = row_weights[k];
        } else {
          out->joints1[vertex * 4 + (k - 4)] = row_joints[k];
          out->weights1[vertex * 4 + (k - 4)] = row_weights[k];
        }
      }
      out->indices[vertex] = (uint32_t)vertex;
      vertex++;
    }
  }
  free(triangles);
  if (vertex != vertices) return -3;

  out->position_accessor = accessor(o, out->positions, vertices, 3, 0);
  out->joint_accessor = raw_accessor(o, out->joints, vertices * 4 * sizeof(uint16_t), vertices, 5123, "VEC4");
  out->joint1_accessor = raw_accessor(o, out->joints1, vertices * 4 * sizeof(uint16_t), vertices, 5123, "VEC4");
  out->weight_accessor = accessor(o, out->weights, vertices, 4, 0);
  out->weight1_accessor = accessor(o, out->weights1, vertices, 4, 0);
  out->index_accessor = raw_accessor(o, out->indices, vertices * sizeof(uint32_t), vertices, 5125, "SCALAR");
  out->bind_accessor = raw_accessor(o, out->inverse_binds, skin->clusters.count * 16 * sizeof(float), skin->clusters.count, 5126, "MAT4");
  out->present = 1;
  return 1;
}

static ufbx_matrix skin_node_bind_world(ufbx_node *node, ufbx_skin_deformer *skin) {
  for (size_t i = 0; i < skin->clusters.count; i++) {
    ufbx_skin_cluster *cluster = skin->clusters.data[i];
    if (cluster && cluster->bone_node == node) return cluster->bind_to_world;
  }
  return node->node_to_world;
}

static int skin_node_rest(ufbx_node *node, ufbx_skin_deformer *skin, ufbx_transform *out) {
  int in_hierarchy = 0;
  for (size_t i = 0; i < skin->clusters.count && !in_hierarchy; i++) {
    ufbx_skin_cluster *cluster = skin->clusters.data[i];
    for (ufbx_node *at = cluster ? cluster->bone_node : NULL; at; at = at->parent) {
      if (at == node) { in_hierarchy = 1; break; }
    }
  }
  if (!in_hierarchy) return 0;
  ufbx_matrix world = skin_node_bind_world(node, skin);
  if (node->parent && node->parent->parent != NULL) {
    ufbx_matrix parent_world = skin_node_bind_world(node->parent, skin);
    ufbx_matrix parent_inverse = ufbx_matrix_invert(&parent_world);
    world = ufbx_matrix_mul(&parent_inverse, &world);
  }
  if (!finite_matrix(&world)) return -1;
  *out = ufbx_matrix_to_transform(&world);
  return finite_f(out->translation.x) && finite_f(out->translation.y) &&
      finite_f(out->translation.z) && finite_f(out->rotation.x) &&
      finite_f(out->rotation.y) && finite_f(out->rotation.z) &&
      finite_f(out->rotation.w) && finite_f(out->scale.x) &&
      finite_f(out->scale.y) && finite_f(out->scale.z) ? 1 : -1;
}

static int write_glb(const char *path, Bytes *json, Bytes *bin) {
  while (json->length % 4) put(json, " ", 1);
  while (bin->length % 4) put(bin, "\0", 1);
  if (json->failed || bin->failed) return FBX_GLB_NO_MEMORY;
  if (json->length > UINT32_MAX - 28 || bin->length > UINT32_MAX - 28 ||
      json->length > UINT32_MAX - 28 - bin->length) return FBX_GLB_WRITE_FAILED;
  uint32_t header[3] = {0x46546C67u, 2u, (uint32_t)(12 + 8 + json->length + 8 + bin->length)};
  uint32_t json_head[2] = {(uint32_t)json->length, 0x4E4F534Au}, bin_head[2] = {(uint32_t)bin->length, 0x004E4942u};
  char *directory = malloc(strlen(path) + 2);
  if (!directory) return FBX_GLB_NO_MEMORY;
  strcpy(directory, path);
  char *slash = strrchr(directory, '/');
  if (slash == NULL) strcpy(directory, ".");
  else if (slash == directory) slash[1] = '\0';
  else *slash = '\0';
  size_t temp_size = strlen(path) + sizeof(".tmp.XXXXXX");
  char *temporary = malloc(temp_size);
  if (!temporary) { free(directory); return FBX_GLB_NO_MEMORY; }
  snprintf(temporary, temp_size, "%s.tmp.XXXXXX", path);
  int temp_fd = mkstemp(temporary);
  if (temp_fd < 0) { free(temporary); free(directory); return FBX_GLB_WRITE_FAILED; }
  FILE *f = fdopen(temp_fd, "wb");
  if (!f) { close(temp_fd); unlink(temporary); free(temporary); free(directory); return FBX_GLB_WRITE_FAILED; }
  int ok = fwrite(header, 12, 1, f) == 1 && fwrite(json_head, 8, 1, f) == 1 &&
           fwrite(json->data, json->length, 1, f) == 1 && fwrite(bin_head, 8, 1, f) == 1 &&
           (bin->length == 0 || fwrite(bin->data, bin->length, 1, f) == 1);
  if (ok) ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
  ok = fclose(f) == 0 && ok;
  if (ok) ok = link(temporary, path) == 0;
  if (!ok) {
    unlink(temporary);
    free(temporary);
    free(directory);
    return FBX_GLB_WRITE_FAILED;
  }
  // The hard link publishes the complete inode atomically and fails if any
  // destination already exists. Only our temporary name is ever removed.
  int temp_removed = unlink(temporary) == 0;
  int directory_fd = open(directory, O_RDONLY | O_DIRECTORY);
  int directory_synced = directory_fd >= 0 && fsync(directory_fd) == 0;
  if (directory_fd >= 0) close(directory_fd);
  free(directory);
  free(temporary);
  return temp_removed && directory_synced ? FBX_GLB_OK : FBX_GLB_WRITE_FAILED;
}

static int convert(ufbx_scene *scene, const char *output, double rate) {
  Out o = {0};
  Bytes json = {0}, nodes = {0}, animations = {0}, mesh_json = {0}, skin_json = {0};
  SkinnedMesh skinned = {0};
  int skin_result = build_skinned_mesh(scene, &o, &skinned);
  if (skin_result < 0) {
    free(skinned.positions); free(skinned.joints); free(skinned.joints1); free(skinned.weights); free(skinned.weights1);
    free(skinned.indices); free(skinned.inverse_binds);
    free(o.json_accessors.data); free(o.json_views.data); free(o.bin.data);
    return FBX_GLB_SKIN_FAILED;
  }
  if (skin_result > 0) {
    text(&mesh_json, "{\"primitives\":[{\"attributes\":{\"POSITION\":");
    textf(&mesh_json, "%.0f,\"JOINTS_0\":%.0f,\"WEIGHTS_0\":%.0f",
        (double)skinned.position_accessor, (double)skinned.joint_accessor,
        (double)skinned.weight_accessor, 0);
    textf(&mesh_json, ",\"JOINTS_1\":%.0f,\"WEIGHTS_1\":%.0f",
        (double)skinned.joint1_accessor, (double)skinned.weight1_accessor, 0, 0);
    textf(&mesh_json, "},\"indices\":%.0f}]}", (double)skinned.index_accessor, 0, 0, 0);
    text(&skin_json, "{\"inverseBindMatrices\":");
    textf(&skin_json, "%.0f,\"joints\":[", (double)skinned.bind_accessor, 0, 0, 0);
    ufbx_mesh *skin_mesh = NULL;
    for (size_t i = 1; i < scene->nodes.count; i++) {
      ufbx_node *node = scene->nodes.data[i];
      if (node && node->typed_id - 1 == skinned.node_index) { skin_mesh = node->mesh; break; }
    }
    if (!skin_mesh || skin_mesh->skin_deformers.count != 1) return FBX_GLB_SKIN_FAILED;
    ufbx_skin_deformer *skin = skin_mesh->skin_deformers.data[0];
    for (size_t joint = 0; joint < skin->clusters.count; joint++) {
      ufbx_skin_cluster *cluster = skin->clusters.data[joint];
      if (joint) text(&skin_json, ",");
      textf(&skin_json, "%.0f", (double)(cluster->bone_node->typed_id - 1), 0, 0, 0);
    }
    text(&skin_json, "]}");
  }
  // glTF node k is ufbx node k + 1 (ufbx node 0 is the implicit root).
  size_t n = scene->nodes.count;
  for (size_t k = 1; k < n; k++) {
    ufbx_node *node = scene->nodes.data[k];
    ufbx_transform t = node->local_transform;
    if (skin_result > 0) {
      int rest_result = skin_node_rest(node, skinned.skin, &t);
      if (rest_result < 0) o.bad = 1;
    }
    if (k > 1) text(&nodes, ",");
    text(&nodes, "{\"name\":");
    name(&nodes, node->name);
    textf(&nodes, ",\"translation\":[%.9g,%.9g,%.9g]", t.translation.x, t.translation.y, t.translation.z, 0);
    textf(&nodes, ",\"rotation\":[%.9g,%.9g,%.9g,%.9g]", t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
    textf(&nodes, ",\"scale\":[%.9g,%.9g,%.9g]", t.scale.x, t.scale.y, t.scale.z, 0);
    if (!finite_f(t.translation.x + t.translation.y + t.translation.z + t.rotation.w + t.scale.x)) o.bad = 1;
    if (node->children.count) {
      text(&nodes, ",\"children\":[");
      for (size_t c = 0; c < node->children.count; c++)
        textf(&nodes, c ? ",%.0f" : "%.0f", (double)(node->children.data[c]->typed_id - 1), 0, 0, 0);
      text(&nodes, "]");
    }
    if (skin_result > 0 && k - 1 == skinned.node_index) text(&nodes, ",\"mesh\":0,\"skin\":0");
    text(&nodes, "}");
  }
  int result = FBX_GLB_OK;
  for (size_t s = 0; s < scene->anim_stacks.count && result == FBX_GLB_OK; s++) {
    ufbx_anim_stack *stack = scene->anim_stacks.data[s];
    ufbx_bake_opts opts = {0};
    opts.resample_rate = rate;
    opts.trim_start_time = true;
    ufbx_error error;
    ufbx_baked_anim *baked = ufbx_bake_anim(scene, stack->anim, &opts, &error);
    if (!baked) { result = FBX_GLB_BAKE_FAILED; break; }
    Bytes samplers = {0}, channels = {0};
    size_t count = 0;
    for (size_t k = 0; k < baked->nodes.count; k++) {
      ufbx_baked_node *b = &baked->nodes.data[k];
      if (b->typed_id == 0 || b->typed_id >= n) continue;
      size_t node = b->typed_id - 1;
      if (b->translation_keys.count)
        track(&o, &samplers, &channels, &count, node, "translation", &b->translation_keys.data[0].time,
              &b->translation_keys.data[0].value.x, b->translation_keys.count, 3, ROW_DOUBLES(ufbx_baked_vec3));
      if (b->rotation_keys.count)
        track(&o, &samplers, &channels, &count, node, "rotation", &b->rotation_keys.data[0].time,
              &b->rotation_keys.data[0].value.x, b->rotation_keys.count, 4, ROW_DOUBLES(ufbx_baked_quat));
      if (b->scale_keys.count)
        track(&o, &samplers, &channels, &count, node, "scale", &b->scale_keys.data[0].time,
              &b->scale_keys.data[0].value.x, b->scale_keys.count, 3, ROW_DOUBLES(ufbx_baked_vec3));
    }
    ufbx_free_baked_anim(baked);
    if (count) {
      if (animations.length) text(&animations, ",");
      text(&animations, "{\"name\":");
      name(&animations, stack->name);
      text(&animations, ",\"samplers\":[");
      put(&animations, samplers.data, samplers.length);
      text(&animations, "],\"channels\":[");
      put(&animations, channels.data, channels.length);
      text(&animations, "]}");
    }
    free(samplers.data);
    free(channels.data);
  }
  if (result == FBX_GLB_OK && o.bad) result = o.bad == 2 ? FBX_GLB_NO_MEMORY : FBX_GLB_NOT_FINITE;
  if (result == FBX_GLB_OK) {
    text(&json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"elisa-engine fbx_to_glb\"},\"scene\":0,\"scenes\":[{\"nodes\":[");
    ufbx_node *root = scene->root_node;
    for (size_t c = 0; c < root->children.count; c++)
      textf(&json, c ? ",%.0f" : "%.0f", (double)(root->children.data[c]->typed_id - 1), 0, 0, 0);
    text(&json, "]}],\"nodes\":[");
    put(&json, nodes.data, nodes.length);
    text(&json, "]");
    if (skin_result > 0) {
      text(&json, ",\"meshes\":[");
      put(&json, mesh_json.data, mesh_json.length);
      text(&json, "],\"skins\":[");
      put(&json, skin_json.data, skin_json.length);
      text(&json, "]");
    }
    if (animations.length) {
      text(&json, ",\"animations\":[");
      put(&json, animations.data, animations.length);
      text(&json, "]");
    }
    if (o.accessors) {
      text(&json, ",\"accessors\":[");
      put(&json, o.json_accessors.data, o.json_accessors.length);
      text(&json, "],\"bufferViews\":[");
      put(&json, o.json_views.data, o.json_views.length);
      textf(&json, "],\"buffers\":[{\"byteLength\":%.0f}]", (double)o.bin.length, 0, 0, 0);
    }
    text(&json, "}");
    if (o.json_accessors.failed || o.json_views.failed || nodes.failed || animations.failed ||
        mesh_json.failed || skin_json.failed) result = FBX_GLB_NO_MEMORY;
    else result = write_glb(output, &json, &o.bin);
  }
  free(json.data); free(nodes.data); free(animations.data); free(mesh_json.data); free(skin_json.data);
  free(o.json_accessors.data); free(o.json_views.data); free(o.bin.data);
  free(skinned.positions); free(skinned.joints); free(skinned.joints1); free(skinned.weights); free(skinned.weights1);
  free(skinned.indices); free(skinned.inverse_binds);
  return result;
}

// Convert `input` (.fbx) to an editable GLB at `output`, baking at `rate`
// samples per second (0 = ufbx's default of 30). Returns 0 or a negative
// FBX_GLB_* code; nothing is written unless the whole take converts.
int64_t elisa_fbx_to_glb(const char *input, const char *output, double rate) {
  if (!input || !output || !*input || !*output || !isfinite(rate) || rate > 120.0) return FBX_GLB_LOAD_FAILED;
  struct stat source_stat;
  if (stat(input, &source_stat) != 0 || !S_ISREG(source_stat.st_mode) ||
      source_stat.st_size <= 0 || (uint64_t)source_stat.st_size > ((uint64_t)512 << 20))
    return FBX_GLB_LOAD_FAILED;
  ufbx_load_opts options = {0};
  options.strict = true;
  options.ignore_embedded = true;
  options.load_external_files = false;
  options.node_depth_limit = 256;
  options.temp_allocator.memory_limit = (size_t)1 << 31;
  options.result_allocator.memory_limit = (size_t)1 << 31;
  // Bake the axis/unit change into the nodes: the implicit root, which
  // TRANSFORM_ROOT would put it on, is not exported.
  options.target_axes = ufbx_axes_right_handed_y_up;
  options.target_unit_meters = 1.0;
  options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
  ufbx_error error;
  ufbx_scene *scene = ufbx_load_file(input, &options, &error);
  if (!scene) return FBX_GLB_LOAD_FAILED;
  if (scene->nodes.count == 0 || scene->nodes.data[0] != scene->root_node ||
      scene->nodes.count > 4096 || scene->meshes.count > 1024 ||
      scene->bones.count > 4096 || scene->materials.count > 1024 ||
      scene->anim_stacks.count > 256) {
    ufbx_free_scene(scene);
    return FBX_GLB_LOAD_FAILED;
  }
  int result = convert(scene, output, rate);
  ufbx_free_scene(scene);
  return result;
}
