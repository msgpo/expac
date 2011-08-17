/* Copyright (c) 2010-2011 Dave Reisner
 *
 * expac.c
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#define _GNU_SOURCE
#include <alpm.h>
#include <ctype.h>
#include <getopt.h>
/* #include <limits.h> */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_DELIM        "\n"
#define DEFAULT_LISTDELIM    "  "
#define DEFAULT_TIMEFMT      "%c"
#define FORMAT_TOKENS        "BCDEGLNOPRSabdhmnprsuvw%"
#define FORMAT_TOKENS_LOCAL  "ilFw"
#define FORMAT_TOKENS_SYNC   "fgk"
#define ESCAPE_TOKENS        "\"\\abefnrtv"

#ifndef PATH_MAX
#define PATH_MAX  4096
#endif

static char const digits[] = "0123456789";
static char const printf_flags[] = "'-+ #0I";

alpm_db_t *db_local = NULL;
alpm_list_t *dblist = NULL;
alpm_list_t *targets = NULL;
bool readone = false;
bool verbose = false;
bool search = false;
bool local = false;
bool groups = false;
const char *format = NULL;
const char *timefmt = NULL;
const char *listdelim = NULL;
const char *delim = NULL;
int pkgcounter = 0;

typedef const char *(*extractfn)(void*);

static char *strtrim(char *str) {
  char *pch = str;

  if (!str || *str == '\0') {
    return str;
  }

  while (isspace((unsigned char)*pch)) {
    pch++;
  }
  if (pch != str) {
    memmove(str, pch, (strlen(pch) + 1));
  }

  if (*str == '\0') {
    return str;
  }

  pch = (str + (strlen(str) - 1));
  while (isspace((unsigned char)*pch)) {
    pch--;
  }
  *++pch = '\0';

  return str;
}

static char *trim_optdep(char *optdep) {
  char *colon;

  colon = strchr(optdep, ':');
  if (colon) {
    *colon = '\0';
  }

  return optdep;
}

static alpm_handle_t *alpm_init(void) {
  alpm_handle_t *handle = NULL;
  enum _alpm_errno_t alpm_errno = 0;
  FILE *fp;
  char line[PATH_MAX];
  char *ptr, *section = NULL;

  handle = alpm_initialize("/", "/var/lib/pacman", &alpm_errno);
  if (!handle) {
    alpm_strerror(alpm_errno);
    return NULL;
  }

  db_local = alpm_option_get_localdb(handle);

  fp = fopen("/etc/pacman.conf", "r");
  if (!fp) {
    perror("fopen: /etc/pacman.conf");
    return handle;
  }

  while (fgets(line, PATH_MAX, fp)) {
    strtrim(line);

    if (strlen(line) == 0 || line[0] == '#') {
      continue;
    }
    if ((ptr = strchr(line, '#'))) {
      *ptr = '\0';
    }

    if (line[0] == '[' && line[strlen(line) - 1] == ']') {
      ptr = &line[1];
      if (section) {
        free(section);
      }

      section = strdup(ptr);
      section[strlen(section) - 1] = '\0';

      if (strcmp(section, "options") != 0) {
        alpm_db_register_sync(handle, section,
            ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL);
      }
    }
  }

  free(section);
  fclose(fp);
  return handle;
}

static const char *alpm_dep_get_name(void *dep) {
  return ((alpm_depend_t*)dep)->name;
}

static void usage(void) {
  fprintf(stderr, "expac %s\n"
      "Usage: expac [options] <format> target...\n\n", VERSION);
  fprintf(stderr,
      " Options:\n"
      "  -Q, --local               search local DB (default)\n"
      "  -S, --sync                search sync DBs\n"
      "  -s, --search              search for matching regex\n"
      "  -g, --group               return packages matching targets as groups\n"
      "  -1, --readone             return only the first result of a sync search\n\n"
      "  -d, --delim <string>      separator used between packages (default: \"\\n\")\n"
      "  -l, --listdelim <string>  separator used between list elements (default: \"  \")\n"
      "  -t, --timefmt <fmt>       date format passed to strftime (default: \"%%c\")\n\n"
      "  -v, --verbose             be more verbose\n\n"
      "  -h, --help                display this help and exit\n\n");
}

