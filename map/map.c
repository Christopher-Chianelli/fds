#include "map.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>

struct fds_map *fds_map_create() {

  char *template = malloc(32);
  strcpy(template, "/tmp/tmpdir.XXXXXX");
  char *tmp_dirname = mkdtemp(template);

  if(tmp_dirname == NULL)
  {
     return NULL;
  }

  struct fds_map *out = malloc(sizeof(struct fds_map));
  out->map_directory = template;
  return out;
}

struct fds_map *fds_map_create_in_dir(char *dir) {
  struct fds_map *out = malloc(sizeof(struct fds_map));
  out->map_directory = dir;
  return out;
}

static char *escape(const char *key) {
  char *out = malloc(PATH_MAX);
  char *end = out;
  *end = 'F';
  end++;
  while (*key != '\0') {
    for (int i = 0; i < (CHAR_BIT >> 2); i++) {
      char key_part = ((*key) & (0xF << (i * 4))) >> (i * 4);
      *end = key_part + '0';
      end++;
    }
    key++;
  }
  *end = '\0';
  return out;
}

static char *unescape(const char *key) {
  char *out = malloc(PATH_MAX);
  char *end = out;
  key++;
  while (*key != '\0') {
    *end = 0;
    for (int i = 0; i < (CHAR_BIT >> 2); i++) {
      char key_part = (*key - '0') << (i * 4);
      *end |= key_part;
      key++;
    }
    end++;
  }
  *end = '\0';
  return out;
}


static char *get_file_for_entry(struct fds_map *map, const char *key) {
  char *key_path = malloc(PATH_MAX);
  strcpy(key_path, map->map_directory);
  char *rest = key_path + strlen(map->map_directory);
  *rest = '/';
  rest++;
  char *escaped_key = escape(key);
  strcpy(rest,escaped_key);
  free(escaped_key);
  return key_path;
}

void fds_map_put(struct fds_map *map, const char *key, const void *data, size_t data_length) {
  char *key_path = get_file_for_entry(map, key);
  int fd = syscall(SYS_open, key_path ,O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  free(key_path);
  if (fd == -1) {
    return;
  }

  int written = 0;
  do {
    int wrote = syscall(SYS_write, fd,data,data_length - written);
    if (wrote == -1) {
      syscall(SYS_close, fd);
      return;
    }
    data += wrote;
    data_length -= wrote;
  } while (data_length > 0);
  syscall(SYS_close, fd);
}

void *fds_map_get(struct fds_map *map, const char *key) {
  char *key_path = get_file_for_entry(map, key);

  struct stat st;
  stat(key_path, &st);
  size_t size = st.st_size;

  int fd = syscall(SYS_open, key_path, O_RDONLY);
  free(key_path);

  if (fd == -1) {
    return NULL;
  }

  void *out = malloc(size);
  void *curr = out;
  size_t bytes_read = 0;

  do {
    int readed = syscall(SYS_read, fd, curr, size - bytes_read);
    if (readed == -1) {
      syscall(SYS_close, fd);
      free(out);
      return NULL;
    }
    bytes_read += readed;
    curr += readed;
  } while (bytes_read != size);

  return out;
}

void fds_map_remove(struct fds_map *map, const char *key) {
  char *key_path = get_file_for_entry(map, key);
  remove(key_path);
  free(key_path);
}

size_t fds_map_size(struct fds_map *map) {
  struct dirent *dp;
  DIR *dfd;
  size_t out = 0;

  if ((dfd = opendir(map->map_directory)) == NULL) {
      return -1;
  }
  while ((dp = readdir(dfd)) != NULL) {
    if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
      continue;
    }
    out++;
  }
  closedir(dfd);
  return out;
}

void fds_map_foreach(struct fds_map *map, void (*fn)(struct fds_map_entry *)) {
  struct dirent *dp;
  DIR *dfd;
  char buf[PATH_MAX];

  if ((dfd = opendir(map->map_directory)) == NULL) {
      printf("Couldn't open directory\n");
      return;
  }
  while ((dp = readdir(dfd)) != NULL) {
    if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
      continue;
    }
    sprintf(buf, "%s/%s", map->map_directory, dp->d_name);

    struct stat st;
    stat(buf, &st);
    size_t size = st.st_size;

    int fd = syscall(SYS_open, buf, O_RDONLY);

    if (fd == -1) {
      continue;
    }

    void *value = malloc(size);
    void *curr = value;
    size_t bytes_read = 0;

    do {
      int readed = syscall(SYS_read, fd, curr, size - bytes_read);
      if (readed == -1) {
        close(fd);
        free(value);
        value = NULL;
        break;
      }
      bytes_read += readed;
      curr += readed;
    } while (bytes_read != size);

    syscall(SYS_close, fd);
    char *key = unescape(dp->d_name);
    struct fds_map_entry entry;
    entry.key = key;
    entry.data = value;
    entry.data_length = size;
    fn(&entry);

    free(key);
    free(value);
  }
  closedir(dfd);
}



void fds_map_free(struct fds_map *map) {
  struct dirent *dp;
  DIR *dfd;
  char buf[PATH_MAX];

  if ((dfd = opendir(map->map_directory)) == NULL) {
      return;
  }
  while ((dp = readdir(dfd)) != NULL) {
    if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
      continue;
    }
    sprintf(buf, "%s/%s", map->map_directory, dp->d_name);

    int fd = syscall(SYS_open, buf, O_RDONLY);

    if (fd == -1) {
      continue;
    }
    sprintf(buf, "%s/%s", map->map_directory, dp->d_name);
    remove(buf);

    syscall(SYS_close, fd);
  }
  closedir(dfd);
  remove(map->map_directory);
  free(map->map_directory);
  free(map);
}
