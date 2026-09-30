// FBX take -> editable GLB (plan M06). ufbx loads the FBX through the same
// axis and unit conversion as the cook path (right-handed, Y up, metres),
// then every animation stack is baked to per-node linear T/R/S key tracks.
// The GLB holds the node hierarchy with rest TRS and one animation per
// stack, with float accessors only, so GlbDocument, MocapPipeline and
// MocapThumbnail edit it like any other take. Meshes and skins are not
// carried over: the output is the editable animation, not a render asset.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ufbx.h"

enum {
  FBX_GLB_OK = 0,
  FBX_GLB_LOAD_FAILED = -1,
  FBX_GLB_BAKE_FAILED = -2,
  FBX_GLB_NO_MEMORY = -3,
  FBX_GLB_WRITE_FAILED = -4,
  FBX_GLB_NOT_FINITE = -5,
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

static int write_glb(const char *path, Bytes *json, Bytes *bin) {
  while (json->length % 4) put(json, " ", 1);
  while (bin->length % 4) put(bin, "\0", 1);
  if (json->failed || bin->failed) return FBX_GLB_NO_MEMORY;
  uint32_t header[3] = {0x46546C67u, 2u, (uint32_t)(12 + 8 + json->length + 8 + bin->length)};
  uint32_t json_head[2] = {(uint32_t)json->length, 0x4E4F534Au}, bin_head[2] = {(uint32_t)bin->length, 0x004E4942u};
  FILE *f = fopen(path, "wb");
  if (!f) return FBX_GLB_WRITE_FAILED;
  int ok = fwrite(header, 12, 1, f) == 1 && fwrite(json_head, 8, 1, f) == 1 &&
           fwrite(json->data, json->length, 1, f) == 1 && fwrite(bin_head, 8, 1, f) == 1 &&
           (bin->length == 0 || fwrite(bin->data, bin->length, 1, f) == 1);
  ok = fclose(f) == 0 && ok;
  return ok ? FBX_GLB_OK : FBX_GLB_WRITE_FAILED;
}

static int convert(ufbx_scene *scene, const char *output, double rate) {
  Out o = {0};
  Bytes json = {0}, nodes = {0}, animations = {0};
  // glTF node k is ufbx node k + 1 (ufbx node 0 is the implicit root).
  size_t n = scene->nodes.count;
  for (size_t k = 1; k < n; k++) {
    ufbx_node *node = scene->nodes.data[k];
    ufbx_transform t = node->local_transform;
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
    if (o.json_accessors.failed || o.json_views.failed || nodes.failed || animations.failed) result = FBX_GLB_NO_MEMORY;
    else result = write_glb(output, &json, &o.bin);
  }
  free(json.data); free(nodes.data); free(animations.data);
  free(o.json_accessors.data); free(o.json_views.data); free(o.bin.data);
  return result;
}

// Convert `input` (.fbx) to an editable GLB at `output`, baking at `rate`
// samples per second (0 = ufbx's default of 30). Returns 0 or a negative
// FBX_GLB_* code; nothing is written unless the whole take converts.
int64_t elisa_fbx_to_glb(const char *input, const char *output, double rate) {
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
  if (scene->nodes.count == 0 || scene->nodes.data[0] != scene->root_node) {
    ufbx_free_scene(scene);
    return FBX_GLB_LOAD_FAILED;
  }
  int result = convert(scene, output, rate);
  ufbx_free_scene(scene);
  return result;
}
