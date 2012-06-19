#ifndef HSP_ZUTILS_H__
#define HSP_ZUTILS_H__

#include <stdio.h> // FILE
#include <zlib.h>  // Z_* return codes

/* Decompress from file source to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */
int zutil_inf(FILE *dest, FILE *source, int *blks);


// Writes "sz" bytes from "source" to the stream "dest", compressing
// the data first with zlib compression strength "level".
//
// Special thanks to Mark Adler for providing the non-copyrighted
// public domain example program "zpipe.c", from which this function
// is based (Version 1.4  December 11th, 2005).
int zutil_compress_write(FILE *dest, void *source, int sz, int level);

#endif
