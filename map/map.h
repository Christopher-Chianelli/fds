#ifndef FDS_MAP_H
#define FDS_MAP_H

#include <stdlib.h>

struct fds_map {
  char *map_directory;
};

struct fds_map_entry {
  const char *key;
  void *data;
  size_t data_length;
};

extern struct fds_map *fds_map_create();
extern struct fds_map *fds_map_create_in_dir(char *dir);
extern void fds_map_put(struct fds_map *map, const char *key, const void *data, size_t data_length);
extern void *fds_map_get(struct fds_map *map, const char *key);
extern void fds_map_remove(struct fds_map *map, const char *key);
extern size_t fds_map_size(struct fds_map *map);
extern void fds_map_foreach(struct fds_map *map, void (*fn)(struct fds_map_entry *));
extern void fds_map_free(struct fds_map *map);

#endif //FDS_MAP_H
