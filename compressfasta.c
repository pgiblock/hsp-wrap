#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <zlib.h>


////////////////////////////////////////////////////////////////////////////////
//                              Types and Defines                             //
////////////////////////////////////////////////////////////////////////////////


// Information the master needs (presistent)
typedef struct st_info {
  char       *queries;      // Pointer to start of input queries
  char       *cquery;       // Current input query
  char       *equery;       // One byte past last valid byte of query
  char      **sequences;    // Array of sorted (at some point) inputs
  size_t     *sequencels;   // Array of sorted (at some point) inputs
  size_t      nsequences;   // Number of known sequences
} info_t;


// This is a small until; I'll just make this a global
info_t nfo;


////////////////////////////////////////////////////////////////////////////////
//                                Fasta Code                                  //
////////////////////////////////////////////////////////////////////////////////


// Like strcmp, but respects some custom bounds
static int seqcmp(char *s, char *m)
{
  while(s < nfo.equery) {
    if( !(*m) ) {
      return 0;
    } else {
      if( (*s) < (*m) ) {
        return -1;
      } else if( (*s) > (*m) ) {
        return 1;
      } else {
        s++;
        m++;
      }
    }
  }
  if( (*m) ) {
    return 1;
  } else {
    return 0;
  }
}


static int Sequence_Length(char *seq)
{
  char *p = seq;

  if( seq >= nfo.equery ) {
    return 0;
  }

  // Look forward until end of sequences are found or
  // new sequence start sequence is found.
  do {
    for(p++; (p < nfo.equery) && (*p != '>'); p++);
    if( p >= nfo.equery ) {
      // End of all sequences found
      if( p > seq+1 ) {
      // Some bytes were found before end..
        return nfo.equery-seq;
      } else {
        return 0;
      }
    }
  } while( seqcmp(p,">") );

  // Found a match at p
  return p-seq;
}


// Returns a pointer to the start of the next sequence
static char* Get_Sequence()
{
  char *c;
  int   len;
  
  if( (nfo.cquery >= nfo.equery) || !(len=Sequence_Length(nfo.cquery)) ) {
    // No more queries, return NULL
    return NULL;
  } else {
    c = nfo.cquery;
    // Advance to the next sequence
    nfo.cquery += len;
    // Return the current sequence
    return c;
  }
}


// Opens and maps the input query file
static void Init_Sequences(char *fn)
{
  struct stat statbf;
  int         f;

  // Open and memory map the input sequence file
  if( (f = open(fn,O_RDONLY)) < 0 ) {
    fprintf(stderr,"Could not open() query file. Terminating.\n");
    exit(1);
  }
  if( fstat(f, &statbf) < 0 ) {
    close(f);
    fprintf(stderr,"Could not fstat() opened query file. Terminating.\n");
    exit(1);
  }
  nfo.queries = mmap(NULL,statbf.st_size,PROT_READ,MAP_SHARED,f,0);
  close(f);
  if( nfo.queries == MAP_FAILED ) {
    f = errno;
    fprintf(stderr,"Could not mmap() opened query file.  Terminating. (errno:%d -> %s)\n",
            f,strerror(f));
    exit(1);
  }

  // Query file is mapped; setup pointers to iterate through the sequences
  nfo.cquery = nfo.queries;
  nfo.equery = nfo.queries+statbf.st_size;
}


////////////////////////////////////////////////////////////////////////////////
//                  Application Entry / High Level Code                       //
////////////////////////////////////////////////////////////////////////////////


#define ZCHUNK 16384

