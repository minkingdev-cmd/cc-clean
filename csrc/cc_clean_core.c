#include "cc_clean.h"

static const char *APP_NAME = "claude-cli-nodejs";
static const char *CHROME_NATIVE_HOST_MANIFEST =
    "com.anthropic.claude_code_browser_extension.json";

typedef struct {
  const char *suffix;
  const char *note;
} RelativePathSpec;

#ifdef _WIN32
static const char *CHROME_NATIVE_HOST_IDENTIFIER =
    "com.anthropic.claude_code_browser_extension";
static const char *WINDOWS_DEEP_LINK_PROTOCOL = "claude-cli";
static const char *WINDOWS_NATIVE_HOST_MANIFEST_DIR_SUFFIX =
    "Claude Code\\ChromeNativeHost";
static const char *WINDOWS_NATIVE_HOST_REGISTRY_BASES[] = {
    "Software\\Google\\Chrome\\NativeMessagingHosts",
    "Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts",
    "Software\\ArcBrowser\\Arc\\NativeMessagingHosts",
    "Software\\Chromium\\NativeMessagingHosts",
    "Software\\Microsoft\\Edge\\NativeMessagingHosts",
    "Software\\Vivaldi\\NativeMessagingHosts",
    "Software\\Opera Software\\Opera Stable\\NativeMessagingHosts",
    NULL,
};
static const char *DEFAULT_XDG_DATA_SUFFIX = ".local\\share";
static const char *DEFAULT_XDG_CACHE_SUFFIX = ".cache";
static const char *DEFAULT_XDG_STATE_SUFFIX = ".local\\state";
static const char *DEFAULT_USER_BIN_SUFFIX = ".local\\bin";
#else
static const char *DEFAULT_XDG_DATA_SUFFIX = ".local/share";
static const char *DEFAULT_XDG_CACHE_SUFFIX = ".cache";
static const char *DEFAULT_XDG_STATE_SUFFIX = ".local/state";
static const char *DEFAULT_USER_BIN_SUFFIX = ".local/bin";
#endif

/* ------------------------- 基础字符串工具 ------------------------- */

char *xstrdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = (char *)malloc(n);
  if (!p) {
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  memcpy(p, s, n);
  return p;
}

char *str_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int needed = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (needed < 0) {
    va_end(ap2);
    fprintf(stderr, "字符串格式化失败\n");
    exit(2);
  }
  char *buf = (char *)malloc((size_t)needed + 1);
  if (!buf) {
    va_end(ap2);
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  vsnprintf(buf, (size_t)needed + 1, fmt, ap2);
  va_end(ap2);
  return buf;
}

static bool starts_with(const char *s, const char *prefix) {
  size_t n = strlen(prefix);
  return strncmp(s, prefix, n) == 0;
}

static char *path_join(const char *a, const char *b) {
  size_t alen = strlen(a);
  size_t blen = strlen(b);
  bool need_sep = !(alen > 0 && (a[alen - 1] == '/' || a[alen - 1] == '\\'));
  char *out = (char *)malloc(alen + blen + (need_sep ? 2 : 1));
  if (!out) {
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  memcpy(out, a, alen);
  size_t pos = alen;
  if (need_sep) {
    out[pos++] = PATH_SEP;
  }
  memcpy(out + pos, b, blen);
  out[pos + blen] = '\0';
  return out;
}

static char *path_dirname(const char *path) {
  char *copy = xstrdup(path);
  size_t len = strlen(copy);
  while (len > 0 && (copy[len - 1] == '/' || copy[len - 1] == '\\')) {
    copy[--len] = '\0';
  }
  while (len > 0 && copy[len - 1] != '/' && copy[len - 1] != '\\') {
    copy[--len] = '\0';
  }
  while (len > 1 && (copy[len - 1] == '/' || copy[len - 1] == '\\')) {
    copy[--len] = '\0';
  }
  if (len == 0) {
#ifdef _WIN32
    free(copy);
    return xstrdup(".");
#else
    free(copy);
    return xstrdup("/");
#endif
  }
  return copy;
}

void normalize_separators(char *s) {
#ifdef _WIN32
  for (; *s; ++s) {
    if (*s == '/') {
      *s = '\\';
    }
  }
#else
  for (; *s; ++s) {
    if (*s == '\\') {
      *s = '/';
    }
  }
#endif
}

static void strip_trailing_separators(char *s) {
  size_t len = strlen(s);
#ifdef _WIN32
  size_t min_len = 1;
  if (len >= 3 && isalpha((unsigned char)s[0]) && s[1] == ':' &&
      (s[2] == '\\' || s[2] == '/')) {
    min_len = 3;
  }
#else
  size_t min_len = 1;
#endif
  while (len > min_len && (s[len - 1] == '/' || s[len - 1] == '\\')) {
    s[--len] = '\0';
  }
}

static bool is_sep_char(char c) {
  return c == '/' || c == '\\';
}

static int path_cmp_platform(const char *a, const char *b, size_t n) {
#ifdef _WIN32
  for (size_t i = 0; i < n; ++i) {
    char ca = (char)tolower((unsigned char)a[i]);
    char cb = (char)tolower((unsigned char)b[i]);
    if (ca != cb) return (int)((unsigned char)ca - (unsigned char)cb);
  }
  return 0;
#else
  return strncmp(a, b, n);
#endif
}

static bool path_equals_platform(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  if (la != lb) return false;
  return path_cmp_platform(a, b, la) == 0;
}

char *expand_home(const char *path, const char *home);

static char *canonicalish_path(const char *path, const char *home) {
  char *p = expand_home(path, home);
  normalize_separators(p);
  strip_trailing_separators(p);
  return p;
}

static bool path_is_same_or_within(const char *base, const char *candidate) {
  size_t blen = strlen(base);
  size_t clen = strlen(candidate);
  if (clen < blen) return false;
  if (path_cmp_platform(base, candidate, blen) != 0) return false;
  if (clen == blen) return true;
  return is_sep_char(candidate[blen]);
}

char *expand_home(const char *path, const char *home) {
  if (!path || !*path) {
    return xstrdup("");
  }
  if (path[0] == '~' && (path[1] == '\0' || path[1] == '/' || path[1] == '\\')) {
    const char *rest = path + 1;
    while (*rest == '/' || *rest == '\\') {
      rest++;
    }
    char *base = xstrdup(home);
    if (*rest == '\0') {
      return base;
    }
    char *joined = path_join(base, rest);
    free(base);
    return joined;
  }
  char *out = xstrdup(path);
  normalize_separators(out);
  return out;
}

static const char *get_env_dup(const char *name) {
  const char *v = getenv(name);
  return (v && *v) ? v : NULL;
}

/* ------------------------- 平台路径与文件操作 ------------------------- */

char *get_home_dir(void) {
#ifdef _WIN32
  const char *v = get_env_dup("USERPROFILE");
  if (!v) {
    v = get_env_dup("HOMEDRIVE");
    const char *p = get_env_dup("HOMEPATH");
    if (v && p) {
      return str_printf("%s%s", v, p);
    }
    v = get_env_dup("APPDATA");
  }
  if (!v) {
    fprintf(stderr, "无法确定 HOME/USERPROFILE\n");
    exit(2);
  }
  return xstrdup(v);
#else
  const char *v = get_env_dup("HOME");
  if (!v) {
    fprintf(stderr, "无法确定 HOME\n");
    exit(2);
  }
  return xstrdup(v);
#endif
}

static bool path_exists(const char *path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES;
#else
  struct stat st;
  return lstat(path, &st) == 0;
#endif
}

static int ensure_dir_recursive(const char *path) {
  if (!path || !*path) {
    return 0;
  }
  if (path_exists(path)) {
    return 0;
  }

  char *tmp = xstrdup(path);
  normalize_separators(tmp);
  size_t len = strlen(tmp);
  if (len == 0) {
    free(tmp);
    return 0;
  }

#ifdef _WIN32
  size_t start = 0;
  if (len >= 2 && isalpha((unsigned char)tmp[0]) && tmp[1] == ':') {
    start = 2;
    if (tmp[2] == '\\' || tmp[2] == '/') {
      start = 3;
    }
  } else if (len >= 2 && tmp[0] == '\\' && tmp[1] == '\\') {
    start = 2;
    int seps = 0;
    for (size_t i = 2; i < len; ++i) {
      if (tmp[i] == '\\' || tmp[i] == '/') {
        seps++;
        if (seps == 2) {
          start = i + 1;
          break;
        }
      }
    }
  }
#else
  size_t start = (tmp[0] == '/') ? 1 : 0;
#endif

  for (size_t i = start; i <= len; ++i) {
    if (tmp[i] == '\0' || tmp[i] == '/' || tmp[i] == '\\') {
      char saved = tmp[i];
      tmp[i] = '\0';
      if (*tmp && !path_exists(tmp)) {
#ifdef _WIN32
        if (_mkdir(tmp) != 0 && errno != EEXIST) {
          free(tmp);
          return 1;
        }
#else
        if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
          free(tmp);
          return 1;
        }
#endif
      }
      tmp[i] = saved;
    }
  }
  free(tmp);
  return 0;
}

static bool path_is_dir(const char *path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
    return false;
  }
  return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
  struct stat st;
  if (lstat(path, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
#endif
}

static bool path_is_symlink(const char *path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  return (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  struct stat st;
  if (lstat(path, &st) != 0) {
    return false;
  }
  return S_ISLNK(st.st_mode);
#endif
}

static const char *detect_kind(const char *path) {
  if (path_is_symlink(path)) {
    return "symlink";
  }
  if (path_is_dir(path)) {
    return "dir";
  }
  return "file";
}

static int remove_recursive(const char *path);

#ifdef _WIN32
static int remove_recursive_windows(const char *path) {
  DWORD attr = GetFileAttributesA(path);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    return 0;
  }

  if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
      return RemoveDirectoryA(path) ? 0 : 1;
    }
    return DeleteFileA(path) ? 0 : 1;
  }

  if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    return DeleteFileA(path) ? 0 : 1;
  }

  char pattern[PATH_MAX_LEN];
  snprintf(pattern, sizeof(pattern), "%s\\*", path);

  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
        continue;
      }
      char child[PATH_MAX_LEN];
      snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
      if (remove_recursive(child) != 0) {
        FindClose(h);
        return 1;
      }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  return RemoveDirectoryA(path) ? 0 : 1;
}
#else
static int remove_recursive_posix(const char *path) {
  struct stat st;
  if (lstat(path, &st) != 0) {
    return 0;
  }

  if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
    return unlink(path);
  }

  DIR *dir = opendir(path);
  if (!dir) {
    return -1;
  }

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    char *child = path_join(path, ent->d_name);
    int rc = remove_recursive(child);
    free(child);
    if (rc != 0) {
      closedir(dir);
      return rc;
    }
  }
  closedir(dir);
  return rmdir(path);
}
#endif

static int remove_recursive(const char *path) {
#ifdef _WIN32
  return remove_recursive_windows(path);
#else
  return remove_recursive_posix(path);
#endif
}

static int copy_file_binary(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  if (!in) {
    return 1;
  }
  char *dir = path_dirname(dst);
  if (ensure_dir_recursive(dir) != 0) {
    free(dir);
    fclose(in);
    return 1;
  }
  free(dir);
  FILE *out = fopen(dst, "wb");
  if (!out) {
    fclose(in);
    return 1;
  }
  unsigned char buf[65536];
  size_t n;
  int rc = 0;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      rc = 1;
      break;
    }
  }
  if (ferror(in)) {
    rc = 1;
  }
  fclose(in);
  fclose(out);
  return rc;
}

/* ------------------------- SHA256（仅用于 keychain 名称 hash） ------------------------- */

typedef struct {
  uint8_t data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
} SHA256_CTX;

static uint32_t rotr32(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

static uint32_t ch32(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (~x & z);
}

static uint32_t maj32(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t ep0(uint32_t x) {
  return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
}

static uint32_t ep1(uint32_t x) {
  return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
}

static uint32_t sig0(uint32_t x) {
  return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
}

static uint32_t sig1(uint32_t x) {
  return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
}

static const uint32_t k256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
    0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
    0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
  uint32_t m[64];
  uint32_t a, b, c, d, e, f, g, h;
  size_t i;

  for (i = 0; i < 16; ++i) {
    m[i] = (uint32_t)data[i * 4] << 24 |
           (uint32_t)data[i * 4 + 1] << 16 |
           (uint32_t)data[i * 4 + 2] << 8 |
           (uint32_t)data[i * 4 + 3];
  }
  for (i = 16; i < 64; ++i) {
    m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  for (i = 0; i < 64; ++i) {
    uint32_t t1 = h + ep1(e) + ch32(e, f, g) + k256[i] + m[i];
    uint32_t t2 = ep0(a) + maj32(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    ctx->data[ctx->datalen] = data[i];
    ctx->datalen++;
    if (ctx->datalen == 64) {
      sha256_transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[32]) {
  uint32_t i = ctx->datalen;

  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56) {
      ctx->data[i++] = 0x00;
    }
  } else {
    ctx->data[i++] = 0x80;
    while (i < 64) {
      ctx->data[i++] = 0x00;
    }
    sha256_transform(ctx, ctx->data);
    memset(ctx->data, 0, 56);
  }

  ctx->bitlen += (uint64_t)ctx->datalen * 8;
  ctx->data[63] = (uint8_t)(ctx->bitlen);
  ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
  ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
  ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
  ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
  ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
  ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
  ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
  sha256_transform(ctx, ctx->data);

  for (i = 0; i < 4; ++i) {
    hash[i] = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0xff);
    hash[i + 4] = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0xff);
    hash[i + 8] = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0xff);
    hash[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0xff);
    hash[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0xff);
    hash[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0xff);
    hash[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0xff);
    hash[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0xff);
  }
}