static int parse_options(int argc, char *argv[], alpm_handle_t *handle) {
  int opt, option_index = 0;

  static struct option opts[] = {
    {"readone",   no_argument,        0, '1'},
    {"delim",     required_argument,  0, 'd'},
    {"listdelim", required_argument,  0, 'l'},
    {"group",     required_argument,  0, 'g'},
    {"help",      no_argument,        0, 'h'},
    {"local",     no_argument,        0, 'Q'},
    {"sync",      no_argument,        0, 'S'},
    {"search",    no_argument,        0, 's'},
    {"timefmt",   required_argument,  0, 't'},
    {"verbose",   no_argument,        0, 'v'},
    {0, 0, 0, 0}
  };

  while (-1 != (opt = getopt_long(argc, argv, "1l:d:ghf:QSst:v", opts, &option_index))) {
    switch (opt) {
      case 'S':
        if (dblist) {
          fprintf(stderr, "error: can only select one repo option (use -h for help)\n");
          return 1;
        }
        dblist = alpm_list_copy(alpm_option_get_syncdbs(handle));
        break;
      case 'Q':
        if (dblist) {
          fprintf(stderr, "error: can only select one repo option (use -h for help)\n");
          return 1;
        }
        dblist = alpm_list_add(dblist, db_local);
        local = true;
        break;
      case '1':
        readone = true;
        break;
      case 'd':
        delim = optarg;
        break;
      case 'g':
        groups = true;
        break;
      case 'l':
        listdelim = optarg;
        break;
      case 'h':
        usage();
        return 1;
      case 's':
        search = true;
        break;
      case 't':
        timefmt = optarg;
        break;
      case 'v':
        verbose = true;
        break;

      case '?':
        return 1;
      default:
        return 1;
    }
  }

  if (optind < argc) {
    format = argv[optind++];
  } else {
    fprintf(stderr, "error: missing format string (use -h for help)\n");
    return 1;
  }

  while (optind < argc) {
    targets = alpm_list_add(targets, argv[optind++]);
  }

  return 0;
}

static int print_escaped(const char *delim) {
  const char *f;
  int out = 0;

  for (f = delim; *f != '\0'; f++) {
    if (*f == '\\') {
      switch (*++f) {
        case '\\':
          putchar('\\');
          break;
        case '"':
          putchar('\"');
          break;
        case 'a':
          putchar('\a');
          break;
        case 'b':
          putchar('\b');
          break;
        case 'e': /* \e is nonstandard */
          putchar('\033');
          break;
        case 'n':
          putchar('\n');
          break;
        case 'r':
          putchar('\r');
          break;
        case 't':
          putchar('\t');
          break;
        case 'v':
          putchar('\v');
          break;
        case '0':
          putchar('\0');
          break;
        ++out;
      }
    } else {
      putchar(*f);
      ++out;
    }
  }

  return out;
}

static int print_list(alpm_list_t *list, extractfn fn, bool shortdeps) {
  alpm_list_t *i;
  int out = 0;

  if (!list) {
    if (verbose) {
      out += printf("None");
    }
    return out;
  }

  i = list;
  while (1) {
    char *item;

    item = (char*)(fn ? fn(alpm_list_getdata(i)) : alpm_list_getdata(i));

    if (shortdeps) {
      *(item + strcspn(item, "<>=")) = '\0';
    }

    out += printf("%s", item);

    if ((i = alpm_list_next(i))) {
      out += print_escaped(listdelim);
    } else {
      break;
    }
  }

  return out;
}

static int print_time(time_t timestamp) {
  char buffer[64];
  int out = 0;

  if (!timestamp) {
    if (verbose) {
      out += printf("None");
    }
    return out;
  }

  /* no overflow here, strftime prints a max of 64 including null */
  strftime(&buffer[0], 64, timefmt, localtime(&timestamp));
  out += printf("%s", buffer);

  return out;
}

static int print_filelist(alpm_filelist_t *filelist) {
  int out = 0;
  size_t i;

  for (i = 0; i < filelist->count; i++) {
    out += printf("%s", (filelist->files + i)->name);
    out += print_escaped(listdelim);
  }

  return out;
}

