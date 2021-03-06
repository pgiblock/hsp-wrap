#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "stdiowrap/stdiowrap.h"

int
main(int argc, char **argv)
{
  FILE *outf, *inf;
  char c[40];
  int i;

  struct timespec ts = {.tv_sec=0, .tv_nsec=10000000};

  outf = stdiowrap_fopen(argv[1], "w");
  inf  = stdiowrap_fopen(argv[2], "r");

  if (!outf || !inf) {
    fprintf(stderr, "Could not open file(s)\n");
    exit(EXIT_FAILURE);
  }

  for (i=0; stdiowrap_fgets(c, sizeof(c), inf) != NULL; ++i) {
    printf("Got string: '%s'\n", c);
    stdiowrap_fputs(c, outf);

    nanosleep(&ts, NULL);
  }

  //fprintf(stderr, "Wrote %d lines\n. Done.", i);
  stdiowrap_fclose(inf);
  stdiowrap_fclose(outf);

  return EXIT_SUCCESS;
}