static char *sha256_hex8(const char *input) {
  SHA256_CTX ctx;
  uint8_t hash[32];
  sha256_init(&ctx);
  sha256_update(&ctx, (const uint8_t *)input, strlen(input));
  sha256_final(&ctx, hash);
  char *out = (char *)malloc(9);
  if (!out) {
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  for (int i = 0; i < 4; ++i) {
    snprintf(out + i * 2, 3, "%02x", hash[i]);
  }
  out[8] = '\0';
  return out;
}

/* ------------------------- Artifact 列表 ------------------------- */

void artifact_list_init(ArtifactList *list) {
  list->items = NULL;
  list->len = 0;
  list->cap = 0;
}

static void artifact_list_push(ArtifactList *list, Artifact a) {
  if (list->len == list->cap) {
    size_t next = list->cap == 0 ? 16 : list->cap * 2;
    Artifact *p = (Artifact *)realloc(list->items, next * sizeof(Artifact));
    if (!p) {
      fprintf(stderr, "内存分配失败\n");
      exit(2);
    }
    list->items = p;
    list->cap = next;
  }
  list->items[list->len++] = a;
}

void artifact_list_free(ArtifactList *list) {
  for (size_t i = 0; i < list->len; ++i) {
    free(list->items[i].kind);
    free(list->items[i].identifier);
    free(list->items[i].confidence);
    free(list->items[i].note);
    free(list->items[i].detail);
  }
  free(list->items);
  list->items = NULL;
  list->len = 0;
  list->cap = 0;
}

static Artifact make_artifact(const char *kind,
                              const char *identifier,
                              bool exists,
                              const char *confidence,
                              bool safe_clean,
                              const char *note) {
  Artifact a;
  a.kind = xstrdup(kind);
  a.identifier = xstrdup(identifier);
  a.exists = exists;
  a.confidence = xstrdup(confidence);
  a.safe_clean = safe_clean;
  a.note = xstrdup(note);
  a.detail = xstrdup("");
  return a;
}

#ifdef _WIN32
static Artifact make_artifact_with_detail(const char *kind,
                                          const char *identifier,
                                          bool exists,
                                          const char *confidence,
                                          bool safe_clean,
                                          const char *note,
                                          const char *detail) {
  Artifact a = make_artifact(kind, identifier, exists, confidence, safe_clean, note);
  free(a.detail);
  a.detail = xstrdup(detail ? detail : "");
  return a;
}
#endif

/* ------------------------- 路径与输出表示 ------------------------- */

char *tilde_path(const char *path, const char *home) {
  if (starts_with(path, home)) {
    if (path[strlen(home)] == '\0') {
      return xstrdup("~");
    }
    if (path[strlen(home)] == '/' || path[strlen(home)] == '\\') {
      return str_printf("~%s", path + strlen(home));
    }
  }
  return xstrdup(path);
}

static char *field_encode(const char *s) {
  if (!s) return xstrdup("");
  size_t len = strlen(s);
  char *out = (char *)malloc(len * 3 + 1);
  if (!out) {
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  char *p = out;
  for (size_t i = 0; i < len; ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c == '%' || c == '\t' || c == '\n' || c == '\r') {
      sprintf(p, "%%%02X", c);
      p += 3;
    } else {
      *p++ = (char)c;
    }
  }
  *p = '\0';
  return out;
}

static char hex_val(char c) {
  if (c >= '0' && c <= '9') return (char)(c - '0');
  if (c >= 'A' && c <= 'F') return (char)(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return (char)(c - 'a' + 10);
  return -1;
}

static char *field_decode(const char *s) {
  if (!s) return xstrdup("");
  size_t len = strlen(s);
  char *out = (char *)malloc(len + 1);
  if (!out) {
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  char *p = out;
  for (size_t i = 0; i < len; ++i) {
    if (s[i] == '%' && i + 2 < len) {
      char hi = hex_val(s[i + 1]);
      char lo = hex_val(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        *p++ = (char)((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    *p++ = s[i];
  }
  *p = '\0';
  return out;
}

#ifdef _WIN32
static char *bytes_to_hex(const unsigned char *data, size_t len) {
  char *out = (char *)malloc(len * 2 + 1);
  if (!out) {
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  for (size_t i = 0; i < len; ++i) {
    sprintf(out + i * 2, "%02X", data[i]);
  }
  out[len * 2] = '\0';
  return out;
}

static unsigned char *hex_to_bytes(const char *hex, size_t *out_len) {
  size_t len = strlen(hex);
  if (len % 2 != 0) return NULL;
  unsigned char *buf = (unsigned char *)malloc(len / 2);
  if (!buf) {
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  for (size_t i = 0; i < len; i += 2) {
    char hi = hex_val(hex[i]);
    char lo = hex_val(hex[i + 1]);
    if (hi < 0 || lo < 0) {
      free(buf);
      return NULL;
    }
    buf[i / 2] = (unsigned char)((hi << 4) | lo);
  }
  *out_len = len / 2;
  return buf;
}
#endif

static Artifact path_artifact(const char *path,
                              const char *home,
                              const char *confidence,
                              bool safe_clean,
                              const char *note) {
  bool exists = path_exists(path);
  char *display = tilde_path(path, home);
  Artifact a = make_artifact(detect_kind(path), display, exists, confidence,
                             safe_clean, note);
  free(display);
  return a;
}

static Artifact path_artifact_as_kind(const char *kind,
                                      const char *path,
                                      bool exists,
                                      const char *home,
                                      const char *confidence,
                                      bool safe_clean,
                                      const char *note) {
  char *display = tilde_path(path, home);
  Artifact a = make_artifact(kind, display, exists, confidence, safe_clean, note);
  free(display);
  return a;
}

static char *absolute_to_backup_rel(const char *abs_path) {
  char *norm = xstrdup(abs_path);
  normalize_separators(norm);
#ifdef _WIN32
  if (strlen(norm) >= 2 && isalpha((unsigned char)norm[0]) && norm[1] == ':') {
    char drive = norm[0];
    const char *rest = norm + 2;
    while (*rest == '\\' || *rest == '/') rest++;
    char *rest2 = xstrdup(rest);
    for (char *p = rest2; *p; ++p) {
      if (*p == '\\') *p = '/';
    }
    char *out = str_printf("files/Windows/%c/%s", drive, rest2);
    free(rest2);
    free(norm);
    return out;
  }
  if (starts_with(norm, "\\\\")) {
    const char *rest = norm + 2;
    char *rest2 = xstrdup(rest);
    for (char *p = rest2; *p; ++p) {
      if (*p == '\\') *p = '/';
    }
    char *out = str_printf("files/Windows/UNC/%s", rest2);
    free(rest2);
    free(norm);
    return out;
  }
#endif
  const char *rest = norm;
  while (*rest == '/' || *rest == '\\') rest++;
  char *rest2 = xstrdup(rest);
  for (char *p = rest2; *p; ++p) {
    if (*p == '\\') *p = '/';
  }
  char *out = str_printf("files/%s", rest2);
  free(rest2);
  free(norm);
  return out;
}

static int copy_tree_recursive(const char *src, const char *dst) {
  if (path_is_dir(src)) {
    if (ensure_dir_recursive(dst) != 0) {
      return 1;
    }
#ifdef _WIN32
    char pattern[PATH_MAX_LEN];
    snprintf(pattern, sizeof(pattern), "%s\\*", src);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
      return 0;
    }
    do {
      if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
      char *src_child = path_join(src, fd.cFileName);
      char *dst_child = path_join(dst, fd.cFileName);
      int rc = copy_tree_recursive(src_child, dst_child);
      free(src_child);
      free(dst_child);
      if (rc != 0) {
        FindClose(h);
        return rc;
      }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(src);
    if (!dir) return 1;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
      char *src_child = path_join(src, ent->d_name);
      char *dst_child = path_join(dst, ent->d_name);
      int rc = copy_tree_recursive(src_child, dst_child);
      free(src_child);
      free(dst_child);
      if (rc != 0) {
        closedir(dir);
        return rc;
      }
    }
    closedir(dir);
#endif
    return 0;
  }
#ifndef _WIN32
  if (path_is_symlink(src)) {
    char target[PATH_MAX_LEN];
    ssize_t n = readlink(src, target, sizeof(target) - 1);
    if (n < 0) return 1;
    target[n] = '\0';
    char *dir = path_dirname(dst);
    if (ensure_dir_recursive(dir) != 0) {
      free(dir);
      return 1;
    }
    free(dir);
    unlink(dst);
    return symlink(target, dst);
  }
#endif
  return copy_file_binary(src, dst);
}

void backup_context_close(BackupContext *ctx);

bool backup_context_init(BackupContext *ctx, const char *backup_dir) {
  memset(ctx, 0, sizeof(*ctx));
  if (!backup_dir || !*backup_dir) {
    return false;
  }
  ctx->backup_dir = xstrdup(backup_dir);
  ctx->files_root = path_join(backup_dir, "files");
  if (ensure_dir_recursive(ctx->backup_dir) != 0 ||
      ensure_dir_recursive(ctx->files_root) != 0) {
    backup_context_close(ctx);
    return false;
  }
  char *manifest_path = path_join(backup_dir, "manifest.tsv");
  char *index_path = path_join(backup_dir, "index.md");
  ctx->manifest_fp = fopen(manifest_path, "wb");
  ctx->index_fp = fopen(index_path, "wb");
  free(manifest_path);
  free(index_path);
  if (!ctx->manifest_fp || !ctx->index_fp) {
    backup_context_close(ctx);
    return false;
  }
  time_t now = time(NULL);
  fprintf(ctx->manifest_fp, "VERSION\t2\n");
  fprintf(ctx->index_fp, "# Claude Fingerprint Backup\n\n");
  fprintf(ctx->index_fp, "- created_at: %lld\n", (long long)now);
#ifdef _WIN32
  fprintf(ctx->index_fp, "- platform: windows\n\n");
#elif defined(__APPLE__)
  fprintf(ctx->index_fp, "- platform: macos\n\n");
#else
  fprintf(ctx->index_fp, "- platform: linux\n\n");
#endif
  fprintf(ctx->index_fp, "- restore_command: `cc-clean restore --backup-dir \"%s\"`\n\n",
          ctx->backup_dir);
  fprintf(ctx->index_fp, "## Entries\n\n");
  ctx->enabled = true;
  return true;
}

void backup_context_close(BackupContext *ctx) {
  if (ctx->manifest_fp) fclose(ctx->manifest_fp);
  if (ctx->index_fp) fclose(ctx->index_fp);
  free(ctx->backup_dir);
  free(ctx->files_root);
  memset(ctx, 0, sizeof(*ctx));
}

/* ------------------------- 平台路径生成 ------------------------- */

char *get_default_config_home(const char *home) {
  const char *override_dir = get_env_dup("CLAUDE_CONFIG_DIR");
  if (override_dir) {
    return expand_home(override_dir, home);
  }
  return path_join(home, ".claude");
}

static void get_env_paths(const char *home,
                          char **data_out,
                          char **config_out,
                          char **cache_out,
                          char **log_out) {
#ifdef _WIN32
  const char *appdata = get_env_dup("APPDATA");
  const char *localappdata = get_env_dup("LOCALAPPDATA");
  char *owned_appdata = NULL;
  char *owned_localappdata = NULL;
  if (!appdata) {
    char *tmp = path_join(home, "AppData\\Roaming");
    owned_appdata = xstrdup(tmp);
    appdata = owned_appdata;
    free(tmp);
  }
  if (!localappdata) {
    char *tmp = path_join(home, "AppData\\Local");
    owned_localappdata = xstrdup(tmp);
    localappdata = owned_localappdata;
    free(tmp);
  }
  *data_out = path_join(localappdata, "claude-cli-nodejs\\Data");
  *config_out = path_join(appdata, "claude-cli-nodejs\\Config");
  *cache_out = path_join(localappdata, "claude-cli-nodejs\\Cache");
  *log_out = path_join(localappdata, "claude-cli-nodejs\\Log");
  free(owned_appdata);
  free(owned_localappdata);
#elif defined(__APPLE__)
  char *mac_data_suffix = str_printf("Library/Application Support/%s", APP_NAME);
  char *mac_config_suffix = str_printf("Library/Preferences/%s", APP_NAME);
  char *mac_cache_suffix = str_printf("Library/Caches/%s", APP_NAME);
  char *mac_log_suffix = str_printf("Library/Logs/%s", APP_NAME);
  *data_out = path_join(home, mac_data_suffix);
  *config_out = path_join(home, mac_config_suffix);
  *cache_out = path_join(home, mac_cache_suffix);
  *log_out = path_join(home, mac_log_suffix);
  free(mac_data_suffix);
  free(mac_config_suffix);
  free(mac_cache_suffix);
  free(mac_log_suffix);
#else
  const char *xdg_data = get_env_dup("XDG_DATA_HOME");
  const char *xdg_config = get_env_dup("XDG_CONFIG_HOME");
  const char *xdg_cache = get_env_dup("XDG_CACHE_HOME");
  const char *xdg_state = get_env_dup("XDG_STATE_HOME");
  char *d1 = xdg_data ? xstrdup(xdg_data) : path_join(home, ".local/share");
  char *d2 = xdg_config ? xstrdup(xdg_config) : path_join(home, ".config");
  char *d3 = xdg_cache ? xstrdup(xdg_cache) : path_join(home, ".cache");
  char *d4 = xdg_state ? xstrdup(xdg_state) : path_join(home, ".local/state");
  *data_out = path_join(d1, APP_NAME);
  *config_out = path_join(d2, APP_NAME);
  *cache_out = path_join(d3, APP_NAME);
  *log_out = path_join(d4, APP_NAME);
  free(d1);
  free(d2);
  free(d3);
  free(d4);
#endif
}


static char *dup_env_or_join(const char *env_name,
                             const char *home,
                             const char *fallback_suffix) {
  const char *v = get_env_dup(env_name);
  return v ? xstrdup(v) : path_join(home, fallback_suffix);
}

static void add_relative_artifact(ArtifactList *list,
                                  const char *base,
                                  const char *suffix,
                                  const char *home,
                                  const char *confidence,
                                  bool safe_clean,
                                  const char *note) {
  char *path = path_join(base, suffix);
  artifact_list_push(list, path_artifact(path, home, confidence, safe_clean, note));
  free(path);
}

static void add_relative_artifacts(ArtifactList *list,
                                   const char *base,
                                   const char *home,
                                   const char *confidence,
                                   bool safe_clean,
                                   const RelativePathSpec *specs,
                                   size_t count) {
  for (size_t i = 0; i < count; ++i) {
    add_relative_artifact(list, base, specs[i].suffix, home, confidence,
                          safe_clean, specs[i].note);
  }
}

static void get_native_installer_paths(const char *home,
                                       char **versions_out,
                                       char **staging_out,
                                       char **locks_out,
                                       char **executable_out) {
  char *data_home = dup_env_or_join("XDG_DATA_HOME", home, DEFAULT_XDG_DATA_SUFFIX);
  char *cache_home = dup_env_or_join("XDG_CACHE_HOME", home, DEFAULT_XDG_CACHE_SUFFIX);
  char *state_home = dup_env_or_join("XDG_STATE_HOME", home, DEFAULT_XDG_STATE_SUFFIX);
  char *user_bin = path_join(home, DEFAULT_USER_BIN_SUFFIX);
  char *claude_data = path_join(data_home, "claude");
  char *claude_cache = path_join(cache_home, "claude");
  char *claude_state = path_join(state_home, "claude");
#ifdef _WIN32
  const char *executable_name = "claude.exe";
#else
  const char *executable_name = "claude";
#endif
  *versions_out = path_join(claude_data, "versions");
  *staging_out = path_join(claude_cache, "staging");
  *locks_out = path_join(claude_state, "locks");
  *executable_out = path_join(user_bin, executable_name);
  free(data_home);
  free(cache_home);
  free(state_home);
  free(user_bin);
  free(claude_data);
  free(claude_cache);
  free(claude_state);
}

static void add_native_installer_artifacts(ArtifactList *runtime, const char *home) {
  char *versions = NULL;
  char *staging = NULL;
  char *locks = NULL;
  char *executable = NULL;
  get_native_installer_paths(home, &versions, &staging, &locks, &executable);
  artifact_list_push(runtime, path_artifact(versions, home, "high", true,
                                            "Native installer 版本目录"));
  artifact_list_push(runtime, path_artifact(staging, home, "high", true,
                                            "Native installer staging 缓存"));
  artifact_list_push(runtime, path_artifact(locks, home, "high", true,
                                            "Native installer 锁目录"));
  artifact_list_push(runtime, path_artifact(executable, home, "high", true,
                                            "Native installer 用户级可执行文件"));
  free(versions);
  free(staging);
  free(locks);
  free(executable);
}

#ifndef _WIN32
#ifndef __APPLE__
static char *get_linux_deep_link_desktop_path(const char *home) {
  char *data_home = dup_env_or_join("XDG_DATA_HOME", home, DEFAULT_XDG_DATA_SUFFIX);
  char *applications = path_join(data_home, "applications");
  char *desktop = path_join(applications, "claude-code-url-handler.desktop");
  free(data_home);
  free(applications);
  return desktop;
}
#endif
#endif

static void add_browser_native_host_artifacts(ArtifactList *runtime,
                                              const char *config_home,
                                              const char *home) {
  char *chrome_dir = path_join(config_home, "chrome");
  artifact_list_push(runtime, path_artifact(chrome_dir, home, "high", true,
                                            "Claude in Chrome 运行目录"));
  free(chrome_dir);

#ifdef _WIN32
  const char *appdata = get_env_dup("APPDATA");
  char *base = appdata ? xstrdup(appdata) : path_join(home, "AppData\\Local");
  char *manifest_dir = path_join(base, WINDOWS_NATIVE_HOST_MANIFEST_DIR_SUFFIX);
  char *manifest_path = path_join(manifest_dir, CHROME_NATIVE_HOST_MANIFEST);
  artifact_list_push(runtime, path_artifact(manifest_path, home, "high", true,
                                            "Windows Claude in Chrome Native Host manifest"));
  free(base);
  free(manifest_dir);
  free(manifest_path);
#elif defined(__APPLE__)
  const char *dirs[] = {
      "Library/Application Support/Google/Chrome/NativeMessagingHosts",
      "Library/Application Support/BraveSoftware/Brave-Browser/NativeMessagingHosts",
      "Library/Application Support/Arc/User Data/NativeMessagingHosts",
      "Library/Application Support/Chromium/NativeMessagingHosts",
      "Library/Application Support/Microsoft Edge/NativeMessagingHosts",
      "Library/Application Support/Vivaldi/NativeMessagingHosts",
      "Library/Application Support/com.operasoftware.Opera/NativeMessagingHosts",
      NULL,
  };
  for (size_t i = 0; dirs[i] != NULL; ++i) {
    char *dir = path_join(home, dirs[i]);
    char *manifest = path_join(dir, CHROME_NATIVE_HOST_MANIFEST);
    artifact_list_push(runtime, path_artifact(manifest, home, "high", true,
                                              "浏览器 Native Messaging Host manifest"));
    free(dir);
    free(manifest);
  }
#else
  const char *dirs[] = {
      ".config/google-chrome/NativeMessagingHosts",
      ".config/BraveSoftware/Brave-Browser/NativeMessagingHosts",
      ".config/chromium/NativeMessagingHosts",
      ".config/microsoft-edge/NativeMessagingHosts",
      ".config/vivaldi/NativeMessagingHosts",
      ".config/opera/NativeMessagingHosts",
      NULL,
  };
  for (size_t i = 0; dirs[i] != NULL; ++i) {
    char *dir = path_join(home, dirs[i]);
    char *manifest = path_join(dir, CHROME_NATIVE_HOST_MANIFEST);
    artifact_list_push(runtime, path_artifact(manifest, home, "high", true,
                                              "浏览器 Native Messaging Host manifest"));
    free(dir);
    free(manifest);
  }
#endif
}

static bool is_known_native_installer_path(const char *path, const char *home) {
  char *versions = NULL;
  char *staging = NULL;
  char *locks = NULL;
  char *executable = NULL;
  get_native_installer_paths(home, &versions, &staging, &locks, &executable);
  bool ok = path_is_same_or_within(versions, path) ||
            path_is_same_or_within(staging, path) ||
            path_is_same_or_within(locks, path) ||
            path_is_same_or_within(executable, path);
  free(versions);
  free(staging);
  free(locks);
  free(executable);
  return ok;
}

static bool is_known_browser_integration_path(const char *path,
                                              const char *config_home,
                                              const char *home) {
  char *chrome_dir = path_join(config_home, "chrome");
  if (path_is_same_or_within(chrome_dir, path)) {
    free(chrome_dir);
    return true;
  }
  free(chrome_dir);

#ifdef _WIN32
  const char *appdata = get_env_dup("APPDATA");
  char *base = appdata ? xstrdup(appdata) : path_join(home, "AppData\\Local");
  char *manifest_dir = path_join(base, WINDOWS_NATIVE_HOST_MANIFEST_DIR_SUFFIX);
  char *manifest_path = path_join(manifest_dir, CHROME_NATIVE_HOST_MANIFEST);
  bool ok = path_equals_platform(manifest_path, path);
  free(base);
  free(manifest_dir);
  free(manifest_path);
  return ok;
#elif defined(__APPLE__)
  char *deep_link_app = path_join(home, "Applications/Claude Code URL Handler.app");
  if (path_is_same_or_within(deep_link_app, path)) {
    free(deep_link_app);
    return true;
  }
  free(deep_link_app);
  const char *dirs[] = {
      "Library/Application Support/Google/Chrome/NativeMessagingHosts",
      "Library/Application Support/BraveSoftware/Brave-Browser/NativeMessagingHosts",
      "Library/Application Support/Arc/User Data/NativeMessagingHosts",
      "Library/Application Support/Chromium/NativeMessagingHosts",
      "Library/Application Support/Microsoft Edge/NativeMessagingHosts",
      "Library/Application Support/Vivaldi/NativeMessagingHosts",
      "Library/Application Support/com.operasoftware.Opera/NativeMessagingHosts",
      NULL,
  };
  for (size_t i = 0; dirs[i] != NULL; ++i) {
    char *dir = path_join(home, dirs[i]);
    char *manifest = path_join(dir, CHROME_NATIVE_HOST_MANIFEST);
    bool match = path_equals_platform(manifest, path);
    free(dir);
    free(manifest);
    if (match) {
      return true;
    }
  }
  return false;
#else
  char *desktop = get_linux_deep_link_desktop_path(home);
  if (path_equals_platform(desktop, path)) {
    free(desktop);
    return true;
  }
  free(desktop);
  const char *dirs[] = {
      ".config/google-chrome/NativeMessagingHosts",
      ".config/BraveSoftware/Brave-Browser/NativeMessagingHosts",
      ".config/chromium/NativeMessagingHosts",
      ".config/microsoft-edge/NativeMessagingHosts",
      ".config/vivaldi/NativeMessagingHosts",
      ".config/opera/NativeMessagingHosts",
      NULL,
  };
  for (size_t i = 0; dirs[i] != NULL; ++i) {
    char *dir = path_join(home, dirs[i]);
    char *manifest = path_join(dir, CHROME_NATIVE_HOST_MANIFEST);
    bool match = path_equals_platform(manifest, path);
    free(dir);
    free(manifest);
    if (match) {
      return true;
    }
  }
  return false;
#endif
}

static int read_text_file(const char *path, char **out) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return 1;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return 1;
  }
  long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return 1;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return 1;
  }
  char *buf = (char *)malloc((size_t)size + 1);
  if (!buf) {
    fclose(fp);
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  size_t n = fread(buf, 1, (size_t)size, fp);
  fclose(fp);
  if (n != (size_t)size) {
    free(buf);
    return 1;
  }
  buf[n] = '\0';
  *out = buf;
  return 0;
}

static int write_text_file(const char *path, const char *content) {
  char *dir = path_dirname(path);
  if (ensure_dir_recursive(dir) != 0) {
    free(dir);
    return 1;
  }
  free(dir);
  FILE *fp = fopen(path, "wb");
  if (!fp) return 1;
  size_t len = strlen(content);
  size_t n = fwrite(content, 1, len, fp);
  fclose(fp);
  return n == len ? 0 : 1;
}

static void append_buffer(char **buf,
                          size_t *len,
                          size_t *cap,
                          const char *text) {
  size_t add = strlen(text);
  if (*len + add + 1 > *cap) {
    size_t next = *cap == 0 ? 1024 : *cap;
    while (*len + add + 1 > next) next *= 2;
    char *p = (char *)realloc(*buf, next);
    if (!p) {
      fprintf(stderr, "内存分配失败\n");
      exit(2);
    }
    *buf = p;
    *cap = next;
  }
  memcpy(*buf + *len, text, add);
  *len += add;
  (*buf)[*len] = '\0';
}

static void append_line_buffer(char **buf,
                               size_t *len,
                               size_t *cap,
                               const char *line,
                               bool *first_line) {
  if (!*first_line) {
    append_buffer(buf, len, cap, "\n");
  }
  append_buffer(buf, len, cap, line);
  *first_line = false;
}

static char *trim_copy(const char *s) {
  const char *start = s;
  while (*start && isspace((unsigned char)*start)) start++;
  const char *end = s + strlen(s);
  while (end > start && isspace((unsigned char)end[-1])) end--;
  size_t len = (size_t)(end - start);
  char *out = (char *)malloc(len + 1);
  if (!out) {
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  memcpy(out, start, len);
  out[len] = '\0';
  return out;
}

static void trim_trailing_newlines_simple(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
    s[--n] = '\0';
  }
}

static void artifact_list_push_unique(ArtifactList *list, Artifact a) {
  for (size_t i = 0; i < list->len; ++i) {
    if (strcmp(list->items[i].kind, a.kind) == 0 &&
        strcmp(list->items[i].identifier, a.identifier) == 0) {
      free(a.kind);
      free(a.identifier);
      free(a.confidence);
      free(a.note);
      free(a.detail);
      return;
    }
  }
  artifact_list_push(list, a);
}

static bool component_starts_with_any(const char *path,
                                      const char *const *prefixes) {
  const char *p = path;
  while (*p) {
    while (*p && is_sep_char(*p)) p++;
    if (!*p) break;
    const char *start = p;
    while (*p && !is_sep_char(*p)) p++;
    size_t len = (size_t)(p - start);
    for (size_t i = 0; prefixes[i] != NULL; ++i) {
      size_t plen = strlen(prefixes[i]);
      if (len >= plen && path_cmp_platform(start, prefixes[i], plen) == 0) {
        return true;
      }
    }
  }
  return false;
}

static void add_matching_children_artifacts(ArtifactList *runtime,
                                            const char *base_dir,
                                            const char *home,
                                            const char *const *prefixes,
                                            const char *note) {
  if (!path_exists(base_dir)) return;
#ifdef _WIN32
  char pattern[PATH_MAX_LEN];
  snprintf(pattern, sizeof(pattern), "%s\\*", base_dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
    bool match = false;
    for (size_t i = 0; prefixes[i] != NULL; ++i) {
      if (path_cmp_platform(fd.cFileName, prefixes[i], strlen(prefixes[i])) == 0) {
        match = true;
        break;
      }
    }
    if (!match) continue;
    char *full = path_join(base_dir, fd.cFileName);
    if (path_exists(full)) {
      artifact_list_push_unique(runtime, path_artifact(full, home, "high", true, note));
    }
    free(full);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
#else
  DIR *dir = opendir(base_dir);
  if (!dir) return;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
    bool match = false;
    for (size_t i = 0; prefixes[i] != NULL; ++i) {
      if (strncmp(ent->d_name, prefixes[i], strlen(prefixes[i])) == 0) {
        match = true;
        break;
      }
    }
    if (!match) continue;
    char *full = path_join(base_dir, ent->d_name);
    if (path_exists(full) || path_is_symlink(full)) {
      artifact_list_push_unique(runtime, path_artifact(full, home, "high", true, note));
    }
    free(full);
  }
  closedir(dir);
#endif
}

static char *get_shell_completion_cache_path(const char *config_home,
                                             const char *shell_name) {
  if (strcmp(shell_name, "zsh") == 0) return path_join(config_home, "completion.zsh");
  if (strcmp(shell_name, "bash") == 0) return path_join(config_home, "completion.bash");
  return path_join(config_home, "completion.fish");
}

static void get_shell_config_paths(const char *home,
                                   char **zsh_out,
                                   char **bash_out,
                                   char **fish_out) {
  const char *zdotdir = get_env_dup("ZDOTDIR");
  const char *xdg_config = get_env_dup("XDG_CONFIG_HOME");
  char *zsh_base = zdotdir ? xstrdup(zdotdir) : xstrdup(home);
  char *fish_base = xdg_config ? xstrdup(xdg_config) : path_join(home, ".config");
  *zsh_out = path_join(zsh_base, ".zshrc");
  *bash_out = path_join(home, ".bashrc");
  char *fish_dir = path_join(fish_base, "fish");
  *fish_out = path_join(fish_dir, "config.fish");
  free(zsh_base);
  free(fish_base);
  free(fish_dir);
}

static bool line_is_claude_completion_comment(const char *line) {
  return strstr(line, "Claude Code shell completions") != NULL;
}

static bool line_is_completion_source(const char *line,
                                      const char *cache_path) {
  if (strstr(line, "claude completion") != NULL) return true;
  char *norm_line = xstrdup(line);
  char *norm_cache = xstrdup(cache_path);
  normalize_separators(norm_line);
  normalize_separators(norm_cache);
  bool found = strstr(norm_line, norm_cache) != NULL;
  free(norm_line);
  free(norm_cache);
  return found;
}

static bool line_is_default_claude_alias(const char *line,
                                         const char *local_claude_path) {
  const char *p = line;
  while (*p && isspace((unsigned char)*p)) p++;
  if (!starts_with(p, "alias")) return false;
  p += 5;
  while (*p && isspace((unsigned char)*p)) p++;
  if (!starts_with(p, "claude")) return false;
  p += 6;
  while (*p && isspace((unsigned char)*p)) p++;
  if (*p != '=') return false;
  p++;
  while (*p && isspace((unsigned char)*p)) p++;
  char quote = 0;
  if (*p == '\'' || *p == '"') {
    quote = *p;
    p++;
  }
  const char *start = p;
  while (*p) {
    if (quote) {
      if (*p == quote) break;
    } else if (*p == '#' || *p == '\n' || *p == '\r') {
      break;
    }
    p++;
  }
  size_t len = (size_t)(p - start);
  char *target = (char *)malloc(len + 1);
  if (!target) {
    fprintf(stderr, "内存分配失败\n");
    exit(2);
  }
  memcpy(target, start, len);
  target[len] = '\0';
  char *trimmed = trim_copy(target);
  free(target);
  char *norm_trimmed = xstrdup(trimmed);
  char *norm_local = xstrdup(local_claude_path);
  normalize_separators(norm_trimmed);
  normalize_separators(norm_local);
  bool ok = path_equals_platform(norm_trimmed, norm_local);
  free(norm_trimmed);
  free(norm_local);
  free(trimmed);
  return ok;
}

static bool shell_config_has_claude_setup(const char *path,
                                          const char *cache_path,
                                          const char *local_claude_path) {
  char *content = NULL;
  if (read_text_file(path, &content) != 0) return false;
  bool found = false;
  char *copy = xstrdup(content);
  char *save = NULL;
  for (char *line = strtok_r(copy, "\n", &save); line != NULL;
       line = strtok_r(NULL, "\n", &save)) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';
    if (line_is_claude_completion_comment(line) ||
        line_is_completion_source(line, cache_path) ||
        line_is_default_claude_alias(line, local_claude_path)) {
      found = true;
      break;
    }
  }
  free(copy);
  free(content);
  return found;
}

static int cleanup_shell_config_file(const char *path,
                                     const char *cache_path,
                                     const char *local_claude_path) {
  char *content = NULL;
  if (read_text_file(path, &content) != 0) return 1;
  char *copy = xstrdup(content);
  char *out = NULL;
  size_t out_len = 0, out_cap = 0;
  bool first = true;
  char *save = NULL;
  for (char *line = strtok_r(copy, "\n", &save); line != NULL;
       line = strtok_r(NULL, "\n", &save)) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';
    if (line_is_claude_completion_comment(line) ||
        line_is_completion_source(line, cache_path) ||
        line_is_default_claude_alias(line, local_claude_path)) {
      continue;
    }
    append_line_buffer(&out, &out_len, &out_cap, line, &first);
  }
  if (!out) out = xstrdup("");
  int rc = write_text_file(path, out);
  free(out);
  free(copy);
  free(content);
  return rc;
}

static void add_shell_config_artifacts(ArtifactList *runtime,
                                       const char *config_home,
                                       const char *home) {
  char *zshrc = NULL;
  char *bashrc = NULL;
  char *fishrc = NULL;
  get_shell_config_paths(home, &zshrc, &bashrc, &fishrc);
  const char *names[] = {"zsh", "bash", "fish"};
  char *paths[] = {zshrc, bashrc, fishrc};
  for (size_t i = 0; i < 3; ++i) {
    char *cache_path = get_shell_completion_cache_path(config_home, names[i]);
    char *local_claude_path = path_join(config_home, "local/claude");
    bool exists =
        path_exists(paths[i]) &&
        shell_config_has_claude_setup(paths[i], cache_path, local_claude_path);
    artifact_list_push(runtime,
                       path_artifact_as_kind(
                           "shell_config", paths[i], exists, home, "high", true,
                           "shell 配置中的 Claude Code completion / alias 注入"));
    free(cache_path);
    free(local_claude_path);
  }
  free(zshrc);
  free(bashrc);
  free(fishrc);
}

#ifndef _WIN32
#ifndef __APPLE__
static bool mimeapps_line_mentions_claude(const char *line) {
  return strstr(line, "x-scheme-handler/claude-cli") != NULL ||
         strstr(line, "claude-code-url-handler.desktop") != NULL;
}

static bool mimeapps_file_has_claude_handler(const char *path) {
  char *content = NULL;
  if (read_text_file(path, &content) != 0) return false;
  bool found = false;
  char *copy = xstrdup(content);
  char *save = NULL;
  for (char *line = strtok_r(copy, "\n", &save); line != NULL;
       line = strtok_r(NULL, "\n", &save)) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';
    if (mimeapps_line_mentions_claude(line)) {
      found = true;
      break;
    }
  }
  free(copy);
  free(content);
  return found;
}

static int cleanup_mimeapps_file(const char *path) {
  char *content = NULL;
  if (read_text_file(path, &content) != 0) return 1;
  char *copy = xstrdup(content);
  char *out = NULL;
  size_t out_len = 0, out_cap = 0;
  bool first = true;
  char *save = NULL;
  for (char *line = strtok_r(copy, "\n", &save); line != NULL;
       line = strtok_r(NULL, "\n", &save)) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';
    if (!starts_with(line, "x-scheme-handler/claude-cli=")) {
      append_line_buffer(&out, &out_len, &out_cap, line, &first);
      continue;
    }
    const char *rhs = strchr(line, '=');
    if (!rhs) continue;
    rhs++;
    char *work = xstrdup(rhs);
    char *save2 = NULL;
    char *rebuilt = NULL;
    size_t rebuilt_len = 0, rebuilt_cap = 0;
    bool kept_any = false;
    for (char *item = strtok_r(work, ";", &save2); item != NULL;
         item = strtok_r(NULL, ";", &save2)) {
      char *trimmed = trim_copy(item);
      if (*trimmed &&
          strcmp(trimmed, "claude-code-url-handler.desktop") != 0) {
        if (kept_any) append_buffer(&rebuilt, &rebuilt_len, &rebuilt_cap, ";");
        append_buffer(&rebuilt, &rebuilt_len, &rebuilt_cap, trimmed);
        kept_any = true;
      }
      free(trimmed);
    }
    free(work);
    if (kept_any) {
      append_buffer(&rebuilt, &rebuilt_len, &rebuilt_cap, ";");
      char *newline = str_printf("x-scheme-handler/claude-cli=%s", rebuilt);
      append_line_buffer(&out, &out_len, &out_cap, newline, &first);
      free(newline);
    }
    free(rebuilt);
  }
  if (!out) out = xstrdup("");
  int rc = write_text_file(path, out);
  free(out);
  free(copy);
  free(content);
  return rc;
}

static void add_linux_mimeapps_artifacts(ArtifactList *runtime,
                                         const char *home) {
  char *xdg_config = dup_env_or_join("XDG_CONFIG_HOME", home, ".config");
  char *xdg_data = dup_env_or_join("XDG_DATA_HOME", home, DEFAULT_XDG_DATA_SUFFIX);
  char *mime1 = path_join(xdg_config, "mimeapps.list");
  char *apps_dir = path_join(xdg_data, "applications");
  char *mime2 = path_join(apps_dir, "mimeapps.list");
  bool exists1 = path_exists(mime1) && mimeapps_file_has_claude_handler(mime1);
  bool exists2 = path_exists(mime2) && mimeapps_file_has_claude_handler(mime2);
  artifact_list_push(
      runtime,
      path_artifact_as_kind("mimeapps_config", mime1, exists1, home, "high",
                            true, "Linux xdg-mime 关联配置"));
  artifact_list_push(
      runtime,
      path_artifact_as_kind("mimeapps_config", mime2, exists2, home, "high",
                            true, "Linux xdg-mime 关联配置"));
  free(xdg_config);
  free(xdg_data);
  free(mime1);
  free(apps_dir);
  free(mime2);
}
#endif
#endif

static void add_vscode_extension_artifacts(ArtifactList *runtime,
                                           const char *home) {
  const char *ext_prefixes[] = {
      "anthropic.claude-code",
      "anthropic.claude-code-internal",
      NULL,
  };
  const char *roots[] = {
      ".vscode/extensions",
      ".cursor/extensions",
      ".windsurf/extensions",
      NULL,
  };
  for (size_t i = 0; roots[i] != NULL; ++i) {
    char *root = path_join(home, roots[i]);
    add_matching_children_artifacts(runtime, root, home, ext_prefixes,
                                    "VS Code / Cursor / Windsurf 扩展目录");
    free(root);
  }
}

static void add_jetbrains_plugin_artifacts(ArtifactList *runtime,
                                           const char *home) {
  const char *plugin_prefixes[] = {"claude-code-jetbrains-plugin", NULL};
  const char *ide_prefixes[] = {
      "PyCharm", "IntelliJIdea", "IdeaIC", "WebStorm", "PhpStorm",
      "RubyMine", "CLion", "GoLand", "Rider", "DataGrip", "AppCode",
      "DataSpell", "Aqua", "Gateway", "Fleet", "AndroidStudio", NULL,
  };
#ifdef __APPLE__
  const char *bases[] = {
      "Library/Application Support/JetBrains",
      "Library/Application Support",
      "Library/Application Support/Google",
      NULL,
  };
  for (size_t i = 0; bases[i] != NULL; ++i) {
    char *base = path_join(home, bases[i]);
    if (path_exists(base)) {
      DIR *dir = opendir(base);
      if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
          if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
          bool ide_match = false;
          for (size_t j = 0; ide_prefixes[j] != NULL; ++j) {
            if (strncmp(ent->d_name, ide_prefixes[j], strlen(ide_prefixes[j])) == 0) {
              ide_match = true;
              break;
            }
          }
          if (!ide_match) continue;
          char *matched = path_join(base, ent->d_name);
          char *plugins_dir = path_join(matched, "plugins");
          add_matching_children_artifacts(runtime, plugins_dir, home, plugin_prefixes,
                                          "JetBrains Claude Code 插件目录");
          free(matched);
          free(plugins_dir);
        }
        closedir(dir);
      }
    }
    free(base);
  }
#elif defined(_WIN32)
  const char *appdata = get_env_dup("APPDATA");
  const char *localappdata = get_env_dup("LOCALAPPDATA");
  char *base1 = appdata ? xstrdup(appdata) : path_join(home, "AppData\\Roaming");
  char *base2 = localappdata ? xstrdup(localappdata) : path_join(home, "AppData\\Local");
  const char *bases[] = {base1, base2, NULL};
  for (size_t i = 0; bases[i] != NULL; ++i) {
    if (!path_exists(bases[i])) continue;
    char pattern[PATH_MAX_LEN];
    snprintf(pattern, sizeof(pattern), "%s\\*", bases[i]);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
      bool ide_match = false;
      for (size_t j = 0; ide_prefixes[j] != NULL; ++j) {
        if (path_cmp_platform(fd.cFileName, ide_prefixes[j], strlen(ide_prefixes[j])) == 0) {
          ide_match = true;
          break;
        }
      }
      if (!ide_match) continue;
      char *matched = path_join(bases[i], fd.cFileName);
      char *plugins_dir = path_join(matched, "plugins");
      add_matching_children_artifacts(runtime, plugins_dir, home, plugin_prefixes,
                                      "JetBrains Claude Code 插件目录");
      free(matched);
      free(plugins_dir);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  free(base1);
  free(base2);
#else
  const char *bases[] = {
      ".config/JetBrains",
      ".local/share/JetBrains",
      NULL,
  };
  for (size_t i = 0; bases[i] != NULL; ++i) {
    char *base = path_join(home, bases[i]);
    if (path_exists(base)) {
      DIR *dir = opendir(base);
      if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
          if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
          bool ide_match = false;
          for (size_t j = 0; ide_prefixes[j] != NULL; ++j) {
            if (strncmp(ent->d_name, ide_prefixes[j], strlen(ide_prefixes[j])) == 0) {
              ide_match = true;
              break;
            }
          }
          if (!ide_match) continue;
          char *matched = path_join(base, ent->d_name);
          add_matching_children_artifacts(runtime, matched, home, plugin_prefixes,
                                          "JetBrains Claude Code 插件目录");
          free(matched);
        }
        closedir(dir);
      }
    }
    free(base);
  }
  for (size_t j = 0; ide_prefixes[j] != NULL; ++j) {
    char *hidden = str_printf(".%s", ide_prefixes[j]);
    char *matched = path_join(home, hidden);
    add_matching_children_artifacts(runtime, matched, home, plugin_prefixes,
                                    "JetBrains Claude Code 插件目录");
    free(hidden);
    free(matched);
  }