// Writes "sz" bytes from "source" to the stream "dest", compressing
// the data first with zlib compression strength "level".
//
// Special thanks to Mark Adler for providing the non-copyrighted
// public domain example program "zpipe.c", from which this function
// is based (Version 1.4  December 11th, 2005).
static void ZCompressWrite(void *source, int sz, FILE *dest, int level)
{
  z_stream       strm;
  unsigned char  in[ZCHUNK],out[ZCHUNK];
  int            blk,tw=sz;
  int            rv,flush;
  unsigned       ndata;
  long           lenpos,len;
  
  // Init zlib state
  strm.zalloc = Z_NULL;
  strm.zfree  = Z_NULL;
  strm.opaque = Z_NULL;
  rv = deflateInit(&strm, level);
  if( rv != Z_OK ) {
    fprintf(stderr,"Failed to init zlib.  Terminating.\n");
    exit(1);
  }

  // Write a placeholder long that will eventually hold the 
  // compressed block size.
  if( (lenpos=ftell(dest)) < 0 ) {
    fprintf(stderr,"Failed to find block size pos.  Terminating.\n");
    exit(1);
  }
  len = 0;
  if( fwrite(&len,sizeof(len),1,dest) != 1 ) {
    fprintf(stderr,"Failed to stub block size.  Terminating.\n");
    exit(1);
  }

  // Compress while there is still data to write
  do {
    // Setup input
    blk = ((tw < ZCHUNK)?(tw):(ZCHUNK));
    memcpy(in, source+(sz-tw), blk);
    strm.avail_in = blk;
    strm.next_in  = in;
    flush = ((!(tw-blk))?(Z_FINISH):(Z_NO_FLUSH));
    do {
      // Setup output and compress
      strm.avail_out = ZCHUNK;
      strm.next_out  = out;
      rv = deflate(&strm, flush);
      if( rv == Z_STREAM_ERROR ) {
        fprintf(stderr,"Failed to compress output block.  Terminating.\n");
        exit(1);
      }
      // Write compressed data to destination
      ndata = ZCHUNK - strm.avail_out;
      if( (fwrite(out, 1, ndata, dest) != ndata) || ferror(dest) ) {
        deflateEnd(&strm);
        fprintf(stderr,"Failed to compress output block.  Terminating.\n");
        exit(1);
      }
      len += ndata;
    } while( strm.avail_out == 0 );
    // Sanity check
    if( strm.avail_in != 0 ) {
      fprintf(stderr,"Did not fully compress block.  Terminating.\n");
      exit(1);
    }
    // Update "to write" count
    tw -= blk;
  } while( flush != Z_FINISH );

  // Another sanity check
  if( rv != Z_STREAM_END ) {
    fprintf(stderr,"Didn't finish compression properly.  Terminating.\n");	   
    exit(1);
  }

  // Cleanup
  deflateEnd(&strm);

  // Now that the compressed data is written, write the
  // size of the compressed block at the front of the block
  if( fseek(dest,lenpos,SEEK_SET) ) {
    fprintf(stderr,"Could not seek to len pos.  Terminating.\n");
    exit(1);
  }
  if( fwrite(&len,sizeof(len),1,dest) != 1 ) {
    fprintf(stderr,"Failed to write len.  Terminating.\n");
    exit(1);
  }
  //fprintf(stderr,"insz:\t%d\ncmpzsz:\t %ld\n",sz,len);
  if( fseek(dest,0,SEEK_END) ) {
    fprintf(stderr,"Could not seek to end.  Terminating.\n");
    exit(1);
  }

}


void print_usage(FILE *f)
{
  fprintf(f, "usage: compressfasta <fasta_file> <block_size> <output_file>\n");
}


void print_help()
{
  print_usage(stdout);
  printf("LALALA\n");
}


int main(int argc, char **argv)
{
  char   *seq;
  int     i, bsz;
  size_t  sz;
  FILE   *f;

  
  // Clear the info struct
  memset(&nfo,0,sizeof(info_t));
  
  // Check command line args
  if( argc == 2 && strcmp(argv[1], "--help") == 0) {
    print_help();
    exit(0);
  }
  if( argc != 4 ) {
    print_usage(stderr);
    exit(1);
  }

  // Get split count
  if( sscanf(argv[2],"%d",&bsz) != 1 ) {
    fprintf(stderr,"compressfasta: %s: invalid block size parameter\n",argv[2]);
    exit(1);
  }
  
  // Get access to the query sequence file so that we can
  // iterate through the list
  Init_Sequences(argv[1]);

  // Open output file
  if( !(f=fopen(argv[3],"w")) ) {
    fprintf(stderr,"Could not open output file for writing. Terminating.\n");
    exit(1);
  }

  // Get a block of sequences to put in output file
  for(i=0,seq=NULL; !i || seq; i++) {
    // Get a block
    for(sz=0; (sz < bsz) && (seq=Get_Sequence()); sz+=nfo.sequencels[nfo.nsequences-1]) {
      nfo.nsequences++;
      if( !(nfo.sequences = realloc(nfo.sequences,nfo.nsequences*sizeof(char*))) ) {
        fprintf(stderr,"Could not grow sequence pointer array. Terminating.\n");
        exit(1);
      }
      if( !(nfo.sequencels = realloc(nfo.sequencels,nfo.nsequences*sizeof(size_t))) ) {
        fprintf(stderr,"Could not grow sequence pointer array. Terminating.\n");
        exit(1);
      }
      nfo.sequences[nfo.nsequences-1]  = seq;
      nfo.sequencels[nfo.nsequences-1] = Sequence_Length(seq);
    }
    
    // Write the block to the file:
    //
    // I happen to know that "seq=Get_Sequence()" will always be contiguous
    // non-terminated sequences when called in succsession like above.
    if( sz ) {
      ZCompressWrite(nfo.sequences[0], sz, f, Z_DEFAULT_COMPRESSION);
    }

    // Get ready for the next block
    nfo.nsequences = 0;
    free(nfo.sequences);
    free(nfo.sequencels);
    nfo.sequences = NULL;
    nfo.sequencels = NULL;
  }

  // Return success
  fclose(f);
  return 0;
}
