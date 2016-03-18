#include <ccan/err/err.h>
#include <ccan/tal/path/path.h>
#include <ccan/tal/str/str.h>
#include <ccan/short_types/short_types.h>
#include <unistd.h>
#include <stdio.h>
#include <pwd.h>
#include <dirent.h>
#include "io.h"
#include "blockfiles.h"

static void add_name(char ***names_p, unsigned int num, char *name)
{
  size_t count = tal_count(*names_p);
  if (num >= count) {
    tal_resize(names_p, num + 1);
    memset(*names_p + count, 0, sizeof(char *) * (num + 1 - count));
  }
  if ((*names_p)[num])
    errx(1, "Duplicate block file for %u? '%s' and '%s'",
	 num, name, (*names_p)[num]);
  (*names_p)[num] = name;
}

char **block_filenames(tal_t *ctx, const char *path, bool testnet3)
{
  char **names = tal_arr(ctx, char *, 0);
  char *tmp_ctx = tal_arr(ctx, char, 0);
  DIR *dir;
  struct dirent *ent;

  if (!path) {
    char *base = getenv("HOME");
    if (!base) {
      struct passwd *passwd = getpwuid(getuid());
      if (!passwd)
	err(1, "Could not get home dir");
      base = passwd->pw_dir;
    }

    base = path_join(tmp_ctx, base, ".bitcoin");
    if (testnet3)
      base = path_join(tmp_ctx, base, "testnet3");

    /* First try new-style: $HOME/.bitcoin/blocks/blk[0-9]*.dat. */
    path = path_join(tmp_ctx, base, "blocks");
    dir = opendir(path);
    if (!dir) {
      /* Old-style: $HOME/.bitcoin/blk[0-9]*.dat. */
      path = base;
      dir = opendir(path);
    }
  } else
    dir = opendir(path);

  if (!dir)
    err(1, "Could not open bitcoin dir '%s'", path);

  while ((ent = readdir(dir)) != NULL) {
    char *numstr;
    int num;
    if (!tal_strreg(tmp_ctx, ent->d_name,
		    "^blk([0-9]+)\\.dat$", &numstr))
      continue;
    num = strtol(numstr, NULL, 10);
    add_name(&names, num, path_join(names, path, ent->d_name));
  }
  tal_free(tmp_ctx);
  return names;
}

/* Cache file opens. */
struct file *block_file(char **block_fnames, unsigned int index, bool use_mmap)
{
#define NUM_BLOCKFILES 2
  static struct file f[NUM_BLOCKFILES];
  static size_t next;
  size_t i;

  for (i = 0; i < NUM_BLOCKFILES; i++) {
    if (f[i].name == block_fnames[index])
      return f+i;
  }

  /* Kick one out. */
  i = next;
  if (f[i].name)
    file_close(&f[i]);

  file_open(&f[i], block_fnames[index], 0,
	    O_RDONLY | (use_mmap ? 0 : O_NO_MMAP));
  next++;
  if (next == NUM_BLOCKFILES)
    next = 0;
  return f + i;
}