#endif
}

static char *get_npm_global_prefix(const char *home) {
  const char *env_prefix = get_env_dup("NPM_CONFIG_PREFIX");
  if (env_prefix && *env_prefix) return xstrdup(env_prefix);
#ifdef _WIN32
  FILE *fp = _popen("npm config get prefix 2>NUL", "r");
#else
  FILE *fp = popen("npm config get prefix 2>/dev/null", "r");
#endif
  if (fp) {
    char buf[PATH_MAX_LEN];
    if (fgets(buf, sizeof(buf), fp)) {
#ifdef _WIN32
      _pclose(fp);
#else
      pclose(fp);
#endif
      char *trimmed = xstrdup(buf);
      trim_trailing_newlines_simple(trimmed);
      if (*trimmed) return trimmed;
      free(trimmed);
    } else {
#ifdef _WIN32
      _pclose(fp);
#else
      pclose(fp);
#endif
    }
  }
#ifdef _WIN32
  const char *appdata = get_env_dup("APPDATA");
  return appdata ? path_join(appdata, "npm") : NULL;
#else
  (void)home;
  return NULL;
#endif
}

static void add_npm_global_artifacts(ArtifactList *runtime,
                                     const char *home) {
  char *prefix = get_npm_global_prefix(home);
  if (!prefix) return;
#ifdef _WIN32
  char *bin_cmd = path_join(prefix, "claude.cmd");
  char *bin_ps1 = path_join(prefix, "claude.ps1");
  char *bin_exe = path_join(prefix, "claude");
  char *node_modules = path_join(prefix, "node_modules");
  char *scope_dir = path_join(node_modules, "@anthropic-ai");
  char *pkg = path_join(scope_dir, "claude-code");
  artifact_list_push(runtime, path_artifact(bin_cmd, home, "high", true,
                                            "全局 npm 安装的 claude.cmd"));
  artifact_list_push(runtime, path_artifact(bin_ps1, home, "high", true,
                                            "全局 npm 安装的 claude.ps1"));
  artifact_list_push(runtime, path_artifact(bin_exe, home, "high", true,
                                            "全局 npm 安装的 claude 可执行入口"));
  artifact_list_push(runtime, path_artifact(pkg, home, "high", true,
                                            "全局 npm 安装的 @anthropic-ai/claude-code 包目录"));
  free(bin_cmd);
  free(bin_ps1);
  free(bin_exe);
  free(node_modules);
  free(scope_dir);
  free(pkg);
#else
  char *bin_dir = path_join(prefix, "bin");
  char *bin = path_join(bin_dir, "claude");
  char *lib_dir = path_join(prefix, "lib");
  char *node_modules = path_join(lib_dir, "node_modules");
  char *scope_dir = path_join(node_modules, "@anthropic-ai");
  char *pkg = path_join(scope_dir, "claude-code");
  artifact_list_push(runtime, path_artifact(bin, home, "high", true,
                                            "全局 npm 安装的 claude 可执行入口"));
  artifact_list_push(runtime, path_artifact(pkg, home, "high", true,
                                            "全局 npm 安装的 @anthropic-ai/claude-code 包目录"));
  free(bin_dir);
  free(bin);
  free(lib_dir);
  free(node_modules);
  free(scope_dir);
  free(pkg);
#endif
  free(prefix);
}

