#ifndef HSPWRAP_H__
#define HSPWRAP_H__

#include <stdint.h>
#include <mpi.h>
#include "../../hsp-config.h"

// Configuration
//#define NUM_PROCS 12
//#define BUFFER_SIZE (4L<<20)

// Common utils
#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ZERO_ARRAY(a) memset(a, 0, sizeof(a))

// Tag for master messages
#define TAG_WORKUNIT  0
#define TAG_DATA      1
#define TAG_MASTERCMD 2
// Tag for slave messages
#define TAG_REQUEST 3

//#define TRACE 1
#define INFO 1

#ifdef TRACE
#define trace(...) fprintf(stderr, __VA_ARGS__)
#else
#define trace(...) 
#endif

#ifdef INFO
#define info(...) fprintf(stderr, __VA_ARGS__)
#else
#define info(fmt, ...) 
#endif

typedef uint32_t blockid_t;
typedef uint16_t blockcnt_t;

// Types of work units (from master to slave)
enum workunit_type {
  WU_TYPE_EXIT,
  WU_TYPE_DATA
};

// Types of requests (from slave to master)
enum request_type {
  REQ_WORKUNIT,
  REQ_ABORT
};

// Work unit message
struct workunit {
  enum workunit_type type;
  uint32_t           count;
  uint32_t           len;
  blockid_t          blk_id;
};

// Request message
struct request {
  enum request_type  type;
  uint32_t           count;
};

char *iter_next (char *s, char *e, char *i);
int   chunked_bcast (void *buffer, size_t count, int root, MPI_Comm comm);

#endif // HSPWRAP_H__