static int print_pkg(alpm_pkg_t *pkg, const char *format) {
  const char *f, *end;
  char fmt[32];
  int len, out = 0;

  end = rawmemchr(format, '\0');

  for (f = format; f < end; f++) {
    bool shortdeps = false;
    len = 0;
    if (*f == '%') {
      len = strspn(f + 1 + len, printf_flags);
      len += strspn(f + 1 + len, digits);
      snprintf(fmt, len + 3, "%ss", f);
      fmt[len + 1] = 's';
      f += len + 1;
      switch (*f) {
        /* simple attributes */
        case 'f': /* filename */
          out += printf(fmt, alpm_pkg_get_filename(pkg));
          break;
        case 'n': /* package name */
          out += printf(fmt, alpm_pkg_get_name(pkg));
          break;
        case 'v': /* version */
          out += printf(fmt, alpm_pkg_get_version(pkg));
          break;
        case 'd': /* description */
          out += printf(fmt, alpm_pkg_get_desc(pkg));
          break;
        case 'u': /* project url */
          out += printf(fmt, alpm_pkg_get_url(pkg));
          break;
        case 'p': /* packager name */
          out += printf(fmt, alpm_pkg_get_packager(pkg));
          break;
        case 's': /* md5sum */
          out += printf(fmt, alpm_pkg_get_md5sum(pkg));
          break;
        case 'a': /* architecutre */
          out += printf(fmt, alpm_pkg_get_arch(pkg));
          break;
        case 'i': /* has install scriptlet? */
          out += printf(fmt, alpm_pkg_has_scriptlet(pkg) ? "yes" : "no");
          break;
        case 'r': /* repo */
          out += printf(fmt, alpm_db_get_name(alpm_pkg_get_db(pkg)));
          break;
        case 'w': /* install reason */
          out += printf(fmt, alpm_pkg_get_reason(pkg) ? "dependency" : "explicit");
          break;
        case '!': /* result number */
          out += printf("%d", pkgcounter++);
          break;
        case 'g': /* base64 gpg sig */
          out += printf(fmt, alpm_pkg_get_base64_sig(pkg));
          break;
        case 'h': /* sha256sum */
          out += printf(fmt, alpm_pkg_get_sha256sum(pkg));
          break;

        /* times */
        case 'b': /* build date */
          out += print_time(alpm_pkg_get_builddate(pkg));
          break;
        case 'l': /* install date */
          out += print_time(alpm_pkg_get_installdate(pkg));
          break;

        /* sizes */
        case 'k': /* download size */
          out += printf("%.2f KiB", (float)alpm_pkg_get_size(pkg) / 1024.0);
          break;
        case 'm': /* install size */
          out += printf("%.2f KiB", (float)alpm_pkg_get_isize(pkg) / 1024.0);
          break;

        /* lists */
        case 'F': /* files */
          out += print_filelist(alpm_pkg_get_files(pkg));
          break;
        case 'N': /* requiredby */
          out += print_list(alpm_pkg_compute_requiredby(pkg), NULL, shortdeps);
          break;
        case 'L': /* licenses */
          out += print_list(alpm_pkg_get_licenses(pkg), NULL, shortdeps);
          break;
        case 'G': /* groups */
          out += print_list(alpm_pkg_get_groups(pkg), NULL, shortdeps);
          break;
        case 'E': /* depends (shortdeps) */
          out += print_list(alpm_pkg_get_depends(pkg), (extractfn)alpm_dep_get_name, shortdeps);
          break;
        case 'D': /* depends */
          out += print_list(alpm_pkg_get_depends(pkg), (extractfn)alpm_dep_compute_string, shortdeps);
          break;
        case 'O': /* optdepends */
          out += print_list(alpm_pkg_get_optdepends(pkg), NULL, shortdeps);
          break;
        case 'o': /* optdepends (shortdeps) */
          out += print_list(alpm_pkg_get_optdepends(pkg), (extractfn)trim_optdep, shortdeps);
          break;
        case 'C': /* conflicts */
          out += print_list(alpm_pkg_get_conflicts(pkg), NULL, shortdeps);
          break;
        case 'S': /* provides (shortdeps) */
          shortdeps = true;
        case 'P': /* provides */
          out += print_list(alpm_pkg_get_provides(pkg), NULL, shortdeps);
          break;
        case 'R': /* replaces */
          out += print_list(alpm_pkg_get_replaces(pkg), NULL, shortdeps);
          break;
        case 'B': /* backup */
          out += print_list(alpm_pkg_get_backup(pkg), NULL, shortdeps);
          break;
        case '%':
          putchar('%');
          out++;
          break;
        default:
          putchar('?');
          out++;
          break;
      }
    } else if (*f == '\\') {
      char buf[3]; /* its not safe to do this in a single sprintf */
      buf[0] = *f;
      buf[1] = *++f;
      buf[2] = '\0';
      out += print_escaped(buf);
    } else {
      putchar(*f);
      out++;
    }
  }

  /* only print a delimeter if any package data was outputted */
  if (out > 0) {
    print_escaped(delim);
  }

  return !out;
}