static bool is_known_shell_config_path(const char *path,
                                       const char *home) {
  char *zshrc = NULL;
  char *bashrc = NULL;
  char *fishrc = NULL;
  get_shell_config_paths(home, &zshrc, &bashrc, &fishrc);
  bool ok = path_equals_platform(zshrc, path) ||
            path_equals_platform(bashrc, path) ||
            path_equals_platform(fishrc, path);
  free(zshrc);
  free(bashrc);
  free(fishrc);
  return ok;
}

#ifndef _WIN32
#ifndef __APPLE__
static bool is_known_mimeapps_path(const char *path,
                                   const char *home) {
  char *xdg_config = dup_env_or_join("XDG_CONFIG_HOME", home, ".config");
  char *xdg_data = dup_env_or_join("XDG_DATA_HOME", home, DEFAULT_XDG_DATA_SUFFIX);
  char *mime1 = path_join(xdg_config, "mimeapps.list");
  char *apps_dir = path_join(xdg_data, "applications");
  char *mime2 = path_join(apps_dir, "mimeapps.list");
  bool ok = path_equals_platform(mime1, path) || path_equals_platform(mime2, path);
  free(xdg_config);
  free(xdg_data);
  free(mime1);
  free(apps_dir);
  free(mime2);
  return ok;
}
#endif
#endif

