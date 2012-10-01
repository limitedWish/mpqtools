#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "StormLib.h"

char *
format_filename(char *name, int create_dir)
{
  char *str, *ptr;

  str = ptr = strdup(name);
  if (str == NULL) {
    perror("strdup");
    return NULL;
  }

  while(*ptr != '\0') {
    if ((*ptr == '/') || (*ptr == '\\')) {
      if (create_dir) {
	*ptr = '\0';
	if (mkdir(str, 0755) < 0) {
	  if (errno != EEXIST) {
	    free(str);
	    return NULL;
	  }
	}
      }

      *ptr = '/';
    }

    ptr++;
  }

  return str;
}

int
usage(void)
{
  printf(""
	 "Usage: mpqx [options] <MPQ Archive>\n"
	 "             options: -a extract all files in archive\n"
	 "                      -p <pattern>\n"
	 "                         extract only files whos full path match the\n"
	 "                         <pattern>. Wildcast '?' and '*' can be used.\n"
	 "                      -l list all files in the archive\n"
	 "                      -L <listfile>\n"
	 "                         supply additional listfile\n"
	 "\n"
	 "Do nothing if no option is given.\n");

  return 0;
}

int
main(int argc, char **argv)
{
  HANDLE mpq, search;
  SFILE_FIND_DATA file;

  int i;
  TMPQArchive *ptr;
  TFileEntry  *fptr;

  int rv;
  int extract_all = 0;
  int show_listfile = 0;
  char *pattern = NULL;
  char *listfile = NULL;

  char *name;

  if (argc <= 2) {
    usage();
    return 0;
  }

  while ((rv = getopt(argc, argv, ":ap:lL:")) != -1) {
    switch (rv) {
    case 'a':
      extract_all = 1;
      break;
    case 'p':
      pattern = optarg;
      break;
    case 'l':
      show_listfile = 1;
      break;
    case 'L':
      listfile = optarg;
      break;
    case ':':
      printf("option '-%c' needs an argument!\n", optopt);
      return -1;
    case '?':
      printf("unknown option: '-%c'\n", optopt);
      /* fall through */
    default:
      usage();
      return -1;
    }
  }
  argc -= optind;
  argv += optind;

  if (argc != 1) {
    usage();
    return -1;
  }

  rv = SFileOpenArchive(argv[0], 0, BASE_PROVIDER_FILE, &mpq);
  if (!rv) {
    printf("SFileOpenArchive failed!\n");
    return -1;
  }

  search = SFileFindFirstFile(mpq, pattern?pattern:"*", &file, listfile);
  if (search == NULL) {
    printf("Can't find specified file in the archive.\n");
    return -1;
  }

  name = format_filename(file.cFileName, pattern || extract_all);
  if (show_listfile) {
    printf("%s\n", name);
  }

  if (pattern || extract_all) {
    SFileExtractFile(mpq, file.cFileName, name, SFILE_OPEN_FROM_MPQ);
  }

  free(name);

  while (SFileFindNextFile(search, &file) != 0) {
    name = format_filename(file.cFileName, pattern || extract_all);
    if (show_listfile) {
      printf("%s\n", name);
    }

    if (pattern || extract_all) {
      SFileExtractFile(mpq, file.cFileName, name, SFILE_OPEN_FROM_MPQ);
    }

    free(name);
  }

  SFileFindClose(search);

  SFileCloseArchive(mpq);

  return 0;
}