static alpm_list_t *resolve_pkg(alpm_list_t *targets) {
  char *pkgname, *reponame;
  alpm_list_t *t, *r, *ret = NULL;

  if (!targets) {
    for (r = dblist; r; r = alpm_list_next(r)) {
      /* joining causes corruption on alpm_release(), so we copy */
      ret = alpm_list_join(ret, alpm_list_copy(alpm_db_get_pkgcache(alpm_list_getdata(r))));
    }
  } else if (search) {
    for (r = dblist; r; r = alpm_list_next(r)) {
      ret = alpm_list_join(ret, alpm_db_search(alpm_list_getdata(r), targets));
    }
  } else if (groups) {
    for (t = targets; t; t = alpm_list_next(t)) {
      for (r = dblist; r; r = alpm_list_next(r)) {
        alpm_group_t *grp = alpm_db_readgroup(alpm_list_getdata(r), alpm_list_getdata(t));
        if (grp) {
          ret = alpm_list_join(ret, alpm_list_copy(grp->packages));
        }
      }
    }
  } else {
    for (t = targets; t; t = alpm_list_next(t)) {
      alpm_pkg_t *pkg = NULL;
      int found = 0;

      pkgname = reponame = alpm_list_getdata(t);
      if (strchr(pkgname, '/')) {
        strsep(&pkgname, "/");
      } else {
        reponame = NULL;
      }

      for (r = dblist; r; r = alpm_list_next(r)) {
        alpm_db_t *repo = alpm_list_getdata(r);

        if (reponame && strcmp(reponame, alpm_db_get_name(repo)) != 0) {
          continue;
        }

        if (!(pkg = alpm_db_get_pkg(repo, pkgname)) &&
            !(pkg = alpm_find_satisfier(alpm_db_get_pkgcache(repo), pkgname))) {
          continue;
        }

        found = 1;
        ret = alpm_list_add(ret, pkg);
        if (readone) {
          break;
        }
      }
      if (!found && verbose) {
        fprintf(stderr, "error: package `%s' not found\n", pkgname);
      }
    }
  }

  return ret;
}

int main(int argc, char *argv[]) {
  int ret = 1;
  alpm_handle_t *handle;
  alpm_list_t *results, *i;

  handle = alpm_init();
  if (!handle) {
    return ret;
  }

  ret = parse_options(argc, argv, handle);
  if (ret != 0) {
    goto finish;
  }

  /* ensure sane defaults */
  if (!dblist) {
    local = true;
    dblist = alpm_list_add(dblist, db_local);
  }
  delim = delim ? delim : DEFAULT_DELIM;
  listdelim = listdelim ? listdelim : DEFAULT_LISTDELIM;
  timefmt = timefmt ? timefmt : DEFAULT_TIMEFMT;

  results = resolve_pkg(targets);
  if (!results) {
    ret = 1;
    goto finish;
  }

  for (i = results; i; i = alpm_list_next(i)) {
    alpm_pkg_t *pkg = alpm_list_getdata(i);
    ret += print_pkg(pkg, format);
  }
  ret = !!ret; /* clamp to zero/one */

  alpm_list_free(results);

finish:
  alpm_list_free(dblist);
  alpm_list_free(targets);
  alpm_release(handle);
  return ret;
}