static bool is_known_ide_extension_path(const char *path,
                                        const char *home) {
  const char *prefixes[] = {
      "anthropic.claude-code",
      "anthropic.claude-code-internal",
      NULL,
  };
  const char *roots[] = {
      ".vscode/extensions",
      ".cursor/extensions",
      ".windsurf/extensions",
      NULL,
  };
  for (size_t i = 0; roots[i] != NULL; ++i) {
    char *root = path_join(home, roots[i]);
    bool match = path_is_same_or_within(root, path) &&
                 component_starts_with_any(path, prefixes);
    free(root);
    if (match) return true;
  }
  return false;
}

static bool is_known_jetbrains_plugin_path(const char *path,
                                           const char *home) {
  const char *prefixes[] = {"claude-code-jetbrains-plugin", NULL};
  return path_is_same_or_within(home, path) &&
         component_starts_with_any(path, prefixes);
}

static bool is_known_npm_global_path(const char *path,
                                     const char *home) {
  char *prefix = get_npm_global_prefix(home);
  if (!prefix) return false;
#ifdef _WIN32
  char *bin_cmd = path_join(prefix, "claude.cmd");
  char *bin_ps1 = path_join(prefix, "claude.ps1");
  char *bin_exe = path_join(prefix, "claude");
  char *node_modules = path_join(prefix, "node_modules");
  char *scope_dir = path_join(node_modules, "@anthropic-ai");
  char *pkg = path_join(scope_dir, "claude-code");
  bool ok = path_equals_platform(bin_cmd, path) ||
            path_equals_platform(bin_ps1, path) ||
            path_equals_platform(bin_exe, path) ||
            path_is_same_or_within(pkg, path);
  free(bin_cmd);
  free(bin_ps1);
  free(bin_exe);
  free(node_modules);
  free(scope_dir);
  free(pkg);
#else
  char *bin_dir = path_join(prefix, "bin");
  char *bin = path_join(bin_dir, "claude");
  char *lib_dir = path_join(prefix, "lib");
  char *node_modules = path_join(lib_dir, "node_modules");
  char *scope_dir = path_join(node_modules, "@anthropic-ai");
  char *pkg = path_join(scope_dir, "claude-code");
  bool ok = path_equals_platform(bin, path) ||
            path_is_same_or_within(pkg, path);
  free(bin_dir);
  free(bin);
  free(lib_dir);
  free(node_modules);
  free(scope_dir);
  free(pkg);
#endif
  free(prefix);
  return ok;
}

/* ------------------------- macOS Keychain ------------------------- */

#ifdef __APPLE__
static int run_process_capture(const char *const argv[],
                               char **stdout_out,
                               bool silence_stderr) {
  int out_pipe[2];
  if (pipe(out_pipe) != 0) return -1;

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
  posix_spawn_file_actions_addclose(&actions, out_pipe[1]);

  int devnull = -1;
  if (silence_stderr) {
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      posix_spawn_file_actions_adddup2(&actions, devnull, STDERR_FILENO);
      posix_spawn_file_actions_addclose(&actions, devnull);
    }
  }

  pid_t pid = 0;
  int spawn_rc = posix_spawnp(&pid, argv[0], &actions, NULL, (char *const *)argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  close(out_pipe[1]);
  if (devnull >= 0) close(devnull);
  if (spawn_rc != 0) {
    close(out_pipe[0]);
    return -1;
  }

  char *buf = NULL;
  size_t cap = 0, len = 0;
  char tmp[4096];
  ssize_t n;
  while ((n = read(out_pipe[0], tmp, sizeof(tmp))) > 0) {
    if (len + (size_t)n + 1 > cap) {
      size_t next = cap == 0 ? 8192 : cap * 2;
      while (next < len + (size_t)n + 1) next *= 2;
      char *p = (char *)realloc(buf, next);
      if (!p) {
        free(buf);
        close(out_pipe[0]);
        return -1;
      }
      buf = p;
      cap = next;
    }
    memcpy(buf + len, tmp, (size_t)n);
    len += (size_t)n;
  }
  close(out_pipe[0]);

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    free(buf);
    return -1;
  }
  if (!buf) {
    buf = xstrdup("");
  } else {
    buf[len] = '\0';
  }
  if (stdout_out) {
    *stdout_out = buf;
  } else {
    free(buf);
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -1;
}

static int run_process_status(const char *const argv[], bool silence_stderr) {
  char *discard = NULL;
  int rc = run_process_capture(argv, &discard, silence_stderr);
  free(discard);
  return rc;
}

static void trim_trailing_newlines(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
    s[--n] = '\0';
  }
}

static bool is_safe_mac_keychain_service_name(const char *service) {
  return starts_with(service, "Claude Code");
}

static int delete_keychain_service(const char *service) {
  if (!is_safe_mac_keychain_service_name(service)) return 1;
  const char *argv[] = {"security", "delete-generic-password", "-s", service, NULL};
  int rc = run_process_status(argv, true);
  return rc == 0 ? 0 : 1;
}

static void add_mac_keychain_artifacts(ArtifactList *runtime,
                                       const char *config_home,
                                       const char *home) {
  char *default_config = path_join(home, ".claude");
  bool is_default_dir = (strcmp(config_home, default_config) == 0 &&
                         get_env_dup("CLAUDE_CONFIG_DIR") == NULL);
  free(default_config);

  const char *oauth_suffixes[] = {"", "-staging-oauth", "-local-oauth",
                                  "-custom-oauth"};
  char *dir_hash = NULL;
  if (!is_default_dir) {
    char *digest = sha256_hex8(config_home);
    dir_hash = str_printf("-%s", digest);
    free(digest);
  } else {
    dir_hash = xstrdup("");
  }

  for (size_t i = 0; i < sizeof(oauth_suffixes) / sizeof(oauth_suffixes[0]); ++i) {
    char *s1 = str_printf("Claude Code%s%s", oauth_suffixes[i], dir_hash);
    char *s2 = str_printf("Claude Code%s-credentials%s", oauth_suffixes[i], dir_hash);
    const char *argv1[] = {"security", "find-generic-password", "-s", s1, NULL};
    int rc1 = run_process_status(argv1, true);
    artifact_list_push(runtime, make_artifact("keychain", s1, rc1 == 0, "medium",
                                              true, "macOS Keychain 服务项"));
    const char *argv2[] = {"security", "find-generic-password", "-s", s2, NULL};
    int rc2 = run_process_status(argv2, true);
    artifact_list_push(runtime, make_artifact("keychain", s2, rc2 == 0, "medium",
                                              true, "macOS Keychain 服务项"));
    free(s1);
    free(s2);
  }
  free(dir_hash);
}
#else
static void add_mac_keychain_artifacts(ArtifactList *runtime,
                                       const char *config_home,
                                       const char *home) {
  (void)runtime;
  (void)config_home;
  (void)home;
}

static int delete_keychain_service(const char *service) {
  (void)service;
  return 0;
}
#endif

#ifdef _WIN32
static bool windows_registry_key_exists(HKEY root, const char *subkey) {
  HKEY hkey = NULL;
  LONG status = RegOpenKeyExA(root, subkey, 0, KEY_READ, &hkey);
  if (status == ERROR_SUCCESS && hkey != NULL) {
    RegCloseKey(hkey);
    return true;
  }
  return false;
}

static bool windows_registry_value_exists(HKEY root,
                                          const char *subkey,
                                          const char *value_name,
                                          char **value_out) {
  HKEY hkey = NULL;
  LONG status = RegOpenKeyExA(root, subkey, 0, KEY_READ, &hkey);
  if (status != ERROR_SUCCESS || hkey == NULL) {
    return false;
  }

  DWORD type = 0;
  DWORD size = 0;
  status = RegQueryValueExA(hkey, value_name, NULL, &type, NULL, &size);
  if (status != ERROR_SUCCESS) {
    RegCloseKey(hkey);
    return false;
  }

  if (value_out != NULL &&
      (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ) &&
      size > 0) {
    char *buf = (char *)calloc((size_t)size + 2, 1);
    if (!buf) {
      RegCloseKey(hkey);
      fprintf(stderr, "内存分配失败\n");
      exit(2);
    }
    status = RegQueryValueExA(hkey, value_name, NULL, &type, (LPBYTE)buf, &size);
    if (status == ERROR_SUCCESS) {
      *value_out = buf;
    } else {
      free(buf);
    }
  }

  RegCloseKey(hkey);
  return true;
}

static char *truncate_detail(const char *value, size_t max_len) {
  if (!value) {
    return xstrdup("");
  }
  size_t n = strlen(value);
  if (n <= max_len) {
    return xstrdup(value);
  }
  return str_printf("%.*s...(truncated,%zu bytes)", (int)max_len, value, n);
}

static void add_windows_registry_artifacts(ArtifactList *runtime) {
  char *k1 = str_printf("HKCU\Software\Classes\%s", WINDOWS_DEEP_LINK_PROTOCOL);
  char *k1_default =
      str_printf("HKCU\Software\Classes\%s [value:(Default)]",
                 WINDOWS_DEEP_LINK_PROTOCOL);
  char *k1_url_protocol =
      str_printf("HKCU\Software\Classes\%s [value:URL Protocol]",
                 WINDOWS_DEEP_LINK_PROTOCOL);
  char *k_open =
      str_printf("HKCU\Software\Classes\%s\shell\open",
                 WINDOWS_DEEP_LINK_PROTOCOL);
  char *k2 =
      str_printf("HKCU\Software\Classes\%s\shell\open\command",
                 WINDOWS_DEEP_LINK_PROTOCOL);
  char *k2_default =
      str_printf("HKCU\Software\Classes\%s\shell\open\command [value:(Default)]",
                 WINDOWS_DEEP_LINK_PROTOCOL);

  char *sub1 = str_printf("Software\Classes\%s", WINDOWS_DEEP_LINK_PROTOCOL);
  char *sub_open =
      str_printf("Software\Classes\%s\shell\open",
                 WINDOWS_DEEP_LINK_PROTOCOL);
  char *sub2 =
      str_printf("Software\Classes\%s\shell\open\command",
                 WINDOWS_DEEP_LINK_PROTOCOL);
  char *v_root_default = NULL;
  char *v_root_url_protocol = NULL;
  char *v_cmd_default = NULL;

  artifact_list_push(runtime,
                     make_artifact("registry", k1,
                                   windows_registry_key_exists(HKEY_CURRENT_USER, sub1),
                                   "high", true,
                                   "Windows deep-link 协议注册表项"));
  bool has_root_default =
      windows_registry_value_exists(HKEY_CURRENT_USER, sub1, NULL, &v_root_default);
  char *root_default_detail = truncate_detail(v_root_default, 200);
  artifact_list_push(runtime,
                     make_artifact_with_detail("registry_value", k1_default,
                                               has_root_default, "high", true,
                                               "Windows deep-link 根键默认值",
                                               root_default_detail));
  bool has_url_protocol =
      windows_registry_value_exists(HKEY_CURRENT_USER, sub1, "URL Protocol",
                                    &v_root_url_protocol);
  char *url_protocol_detail = truncate_detail(v_root_url_protocol, 200);
  artifact_list_push(runtime,
                     make_artifact_with_detail("registry_value", k1_url_protocol,
                                               has_url_protocol, "high", true,
                                               "Windows URL Protocol 标记值",
                                               url_protocol_detail));
  artifact_list_push(runtime,
                     make_artifact("registry", k_open,
                                   windows_registry_key_exists(HKEY_CURRENT_USER,
                                                               sub_open),
                                   "medium", true,
                                   "Windows deep-link open 子键"));
  artifact_list_push(runtime,
                     make_artifact("registry", k2,
                                   windows_registry_key_exists(HKEY_CURRENT_USER, sub2),
                                   "high", true,
                                   "Windows deep-link 命令注册表项"));
  bool has_cmd_default =
      windows_registry_value_exists(HKEY_CURRENT_USER, sub2, NULL, &v_cmd_default);
  char *cmd_default_detail = truncate_detail(v_cmd_default, 400);
  artifact_list_push(runtime,
                     make_artifact_with_detail("registry_value", k2_default,
                                               has_cmd_default, "high", true,
                                               "Windows deep-link 命令默认值",
                                               cmd_default_detail));

  for (size_t i = 0; WINDOWS_NATIVE_HOST_REGISTRY_BASES[i] != NULL; ++i) {
    char *subkey = str_printf("%s\\%s", WINDOWS_NATIVE_HOST_REGISTRY_BASES[i],
                              CHROME_NATIVE_HOST_IDENTIFIER);
    char *identifier = str_printf("HKCU\\%s", subkey);
    char *identifier_default = str_printf("HKCU\\%s [value:(Default)]", subkey);
    char *value = NULL;
    bool key_exists = windows_registry_key_exists(HKEY_CURRENT_USER, subkey);
    bool has_default = windows_registry_value_exists(HKEY_CURRENT_USER, subkey,
                                                     NULL, &value);
    char *detail = truncate_detail(value, 400);
    artifact_list_push(runtime,
                       make_artifact("registry", identifier, key_exists,
                                     "high", true,
                                     "Windows 浏览器 Native Host 注册表项"));
    artifact_list_push(runtime,
                       make_artifact_with_detail("registry_value",
                                                 identifier_default,
                                                 has_default, "high", true,
                                                 "Windows 浏览器 Native Host manifest 路径",
                                                 detail));
    free(subkey);
    free(identifier);
    free(identifier_default);
    free(value);
    free(detail);
  }

  free(k1);
  free(k1_default);
  free(k1_url_protocol);
  free(k_open);
  free(k2);
  free(k2_default);
  free(sub1);
  free(sub_open);
  free(sub2);
  free(v_root_default);
  free(v_root_url_protocol);
  free(v_cmd_default);
  free(root_default_detail);
  free(url_protocol_detail);
  free(cmd_default_detail);
}

