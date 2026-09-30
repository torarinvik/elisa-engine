// Folder listing for the mocap batch CLI (plan M07). Elisa has no directory
// API, so this lists `*.glb` names in sorted (byte) order and joins paths into
// one of four static buffers, each valid until four more calls.
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOCAP_MAX_FILES 4096
static char *names[MOCAP_MAX_FILES];
static int64_t name_count;
// A small ring so a call can pass several joined paths at once.
static char joined[4][4096];
static int next_joined;

static int by_name(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static int is_glb(const char *name) {
  size_t n = strlen(name);
  return n > 4 && name[0] != '.' && strcmp(name + n - 4, ".glb") == 0;
}

// Returns the number of .glb files in `dir`, or -1 when it cannot be read.
int64_t elisa_mocap_scan(const char *dir) {
  for (int64_t i = 0; i < name_count; i++) free(names[i]);
  name_count = 0;
  DIR *d = opendir(dir);
  if (!d) return -1;
  struct dirent *e;
  while ((e = readdir(d)) && name_count < MOCAP_MAX_FILES) {
    if (is_glb(e->d_name)) names[name_count++] = strdup(e->d_name);
  }
  closedir(d);
  qsort(names, (size_t)name_count, sizeof names[0], by_name);
  return name_count;
}

// `dir/name`, with ".glb" replaced by `extension` when it is not empty.
const char *elisa_mocap_path(const char *dir, int64_t index, const char *extension) {
  if (index < 0 || index >= name_count) return "";
  const char *name = names[index];
  int stem = (int)strlen(name) - (extension[0] ? 4 : 0);
  char *out = joined[next_joined];
  next_joined = (next_joined + 1) % 4;
  snprintf(out, sizeof joined[0], "%s/%.*s%s", dir, stem, name, extension);
  return out;
}

const char *elisa_mocap_name(int64_t index) {
  return index >= 0 && index < name_count ? names[index] : "";
}

const char *elisa_mocap_env(const char *key, const char *fallback) {
  const char *v = getenv(key);
  return v && v[0] ? v : fallback;
}

void elisa_mocap_line(const char *status, const char *name, int64_t a, int64_t b) {
  printf("%s %s %lld %lld\n", status, name, (long long)a, (long long)b);
  fflush(stdout);
}