static bool contains_nocase(const char *haystack, const char *needle) {
  if (!haystack || !needle || !*needle) {
    return false;
  }
  size_t nlen = strlen(needle);
  for (const char *p = haystack; *p; ++p) {
    size_t i = 0;
    while (p[i] && i < nlen &&
           tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
      i++;
    }
    if (i == nlen) {
      return true;
    }
  }
  return false;
}

static bool is_windows_related_credential(const char *target_name) {
  return contains_nocase(target_name, "Claude Code") ||
         contains_nocase(target_name, "claude-cli") ||
         contains_nocase(target_name, "claude-cli-nodejs");
}

static bool is_safe_windows_registry_subkey(const char *subkey) {
  if (starts_with(subkey, "Software\\Classes\\claude-cli")) {
    return true;
  }
  for (size_t i = 0; WINDOWS_NATIVE_HOST_REGISTRY_BASES[i] != NULL; ++i) {
    char *full = str_printf("%s\\%s", WINDOWS_NATIVE_HOST_REGISTRY_BASES[i],
                            CHROME_NATIVE_HOST_IDENTIFIER);
    bool ok = path_equals_platform(subkey, full);
    free(full);
    if (ok) {
      return true;
    }
  }
  return false;
}

static bool windows_registry_read_value_raw(HKEY root,
                                            const char *subkey,
                                            const char *value_name,
                                            DWORD *type_out,
                                            BYTE **data_out,
                                            DWORD *size_out) {
  HKEY hkey = NULL;
  LONG status = RegOpenKeyExA(root, subkey, 0, KEY_READ, &hkey);
  if (status != ERROR_SUCCESS || hkey == NULL) {
    return false;
  }

  DWORD type = 0;
  DWORD size = 0;
  status = RegQueryValueExA(hkey, value_name, NULL, &type, NULL, &size);
  if (status != ERROR_SUCCESS) {
    RegCloseKey(hkey);
    return false;
  }

  BYTE *buf = NULL;
  if (size > 0) {
    buf = (BYTE *)malloc((size_t)size);
    if (!buf) {
      RegCloseKey(hkey);
      fprintf(stderr, "内存分配失败\n");
      exit(2);
    }
    status = RegQueryValueExA(hkey, value_name, NULL, &type, buf, &size);
    if (status != ERROR_SUCCESS) {
      free(buf);
      RegCloseKey(hkey);
      return false;
    }
  }

  RegCloseKey(hkey);
  if (type_out) *type_out = type;
  if (data_out) *data_out = buf;
  else free(buf);
  if (size_out) *size_out = size;
  return true;
}

static void add_windows_credential_artifacts(ArtifactList *runtime) {
  DWORD count = 0;
  PCREDENTIALA *creds = NULL;
  if (!CredEnumerateA(NULL, 0, &count, &creds)) {
    return;
  }

  for (DWORD i = 0; i < count; ++i) {
    PCREDENTIALA cred = creds[i];
    if (!cred || cred->Type != CRED_TYPE_GENERIC || !cred->TargetName) {
      continue;
    }
    if (!is_windows_related_credential(cred->TargetName)) {
      continue;
    }
    artifact_list_push(runtime,
                       make_artifact("credential", cred->TargetName, true,
                                     "medium", true,
                                     "Windows Credential Manager 通用凭据"));
  }

  CredFree(creds);
}
#endif

/* ------------------------- 收集 artifacts ------------------------- */

void collect_artifacts(const char *home,
                              const char *config_home,
                              ArtifactList *runtime,
                              ArtifactList *related) {
  char *env_data = NULL;
  char *env_config = NULL;
  char *env_cache = NULL;
  char *env_log = NULL;
  get_env_paths(home, &env_data, &env_config, &env_cache, &env_log);

  char *global_cfg = path_join(home, ".claude.json");
  char *oauth_cfg = path_join(home, ".claude.oauth.json");
  artifact_list_push(runtime, path_artifact(global_cfg, home, "high", true,
                                            "全局配置文件（历史/兼容路径）"));
  artifact_list_push(runtime, path_artifact(oauth_cfg, home, "high", true,
                                            "OAuth 全局配置文件"));
  free(global_cfg);
  free(oauth_cfg);

  const RelativePathSpec runtime_specs[] = {
      {".credentials.json", "明文凭据 fallback 文件"},
      {".deep-link-register-failed", "deep-link 注册失败标记"},
      {"remote", "远程/CCR 相关本地状态"},
      {"projects", "会话 transcript / session 历史"},
      {"history.jsonl", "命令历史与交互历史"},
      {"uploads", "桥接/附件上传缓存"},
      {"server-sessions.json", "本地 server session 状态"},
      {"sessions", "并发 session 目录"},
      {"debug", "debug 日志"},
      {"telemetry", "1P telemetry 失败批次/缓存"},
      {"plugins", "插件安装/缓存目录"},
      {"cowork_plugins", "cowork 插件目录"},
      {"mcp-needs-auth-cache.json", "MCP 认证需求缓存"},
      {"usage-data", "usage insights 数据"},
      {"startup-perf", "启动性能采样目录"},
      {"backups", "内置备份目录"},
      {"plans", "plans 目录"},
      {"cache", "运行缓存目录"},
      {"traces", "性能 tracing 目录"},
      {"chrome", "Claude in Chrome 运行目录"},
      {"ide", "IDE 集成状态目录"},
      {"shell-snapshots", "shell 快照目录"},
      {"jobs", "后台 jobs 目录"},
      {"tasks", "任务目录"},
      {"teams", "团队/协作目录"},
      {"local", "本地安装目录"},
      {"completion.zsh", "zsh completion 缓存"},
      {"completion.bash", "bash completion 缓存"},
      {"completion.fish", "fish completion 缓存"},
  };
  add_relative_artifacts(runtime, config_home, home, "high", true,
                         runtime_specs,
                         sizeof(runtime_specs) / sizeof(runtime_specs[0]));

  artifact_list_push(runtime, path_artifact(env_data, home, "high", true,
                                            "env-paths 数据目录"));
  artifact_list_push(runtime, path_artifact(env_config, home, "high", true,
                                            "env-paths 配置目录"));
  artifact_list_push(runtime, path_artifact(env_cache, home, "high", true,
                                            "env-paths 缓存目录"));
  artifact_list_push(runtime, path_artifact(env_log, home, "high", true,
                                            "env-paths 日志目录"));

  add_native_installer_artifacts(runtime, home);
  add_browser_native_host_artifacts(runtime, config_home, home);
  add_shell_config_artifacts(runtime, config_home, home);
  add_vscode_extension_artifacts(runtime, home);
  add_jetbrains_plugin_artifacts(runtime, home);
  add_npm_global_artifacts(runtime, home);

#ifndef _WIN32
#ifndef __APPLE__
  char *desktop = get_linux_deep_link_desktop_path(home);
  artifact_list_push(runtime, path_artifact(desktop, home, "high", true,
                                            "Linux deep-link desktop entry"));
  free(desktop);
  add_linux_mimeapps_artifacts(runtime, home);
#endif
#endif

#ifdef __APPLE__
  char *deep_link_app = path_join(home, "Applications/Claude Code URL Handler.app");
  artifact_list_push(runtime, path_artifact(deep_link_app, home, "high", true,
                                            "macOS deep-link 协议处理器"));
  free(deep_link_app);
#endif

#ifdef _WIN32
  add_windows_registry_artifacts(runtime);
  add_windows_credential_artifacts(runtime);
#endif

  add_mac_keychain_artifacts(runtime, config_home, home);

  artifact_list_push(
      related,
      path_artifact(config_home, home, "low", false,
                    "Claude 相关配置根目录，可能包含用户自定义内容"));

  const RelativePathSpec related_specs[] = {
      {"settings.json", "用户设置，默认不建议自动删除"},
      {"agents", "用户 agents 定义，默认不建议自动删除"},
      {"keybindings.json", "用户快捷键配置，默认不建议自动删除"},
      {"CLAUDE.md", "用户级 CLAUDE.md，默认不建议自动删除"},
      {"rules", "用户 rules 目录，默认不建议自动删除"},
      {"skills", "用户 skills 目录，默认不建议自动删除"},
      {"commands", "用户 commands 目录，默认不建议自动删除"},
      {"output-styles", "用户输出样式目录，默认不建议自动删除"},
      {"magic-docs", "用户 Magic Docs 数据，默认不建议自动删除"},
      {"session-memory", "用户 session memory，默认不建议自动删除"},
      {"agent-memory", "用户 agent memory，默认不建议自动删除"},
  };
  add_relative_artifacts(related, config_home, home, "low", false,
                         related_specs,
                         sizeof(related_specs) / sizeof(related_specs[0]));

  if (path_is_dir(config_home)) {
#ifdef _WIN32
    char pattern[PATH_MAX_LEN];
    snprintf(pattern, sizeof(pattern), "%s\\agents.backup.*", config_home);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        char *full = path_join(config_home, fd.cFileName);
        artifact_list_push(related, path_artifact(full, home, "low", false,
                                                  "agents 备份目录，默认不建议自动删除"));
        free(full);
      } while (FindNextFileA(h, &fd));
      FindClose(h);
    }
#else
    DIR *dir = opendir(config_home);
    if (dir) {
      struct dirent *ent;
      while ((ent = readdir(dir)) != NULL) {
        if (starts_with(ent->d_name, "agents.backup.")) {
          char *full = path_join(config_home, ent->d_name);
          artifact_list_push(related, path_artifact(full, home, "low", false,
                                                    "agents 备份目录，默认不建议自动删除"));
          free(full);
        }
      }
      closedir(dir);
    }
#endif
  }

  free(env_data);
  free(env_config);
  free(env_cache);
  free(env_log);
}

static bool is_default_config_home_path(const char *path, const char *home) {
  char *def = path_join(home, ".claude");
  bool ok = path_equals_platform(path, def);
  free(def);
  return ok;
}

static bool is_root_like_path(const char *path, const char *home) {
  if (!path || !*path) return true;
#ifdef _WIN32
  size_t len = strlen(path);
  if (len == 2 && isalpha((unsigned char)path[0]) && path[1] == ':') return true;
  if (len == 3 && isalpha((unsigned char)path[0]) && path[1] == ':' &&
      is_sep_char(path[2])) return true;
  if (path_equals_platform(path, home)) return true;
#else
  if (strcmp(path, "/") == 0) return true;
  if (path_equals_platform(path, home)) return true;
#endif
  return false;
}

static bool is_known_system_path(const char *path, const char *home) {
  (void)home;
#ifdef _WIN32
  char *winds = expand_home("C:\\Windows", home);
  char *pf = expand_home("C:\\Program Files", home);
  char *pfx86 = expand_home("C:\\Program Files (x86)", home);
  char *users = expand_home("C:\\Users", home);
  bool bad = path_is_same_or_within(winds, path) || path_is_same_or_within(pf, path) ||
             path_is_same_or_within(pfx86, path) || path_is_same_or_within(users, path);
  free(winds); free(pf); free(pfx86); free(users);
  return bad;
#else
  const char *roots[] = {"/System", "/Library", "/usr", "/bin", "/etc", "/var",
                         "/sbin", "/opt", NULL};
  for (size_t i = 0; roots[i] != NULL; ++i) {
    if (path_is_same_or_within(roots[i], path)) return true;
  }
  return false;
#endif
}

static bool is_known_claude_restore_path(const char *path, const char *home) {
  char *def = path_join(home, ".claude");
  char *cfg1 = path_join(home, ".claude.json");
  char *cfg2 = path_join(home, ".claude.oauth.json");
  char *data = NULL, *config = NULL, *cache = NULL, *log = NULL;
  get_env_paths(home, &data, &config, &cache, &log);
  bool ok = path_is_same_or_within(def, path) ||
            path_equals_platform(cfg1, path) ||
            path_equals_platform(cfg2, path) ||
            path_is_same_or_within(data, path) ||
            path_is_same_or_within(config, path) ||
            path_is_same_or_within(cache, path) ||
            path_is_same_or_within(log, path) ||
            is_known_native_installer_path(path, home) ||
            is_known_browser_integration_path(path, def, home) ||
            is_known_shell_config_path(path, home) ||
            is_known_ide_extension_path(path, home) ||
            is_known_jetbrains_plugin_path(path, home) ||
            is_known_npm_global_path(path, home)
#ifndef _WIN32
#ifndef __APPLE__
            || is_known_mimeapps_path(path, home)
#endif
#endif
            ;
  free(def);
  free(cfg1);
  free(cfg2);
  free(data);
  free(config);
  free(cache);
  free(log);
  return ok;
}

bool is_safe_purge_target_path(const char *path, const char *home) {
  if (is_root_like_path(path, home)) return false;
  if (is_known_system_path(path, home)) return false;
  return is_default_config_home_path(path, home);
}

static bool is_safe_restore_path(const char *path, const char *home) {
  if (is_root_like_path(path, home)) return false;
  if (is_known_system_path(path, home)) return false;
  return is_known_claude_restore_path(path, home);
}

static int backup_filesystem_artifact(BackupContext *ctx,
                                      const Artifact *a,
                                      const char *home) {
  char *abs_path = starts_with(a->identifier, "~") ? expand_home(a->identifier, home)
                                                    : xstrdup(a->identifier);
  char *rel = absolute_to_backup_rel(abs_path);
  char *dst = path_join(ctx->backup_dir, rel);
  int rc = copy_tree_recursive(abs_path, dst);
  if (rc == 0) {
    char *e_abs = field_encode(abs_path);
    char *e_rel = field_encode(rel);
    char *e_kind = field_encode(a->kind);
    fprintf(ctx->manifest_fp, "FS\t%s\t%s\t%s\n", e_abs, e_rel, e_kind);
    fprintf(ctx->index_fp, "- filesystem `%s` -> `%s`\n", abs_path, rel);
    free(e_abs);
    free(e_rel);
    free(e_kind);
  }
  free(abs_path);
  free(rel);
  free(dst);
  return rc;
}

#ifdef __APPLE__
static int backup_keychain_artifact(BackupContext *ctx, const Artifact *a) {
  if (!is_safe_mac_keychain_service_name(a->identifier)) return 1;
  const char *argv[] = {"security", "find-generic-password", "-s", a->identifier, "-w", NULL};
  char *buf = NULL;
  int rc = run_process_capture(argv, &buf, true);
  if (rc != 0) return 1;
  if (!buf) return 1;
  trim_trailing_newlines(buf);
  char *e_service = field_encode(a->identifier);
  char *e_secret = field_encode(buf);
  fprintf(ctx->manifest_fp, "MACKEY\t%s\t%s\n", e_service, e_secret);
  fprintf(ctx->index_fp, "- keychain `%s`\n", a->identifier);
  free(e_service);
  free(e_secret);
  free(buf);
  return 0;
}
#endif

#ifdef _WIN32
static bool parse_registry_identifier(const char *identifier,
                                      char **subkey_out,
                                      char **value_name_out) {
  const char *prefix = "HKCU\\";
  if (!starts_with(identifier, prefix)) return false;
  const char *sub = identifier + strlen(prefix);
  const char *marker = strstr(sub, " [value:");
  if (!marker) {
    *subkey_out = xstrdup(sub);
    *value_name_out = NULL;
    return true;
  }
  size_t key_len = (size_t)(marker - sub);
  char *subkey = (char *)malloc(key_len + 1);
  if (!subkey) exit(2);
  memcpy(subkey, sub, key_len);
  subkey[key_len] = '\0';
  const char *vstart = marker + strlen(" [value:");
  const char *vend = strrchr(vstart, ']');
  if (!vend) {
    free(subkey);
    return false;
  }
  size_t vlen = (size_t)(vend - vstart);
  char *vname = (char *)malloc(vlen + 1);
  if (!vname) exit(2);
  memcpy(vname, vstart, vlen);
  vname[vlen] = '\0';
  *subkey_out = subkey;
  *value_name_out = vname;
  return true;
}

static int backup_registry_artifact(BackupContext *ctx, const Artifact *a) {
  char *subkey = NULL;
  char *value_name = NULL;
  if (!parse_registry_identifier(a->identifier, &subkey, &value_name)) {
    return 1;
  }
  if (!is_safe_windows_registry_subkey(subkey)) {
    free(subkey);
    free(value_name);
    return 1;
  }
  char *e_subkey = field_encode(subkey);
  if (strcmp(a->kind, "registry_value") == 0 && value_name != NULL) {
    char *e_value = field_encode(value_name);
    DWORD type = 0;
    DWORD size = 0;
    BYTE *data = NULL;
    if (!windows_registry_read_value_raw(HKEY_CURRENT_USER, subkey,
                                         strcmp(value_name, "(Default)") == 0 ? NULL : value_name,
                                         &type, &data, &size)) {
      free(e_subkey);
      free(e_value);
      free(subkey);
      free(value_name);
      return 1;
    }
    char *type_s = str_printf("%lu", (unsigned long)type);
    char *data_hex = bytes_to_hex((const unsigned char *)data, (size_t)size);
    fprintf(ctx->manifest_fp, "REGVAL\t%s\t%s\t%s\t%s\n", e_subkey, e_value, type_s, data_hex);
    fprintf(ctx->index_fp, "- registry value `%s`", a->identifier);
    if (a->detail && *a->detail) fprintf(ctx->index_fp, " = `%s`", a->detail);
    fprintf(ctx->index_fp, "\n");
    free(type_s);
    free(data_hex);
    free(data);
    free(e_value);
  } else {
    fprintf(ctx->manifest_fp, "REGKEY\t%s\n", e_subkey);
    fprintf(ctx->index_fp, "- registry key `%s`\n", a->identifier);
  }
  free(e_subkey);
  free(subkey);
  free(value_name);
  return 0;
}

static int backup_windows_credential_artifact(BackupContext *ctx, const Artifact *a) {
  PCREDENTIALA cred = NULL;
  if (!CredReadA(a->identifier, CRED_TYPE_GENERIC, 0, &cred) || !cred) {
    return 1;
  }
  char *blob_hex = bytes_to_hex((const unsigned char *)cred->CredentialBlob,
                                (size_t)cred->CredentialBlobSize);
  char *e_target = field_encode(cred->TargetName ? cred->TargetName : "");
  char *e_user = field_encode(cred->UserName ? cred->UserName : "");
  char *e_comment = field_encode(cred->Comment ? cred->Comment : "");
  fprintf(ctx->manifest_fp, "WINCRED\t%s\t%u\t%u\t%s\t%s\t%s\n",
          e_target, (unsigned)cred->Type, (unsigned)cred->Persist, e_user,
          e_comment, blob_hex);
  fprintf(ctx->index_fp, "- credential `%s`\n", a->identifier);
  free(blob_hex);
  free(e_target);
  free(e_user);
  free(e_comment);
  CredFree(cred);
  return 0;
}
#endif

static int backup_artifact(BackupContext *ctx, const Artifact *a, const char *home) {
  if (!ctx || !ctx->enabled) return 0;
  if (strcmp(a->kind, "keychain") == 0) {
#ifdef __APPLE__
    return backup_keychain_artifact(ctx, a);
#else
    return 1;
#endif
  }
  if (strcmp(a->kind, "registry") == 0 || strcmp(a->kind, "registry_value") == 0) {
#ifdef _WIN32
    return backup_registry_artifact(ctx, a);
#else
    return 1;
#endif
  }
  if (strcmp(a->kind, "credential") == 0) {
#ifdef _WIN32
    return backup_windows_credential_artifact(ctx, a);
#else
    return 1;
#endif
  }
  return backup_filesystem_artifact(ctx, a, home);
}

/* ------------------------- 清理计划与执行 ------------------------- */

static bool artifact_same(const Artifact *a, const Artifact *b) {
  return strcmp(a->kind, b->kind) == 0 &&
         strcmp(a->identifier, b->identifier) == 0;
}

static bool artifact_list_contains(const ArtifactList *list, const Artifact *a) {
  for (size_t i = 0; i < list->len; ++i) {
    if (artifact_same(&list->items[i], a)) {
      return true;
    }
  }
  return false;
}

void build_cleanup_targets(const ArtifactList *runtime,
                                  const ArtifactList *related,
                                  bool include_related,
                                  bool purge_config_home,
                                  const char *config_home,
                                  const char *home,
                                  ArtifactList *targets) {
  artifact_list_init(targets);
  char *config_home_tilde = tilde_path(config_home, home);

  for (size_t i = 0; i < runtime->len; ++i) {
    const Artifact *a = &runtime->items[i];
    if (a->exists && a->safe_clean && !artifact_list_contains(targets, a)) {
      artifact_list_push(targets, make_artifact(a->kind, a->identifier, a->exists,
                                                a->confidence, a->safe_clean, a->note));
    }
  }

  if (include_related) {
    for (size_t i = 0; i < related->len; ++i) {
      const Artifact *a = &related->items[i];
      if (a->exists && strcmp(a->identifier, config_home_tilde) != 0 &&
          !artifact_list_contains(targets, a)) {
        char *note = str_printf("%s（用户显式要求 include-related）", a->note);
        artifact_list_push(targets, make_artifact(a->kind, a->identifier, a->exists,
                                                  a->confidence, true, note));
        free(note);
      }
    }
  }

  if (purge_config_home && path_exists(config_home)) {
    ArtifactList filtered;
    artifact_list_init(&filtered);
    char *config_home_abs = canonicalish_path(config_home, home);
    for (size_t i = 0; i < targets->len; ++i) {
      Artifact *a = &targets->items[i];
      bool inside = false;
      if (strcmp(a->kind, "keychain") != 0 &&
          strcmp(a->kind, "registry") != 0 &&
          strcmp(a->kind, "registry_value") != 0 &&
          strcmp(a->kind, "credential") != 0) {
        char *artifact_abs = starts_with(a->identifier, "~")
                                 ? canonicalish_path(a->identifier, home)
                                 : canonicalish_path(a->identifier, home);
        if (path_is_same_or_within(config_home_abs, artifact_abs)) {
          inside = true;
        }
        free(artifact_abs);
      }
      if (!inside) {
        artifact_list_push(&filtered, make_artifact(a->kind, a->identifier,
                                                    a->exists, a->confidence,
                                                    a->safe_clean, a->note));
      }
    }
    free(config_home_abs);
    artifact_list_free(targets);
    *targets = filtered;
    artifact_list_push(targets,
                       make_artifact("dir", config_home_tilde, true, "low", true,
                                     "整个配置根目录（高风险操作）"));
  }
  free(config_home_tilde);
}

void print_cleanup_plan(const ArtifactList *targets, bool dry_run) {
  printf("== 清理计划（%s）==\n", dry_run ? "将删除" : "待删除");
  if (targets->len == 0) {
    printf("没有需要清理的目标。\n");
    return;
  }
  for (size_t i = 0; i < targets->len; ++i) {
    const Artifact *a = &targets->items[i];
    printf("- [%s] %s\n", a->kind, a->identifier);
    printf("  说明: %s\n", a->note);
    if (a->detail && *a->detail) {
      printf("  内容: %s\n", a->detail);
    }
  }
}

bool confirm_action(const char *prompt) {
  char buf[32];
  printf("%s [y/N]: ", prompt);
  fflush(stdout);
  if (!fgets(buf, sizeof(buf), stdin)) {
    return false;
  }
  for (char *p = buf; *p; ++p) {
    *p = (char)tolower((unsigned char)*p);
  }
  return starts_with(buf, "y");
}

void cleanup_results_init(CleanupResults *r) {
  r->items = NULL;
  r->len = 0;
  r->cap = 0;
}

static void cleanup_results_push(CleanupResults *r,
                                 const char *kind,
                                 const char *identifier,
                                 const char *status,
                                 const char *message) {
  if (r->len == r->cap) {
    size_t next = r->cap == 0 ? 16 : r->cap * 2;
    CleanupResult *p =
        (CleanupResult *)realloc(r->items, next * sizeof(CleanupResult));
    if (!p) {
      fprintf(stderr, "内存分配失败\n");
      exit(2);
    }
    r->items = p;
    r->cap = next;
  }
  r->items[r->len].kind = xstrdup(kind);
  r->items[r->len].identifier = xstrdup(identifier);
  r->items[r->len].status = xstrdup(status);
  r->items[r->len].message = xstrdup(message ? message : "");
  r->len++;
}

void cleanup_results_free(CleanupResults *r) {
  for (size_t i = 0; i < r->len; ++i) {
    free(r->items[i].kind);
    free(r->items[i].identifier);
    free(r->items[i].status);
    free(r->items[i].message);
  }
  free(r->items);
}

static int delete_registry_artifact(const char *identifier) {
#ifdef _WIN32
  const char *prefix = "HKCU\\";
  if (!starts_with(identifier, prefix)) {
    return 1;
  }
  const char *subkey = identifier + strlen(prefix);

  const char *value_marker = strstr(subkey, " [value:");
  if (value_marker != NULL) {
    size_t key_len = (size_t)(value_marker - subkey);
    char *key_path = (char *)malloc(key_len + 1);
    if (!key_path) {
      fprintf(stderr, "内存分配失败\n");
      exit(2);
    }
    memcpy(key_path, subkey, key_len);
    key_path[key_len] = '\0';

    const char *value_start = value_marker + strlen(" [value:");
    const char *value_end = strrchr(value_start, ']');
    if (!value_end || value_end <= value_start) {
      free(key_path);
      return 1;
    }

    size_t value_len = (size_t)(value_end - value_start);
    char *value_name = (char *)malloc(value_len + 1);
    if (!value_name) {
      free(key_path);
      fprintf(stderr, "内存分配失败\n");
      exit(2);
    }
    memcpy(value_name, value_start, value_len);
    value_name[value_len] = '\0';

    HKEY hkey = NULL;
    LONG open_status = RegOpenKeyExA(HKEY_CURRENT_USER, key_path, 0, KEY_SET_VALUE, &hkey);
    if (open_status == ERROR_FILE_NOT_FOUND) {
      free(key_path);
      free(value_name);
      return 0;
    }
    if (open_status != ERROR_SUCCESS || hkey == NULL) {
      free(key_path);
      free(value_name);
      return 1;
    }

    const char *real_value_name =
        (strcmp(value_name, "(Default)") == 0) ? NULL : value_name;
    LONG delete_status = RegDeleteValueA(hkey, real_value_name);
    RegCloseKey(hkey);
    free(key_path);
    free(value_name);
    if (delete_status == ERROR_SUCCESS || delete_status == ERROR_FILE_NOT_FOUND) {
      return 0;
    }
    return 1;
  }

  LONG status = RegDeleteTreeA(HKEY_CURRENT_USER, subkey);
  if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) {
    return 0;
  }
  return 1;
#else
  (void)identifier;
  return 0;
#endif
}

static int delete_windows_credential(const char *target) {
#ifdef _WIN32
  if (CredDeleteA(target, CRED_TYPE_GENERIC, 0)) {
    return 0;
  }
  DWORD err = GetLastError();
  return (err == ERROR_NOT_FOUND) ? 0 : 1;
#else
  (void)target;
  return 0;
#endif
}

void run_cleanup(const ArtifactList *targets,
                        bool dry_run,
                        CleanupResults *results,
                        const char *home,
                        const char *config_home,
                        BackupContext *backup_ctx) {
  cleanup_results_init(results);
  for (size_t i = 0; i < targets->len; ++i) {
    const Artifact *a = &targets->items[i];
    if (dry_run) {
      cleanup_results_push(results, a->kind, a->identifier, "skipped", a->note);
      continue;
    }

    if (backup_ctx && backup_ctx->enabled) {
      if (backup_artifact(backup_ctx, a, home) != 0) {
        cleanup_results_push(results, a->kind, a->identifier, "failed",
                             "备份失败，已跳过删除");
        continue;
      }
    }

    int rc = 0;
    if (strcmp(a->kind, "keychain") == 0) {
      rc = delete_keychain_service(a->identifier);
    } else if (strcmp(a->kind, "registry") == 0 ||
               strcmp(a->kind, "registry_value") == 0) {
      rc = delete_registry_artifact(a->identifier);
    } else if (strcmp(a->kind, "credential") == 0) {
      rc = delete_windows_credential(a->identifier);
    } else {
      char *abs_or_rel = NULL;
      if (starts_with(a->identifier, "~")) {
        abs_or_rel = expand_home(a->identifier, home);
      } else {
        abs_or_rel = xstrdup(a->identifier);
      }
      if (strcmp(a->kind, "shell_config") == 0) {
        char *cache_zsh = get_shell_completion_cache_path(config_home, "zsh");
        char *cache_bash = get_shell_completion_cache_path(config_home, "bash");
        char *cache_fish = get_shell_completion_cache_path(config_home, "fish");
        char *local_claude_path = path_join(config_home, "local/claude");
        const char *cache_path = cache_zsh;
        if (strstr(abs_or_rel, ".bashrc") != NULL) cache_path = cache_bash;
        else if (strstr(abs_or_rel, "config.fish") != NULL) cache_path = cache_fish;
        rc = cleanup_shell_config_file(abs_or_rel, cache_path, local_claude_path);
        free(cache_zsh);
        free(cache_bash);
        free(cache_fish);
        free(local_claude_path);
      }
#ifndef _WIN32
#ifndef __APPLE__
      else if (strcmp(a->kind, "mimeapps_config") == 0) {
        rc = cleanup_mimeapps_file(abs_or_rel);
      }
#endif
#endif
      else if (path_exists(abs_or_rel) || path_is_symlink(abs_or_rel)) {
        rc = remove_recursive(abs_or_rel);
      }
      free(abs_or_rel);
    }

    if (rc == 0) {
      cleanup_results_push(results, a->kind, a->identifier, "removed", a->note);
    } else {
      cleanup_results_push(results, a->kind, a->identifier, "failed",
                           "删除失败");
    }
  }
}

static char **split_tabs(char *line, size_t *count_out) {
  size_t cap = 8, len = 0;
  char **parts = (char **)malloc(cap * sizeof(char *));
  if (!parts) exit(2);
  char *p = line;
  while (true) {
    if (len == cap) {
      cap *= 2;
      parts = (char **)realloc(parts, cap * sizeof(char *));
      if (!parts) exit(2);
    }
    parts[len++] = p;
    char *tab = strchr(p, '\t');
    if (!tab) break;
    *tab = '\0';
    p = tab + 1;
  }
  *count_out = len;
  return parts;
}

static int restore_fs_entry(const char *backup_dir, const char *enc_orig, const char *enc_rel) {
  char *orig = field_decode(enc_orig);
  char *rel = field_decode(enc_rel);
  char *src = path_join(backup_dir, rel);
  int rc = 0;
  if (path_exists(orig) || path_is_symlink(orig)) {
    rc = remove_recursive(orig);
  }
  if (rc == 0) {
    rc = copy_tree_recursive(src, orig);
  }
  free(orig);
  free(rel);
  free(src);
  return rc;
}

#ifdef __APPLE__
static int restore_mackey_entry(const char *enc_service, const char *enc_secret) {
  char *service = field_decode(enc_service);
  char *secret = field_decode(enc_secret);
  if (!is_safe_mac_keychain_service_name(service)) {
    free(service);
    free(secret);
    return 1;
  }
  const char *user = get_env_dup("USER");
  if (!user) user = "claude-code-user";
  const char *argv[] = {"security", "add-generic-password", "-U", "-a", user,
                        "-s", service, "-w", secret, NULL};
  int rc = run_process_status(argv, true);
  free(service);
  free(secret);
  return rc == 0 ? 0 : 1;
}
#endif

#ifdef _WIN32
static int restore_regkey_entry(const char *enc_subkey) {
  char *subkey = field_decode(enc_subkey);
  HKEY hkey = NULL;
  LONG st = RegCreateKeyExA(HKEY_CURRENT_USER, subkey, 0, NULL, 0,
                            KEY_SET_VALUE, NULL, &hkey, NULL);
  if (hkey) RegCloseKey(hkey);
  free(subkey);
  return st == ERROR_SUCCESS ? 0 : 1;
}

static int restore_regval_entry(const char *enc_subkey,
                                const char *enc_value,
                                const char *type_s,
                                const char *hex_data_s) {
  char *subkey = field_decode(enc_subkey);
  char *value = field_decode(enc_value);
  size_t data_len = 0;
  unsigned char *data = hex_to_bytes(hex_data_s, &data_len);
  if (!data && strlen(hex_data_s) != 0) {
    free(subkey);
    free(value);
    return 1;
  }
  HKEY hkey = NULL;
  LONG st = RegCreateKeyExA(HKEY_CURRENT_USER, subkey, 0, NULL, 0,
                            KEY_SET_VALUE, NULL, &hkey, NULL);
  if (st == ERROR_SUCCESS) {
    const char *real_name = (strcmp(value, "(Default)") == 0) ? NULL : value;
    DWORD type = (DWORD)strtoul(type_s, NULL, 10);
    st = RegSetValueExA(hkey, real_name, 0, type, data, (DWORD)data_len);
  }
  if (hkey) RegCloseKey(hkey);
  free(subkey);
  free(value);
  free(data);
  return st == ERROR_SUCCESS ? 0 : 1;
}

static int restore_wincred_entry(const char *enc_target,
                                 const char *type_s,
                                 const char *persist_s,
                                 const char *enc_user,
                                 const char *enc_comment,
                                 const char *blob_hex) {
  char *target = field_decode(enc_target);
  char *user = field_decode(enc_user);
  char *comment = field_decode(enc_comment);
  size_t blob_len = 0;
  unsigned char *blob = hex_to_bytes(blob_hex, &blob_len);
  if (!blob) {
    free(target); free(user); free(comment);
    return 1;
  }
  CREDENTIALA cred;
  memset(&cred, 0, sizeof(cred));
  cred.Type = (DWORD)strtoul(type_s, NULL, 10);
  cred.TargetName = target;
  cred.Persist = (DWORD)strtoul(persist_s, NULL, 10);
  cred.UserName = user;
  cred.Comment = comment;
  cred.CredentialBlobSize = (DWORD)blob_len;
  cred.CredentialBlob = blob;
  BOOL ok = CredWriteA(&cred, 0);
  free(target); free(user); free(comment); free(blob);
  return ok ? 0 : 1;
}
#endif

static bool should_allow_restore_fs_path(const char *orig, const char *home, bool allow_unsafe) {
  return allow_unsafe || is_safe_restore_path(orig, home);
}

#ifdef __APPLE__
static bool should_allow_restore_mackey(const char *service, bool allow_unsafe) {
  return allow_unsafe || is_safe_mac_keychain_service_name(service);
}
#endif

#ifdef _WIN32
static bool should_allow_restore_regkey(const char *subkey, bool allow_unsafe) {
  return allow_unsafe || is_safe_windows_registry_subkey(subkey);
}

static bool should_allow_restore_wincred(const char *target, bool allow_unsafe) {
  return allow_unsafe || is_windows_related_credential(target);
}
#endif

int restore_from_backup_dir(const char *backup_dir,
                                   bool dry_run,
                                   const char *home,
                                   bool allow_unsafe_restore,
                                   CleanupResults *results) {
  cleanup_results_init(results);
  char *manifest_path = path_join(backup_dir, "manifest.tsv");
  FILE *fp = fopen(manifest_path, "rb");
  free(manifest_path);
  if (!fp) {
    cleanup_results_push(results, "restore", backup_dir, "failed", "未找到 manifest.tsv");
    return 1;
  }

  char line[16384];
  int failed = 0;
  int manifest_version = 1;
#ifndef _WIN32
  (void)manifest_version;
#endif
  while (fgets(line, sizeof(line), fp)) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
    if (len == 0) continue;
    size_t count = 0;
    char **parts = split_tabs(line, &count);
    if (count == 0) {
      free(parts);
      continue;
    }
    if (strcmp(parts[0], "VERSION") == 0) {
      if (count >= 2) {
        manifest_version = atoi(parts[1]);
      }
      free(parts);
      continue;
    }
    if (dry_run) {
      cleanup_results_push(results, "restore", parts[0], "skipped", "dry-run");
      free(parts);
      continue;
    }
    int rc = 1;
    if (strcmp(parts[0], "FS") == 0 && count >= 4) {
      char *orig = field_decode(parts[1]);
      if (!should_allow_restore_fs_path(orig, home, allow_unsafe_restore)) {
        cleanup_results_push(results, "filesystem", orig, "failed",
                             "恢复目标不在允许范围内；如确认可信，可加 --allow-unsafe-restore");
        rc = 1;
      } else {
        rc = restore_fs_entry(backup_dir, parts[1], parts[2]);
        cleanup_results_push(results, "filesystem", orig, rc == 0 ? "restored" : "failed",
                             rc == 0 ? "已恢复" : "恢复失败");
      }
      free(orig);
    } else if (strcmp(parts[0], "MACKEY") == 0 && count >= 3) {
#ifdef __APPLE__
      char *service = field_decode(parts[1]);
      if (!should_allow_restore_mackey(service, allow_unsafe_restore)) {
        cleanup_results_push(results, "keychain", service, "failed",
                             "Keychain 服务名不在允许范围内；如确认可信，可加 --allow-unsafe-restore");
        rc = 1;
      } else {
        rc = restore_mackey_entry(parts[1], parts[2]);
        cleanup_results_push(results, "keychain", service, rc == 0 ? "restored" : "failed",
                             rc == 0 ? "已恢复" : "恢复失败");
      }
      free(service);
#else
      cleanup_results_push(results, "keychain", parts[1], "failed", "当前平台不支持恢复 macOS keychain");
#endif
    } else if (strcmp(parts[0], "REGKEY") == 0 && count >= 2) {
#ifdef _WIN32
      char *sub = field_decode(parts[1]);
      if (!should_allow_restore_regkey(sub, allow_unsafe_restore)) {
        cleanup_results_push(results, "registry", sub, "failed",
                             "注册表键不在允许范围内；如确认可信，可加 --allow-unsafe-restore");
        rc = 1;
      } else {
        rc = restore_regkey_entry(parts[1]);
        cleanup_results_push(results, "registry", sub, rc == 0 ? "restored" : "failed",
                             rc == 0 ? "已恢复" : "恢复失败");
      }
      free(sub);
#else
      cleanup_results_push(results, "registry", parts[1], "failed", "当前平台不支持恢复 Windows 注册表");
#endif
    } else if (strcmp(parts[0], "REGVAL") == 0 && count >= 4) {
#ifdef _WIN32
      char *sub = field_decode(parts[1]);
      char *val = field_decode(parts[2]);
      char *ident = str_printf("%s [%s]", sub, val);
      if (!should_allow_restore_regkey(sub, allow_unsafe_restore)) {
        cleanup_results_push(results, "registry_value", ident, "failed",
                             "注册表值不在允许范围内；如确认可信，可加 --allow-unsafe-restore");
        rc = 1;
      } else if (count >= 5) {
        rc = restore_regval_entry(parts[1], parts[2], parts[3], parts[4]);
        cleanup_results_push(results, "registry_value", ident, rc == 0 ? "restored" : "failed",
                             rc == 0 ? "已恢复" : "恢复失败");
      } else {
        cleanup_results_push(results, "registry_value", ident, "failed",
                             manifest_version >= 2
                                 ? "manifest 中 REGVAL 条目格式无效"
                                 : "旧版 REGVAL 备份不再默认恢复；如确认可信，可重新清理生成新版备份");
        rc = 1;
      }
      free(sub); free(val); free(ident);
#else
      cleanup_results_push(results, "registry_value", parts[1], "failed", "当前平台不支持恢复 Windows 注册表");
#endif
    } else if (strcmp(parts[0], "WINCRED") == 0 && count >= 7) {
#ifdef _WIN32
      char *target = field_decode(parts[1]);
      if (!should_allow_restore_wincred(target, allow_unsafe_restore)) {
        cleanup_results_push(results, "credential", target, "failed",
                             "Credential 目标名不在允许范围内；如确认可信，可加 --allow-unsafe-restore");
        rc = 1;
      } else {
        rc = restore_wincred_entry(parts[1], parts[2], parts[3], parts[4], parts[5], parts[6]);
        cleanup_results_push(results, "credential", target, rc == 0 ? "restored" : "failed",
                             rc == 0 ? "已恢复" : "恢复失败");
      }
      free(target);
#else
      cleanup_results_push(results, "credential", parts[1], "failed", "当前平台不支持恢复 Windows Credential");
#endif
    }
    if (rc != 0) failed = 1;
    free(parts);
  }
  fclose(fp);
  return failed ? 1 : 0;
}
