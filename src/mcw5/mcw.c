#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>
#include <signal.h>
#include <zlib.h>

#include "tscq.h"
#include "mcw.h"



////////////////////////////////////////////////////////////////////////////////
//                               Global State                                 //
////////////////////////////////////////////////////////////////////////////////


// CLI/env arg info
args_t args; 


// Master info struct
masterinfo_t MasterInfo;


// Slave info struct
slaveinfo_t SlaveInfo;


// Result buffer information
resultbuff_t *resultbuff;
pthread_t     resultbuff_thread;
volatile int  result_thread_error=0;


// Information about the DB files (static global input files)
// file_sizes[0] is the exe file
filesizes_t *file_sizes;
int          file_sizes_fd;
void        *shm_exe;
long         shm_exe_sz;


// MPI rank
volatile int Rank;


// Use by both master and slave for timing
float init_time;


// !!av: Gating hack
char *Ucbuff, *Cbuff;
int   Nc,Fd;
int   ConcurrentWriters = 512;

////////////////////////////////////////////////////////////////////////////////
//                                 Util Code                                  //
////////////////////////////////////////////////////////////////////////////////


static void Vprint(int sev, char *fmt, ...)
{
  va_list args;
  char    buf[4096];

  if( VERBOSITY >= sev ) {
    buf[0] = '\0';
#if DBGTIME
    sprintf(buf,"[%lu] ",((unsigned long)time(NULL)));
#endif
    va_start(args, fmt);
    vsnprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), fmt, args);
    va_end(args);
    fprintf(stderr,"%s",buf);
  }

}


static void Report_Timings(int rank, 
			   float i, float o, float ic, float oc, float vi, float vo, 
			   unsigned long b_i, unsigned long b_o, unsigned long b_id, unsigned long b_od)
{
  float t[10];
  float v[10];

  memset(t,0,sizeof(t));

  if( !rank ) {
    // Root uses an empty input (t) and fills (v)
    MPI_Reduce(t, v, 10, MPI_FLOAT, MPI_SUM, 0, MPI_COMM_WORLD);
    Vprint(SEV_NRML,"Real input time:           %.3f  (per thread: %.3f).\n",
	   v[0],(v[0]/(MasterInfo.nslaves*NCORES)));
    Vprint(SEV_NRML,"Real output time:          %.3f  (per thread: %.3f).\n",
	   v[1],(v[1]/(MasterInfo.nslaves*NCORES)));
    Vprint(SEV_NRML,"Input decompression time:  %.3f  (per thread: %.3f).\n",
	   v[2],(v[2]/(MasterInfo.nslaves*NCORES)));
    Vprint(SEV_NRML,"Output compression time:   %.3f  (per thread: %.3f).\n",
 	   v[3],(v[3]/(MasterInfo.nslaves*NCORES)));
    Vprint(SEV_NRML,"Virtual input time:        %.3f  (per thread: %.3f).\n",
	   v[4],(v[4]/(MasterInfo.nslaves*NCORES)));
    Vprint(SEV_NRML,"Virtual output time:       %.3f  (per thread: %.3f).\n",
	   v[5],(v[5]/(MasterInfo.nslaves*NCORES)));
    Vprint(SEV_NRML,"Compressed input:          %.3f  (per thread: %.3f).\n",
	   v[6],(v[6]/(MasterInfo.nslaves*NCORES)));
    Vprint(SEV_NRML,"Decompressed input:        %.3f  (per thread: %.3f).\n",
	   v[7],(v[7]/(MasterInfo.nslaves*NCORES)));
    Vprint(SEV_NRML,"Uncompressed output:       %.3f  (per thread: %.3f).\n",
	   v[8],(v[8]/(MasterInfo.nslaves*NCORES)));
    Vprint(SEV_NRML,"Compressed output:         %.3f  (per thread: %.3f).\n\n",
	   v[9],(v[9]/(MasterInfo.nslaves*NCORES)));
    fflush(stderr);
    fflush(stdout);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
  } else {
    // Slaves fill in values
    v[0] = i;
    v[1] = o;
    v[2] = ic;
    v[3] = oc;
    v[4] = vi;
    v[5] = vo;
    v[6] = b_i;
    v[7] = b_id;
    v[8] = b_od;
    v[9] = b_o;
    MPI_Reduce(v, t, 10, MPI_FLOAT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    // Slaves report their timings as well
#if 0
    Vprint(SEV_NRML,"{%d} Real input time:           %.3f\n", rank, v[0]);
    Vprint(SEV_NRML,"{%d} Real output time:          %.3f\n", rank, v[1]);
    Vprint(SEV_NRML,"{%d} Input decompression time:  %.3f\n", rank, v[2]);
    Vprint(SEV_NRML,"{%d} Output compression time:   %.3f\n", rank, v[3]);
    Vprint(SEV_NRML,"{%d} Virtual input time:        %.3f\n", rank, v[4]);
    Vprint(SEV_NRML,"{%d} Virtual output time:       %.3f\n", rank, v[5]);
    Vprint(SEV_NRML,"{%d} Compressed input:          %.3f\n", rank, v[6]);
    Vprint(SEV_NRML,"{%d} Decompressed input:        %.3f\n", rank, v[7]);
    Vprint(SEV_NRML,"{%d} Uncompressed output:       %.3f\n", rank, v[8]);
    Vprint(SEV_NRML,"{%d} Compressed output:         %.3f\n", rank, v[9]);
#endif
    printf("%d  %.4f  %.4f  %.4f  %.4f  %.4f  %.4f  %.4f  %.4f  %.4f  %.4f\n",
	   rank,
	   v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9]);
    MPI_Barrier(MPI_COMM_WORLD);
  }
}


////////////////////////////////////////////////////////////////////////////////
//                                 Exit Code                                  //
////////////////////////////////////////////////////////////////////////////////


static void cleanup();


static void Abort(int arg)
{
  // Make sure our shared memory segments are removed
  cleanup();
  
  // Abort the MPI job
  MPI_Abort(MPI_COMM_WORLD,arg);
}


////////////////////////////////////////////////////////////////////////////////
//                              SHM Wrapper Code                              //
////////////////////////////////////////////////////////////////////////////////


static void* Create_SHM(long shmsz, int *fd)
{
  void *shm;
  int   shmfd;

  // Create the shared memory segment, and then mark for removal.
  // As soon as all attachments are gone, the segment will be
  // destroyed by the OS.
  shmfd = shmget(IPC_PRIVATE, shmsz, IPC_CREAT | IPC_EXCL | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
  if( shmfd < 0 ) {
    Vprint(SEV_ERROR,"Failed to make SHM (%d).  Terminating.\n",errno);
    Abort(1);
  }
  shm = shmat(shmfd, NULL, 0);
  shmctl(shmfd, IPC_RMID, NULL);
  if( shm == ((void*)-1) ) {
    Vprint(SEV_ERROR,"Failed to attach SHM. Terminating.\n");
    Abort(1);
  }
  
  // Return the created SHM
  if( fd ) {
    *fd = shmfd;
  }
  return shm;
}


static void* Create_DBSHM(char *name, long shmsz)
{
  int    fd;
  void  *shm;

  // Create the shared memory segment
  shm = Create_SHM(shmsz,&fd);  

  // Save some info about the SHM
  if( file_sizes->nfiles < MAX_DB_FILES ) {
    file_sizes->fs[file_sizes->nfiles].shm     = shm;
    file_sizes->fs[file_sizes->nfiles].shmsize = shmsz;
    file_sizes->fs[file_sizes->nfiles].size    = shmsz;
    file_sizes->fs[file_sizes->nfiles].fd      = fd;
    sprintf(file_sizes->fs[file_sizes->nfiles].name,"%s",name);
    file_sizes->nfiles++;
  } else {
    Vprint(SEV_ERROR,"Too many DB files; increase MAX_DB_FILES. Terminating.\n");
    Abort(1);
  }

  // Return the newly created SHM
  return shm;
}


////////////////////////////////////////////////////////////////////////////////
//                               Master Code                                  //
////////////////////////////////////////////////////////////////////////////////


// Returns a pointer to the start of the next sequence
static seq_data_t Get_SequenceBlocks(int *size, int *blocks)
{
  static int qndx=0,f1=0,f2=0;
  double     pd;
  int        i,sz;

  // Make sure the current (next to be returned) is in bounds
  if( qndx >= MasterInfo.nqueries ) {
    // No more queries, return NULL
    (*size)   = 0;
    (*blocks) = 0;
    return NULL;
  }

  // See if there needs to be an adjustment to blocks returned
  pd = ((double)qndx)/(MasterInfo.nqueries-1);
  if( pd < 0.90 ) {
    // 0 - 90
    if( args.wum ) {
      (*blocks) *= args.wum;
    } 
  } else if( pd < 0.95 ) {
    // 90 - 95
    if( args.wum/2 ) {
      (*blocks) *= (args.wum/2);
      if( !f1 ) {
	f1 = 1;
	Vprint(SEV_NRML,"Master switching to smaller WUM: %d\n\n",(args.wum/2));
      }
    }
    if( !f1 ) {
      f1 = 1;
      Vprint(SEV_NRML,"Master switching to smaller WUM: 1\n\n");
    }
  } else {
    // 95 - 100
    if( !f2 ) {
      f2 = 1;
      Vprint(SEV_NRML,"Master switching to smaller WUM: 1\n\n",1);
    }
  }

  // "Aquire" the requested blocks
  for(i=sz=0; (i < (*blocks)) && (qndx < MasterInfo.nqueries); i++,qndx++) {
    sz += MasterInfo.queries[qndx]->len+sizeof(MasterInfo.queries[qndx]->len);
  }

  // Return the blocks and metadata
  (*blocks) = i;
  (*size)   = sz;
  return (seq_data_t)MasterInfo.queries[qndx-i];
}


// Opens and maps the input query file
static void Init_Queries(char *fn)
{
  struct stat    statbf;
  compressedb_t *cb;
  int            f;
  

  // Open and memory map the input sequence file
  if( (f = open(fn,O_RDONLY)) < 0 ) {
    Vprint(SEV_ERROR,"Master could not open() query file. Terminating.\n");
    Abort(1);
  }
  if( fstat(f, &statbf) < 0 ) {
    close(f);
    Vprint(SEV_ERROR,"Master could not fstat() opened query file. Terminating.\n");
    Abort(1);
  }
  MasterInfo.qmap = mmap(NULL,statbf.st_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,f,0);
  close(f);
  if( MasterInfo.qmap == MAP_FAILED ) {
    Vprint(SEV_ERROR,"Master could not mmap() opened query file. Terminating.\n");
    Abort(1);
  }

  // Build array of pointers to block structs from raw map
  MasterInfo.nqueries = 0;
  MasterInfo.queries  = NULL;
  for(cb=(compressedb_t*)MasterInfo.qmap; cb<(compressedb_t*)(MasterInfo.qmap+statbf.st_size); cb=((void*)cb)+cb->len+sizeof(cb->len)) {
    // Start of new compressed block
    MasterInfo.nqueries++;
    if( !(MasterInfo.queries=realloc(MasterInfo.queries,MasterInfo.nqueries*sizeof(compressedb_t*))) ) {
      Vprint(SEV_ERROR,"Master failed to realloc() compressed input block array. Terminating.\n");
      Abort(1);
    }
    MasterInfo.queries[MasterInfo.nqueries-1] = cb;
  }

  // Compute a reasonable size for a query block
  Vprint(SEV_NRML,"Compressed input block count: %d\n\n",MasterInfo.nqueries);
}


////////////////////////////////////////////////////////////////////////////////


static void Init_Master()
{
  int i;

  // Break query file into work units
  Init_Queries(args.queryf);

  // Allocate space for master to keep track of slaves
  if( !(MasterInfo.slaves=malloc(MasterInfo.nslaves*sizeof(slave_t))) ) {
    Vprint(SEV_ERROR,"Master failed to allocate slave array. Terminating.\n");
    Abort(1);
  }
  // Allocate space to hold irecv request fields 
  if( !(MasterInfo.mpi_req=malloc(MasterInfo.nslaves*3*sizeof(MPI_Request))) ) {
    Vprint(SEV_ERROR,"Master failed to allocate slave MPI request array. Terminating.\n");
    Abort(1);
  }

  // Init the array
  memset(MasterInfo.slaves,0,MasterInfo.nslaves*sizeof(slave_t));
  for(i=0; i<MasterInfo.nslaves; i++) {
    MasterInfo.slaves[i].rank = i+1;
  }
}


static void Master(int processes, int rank)
{
  masterinfo_t *mi=&MasterInfo; // Just to shorten
  seq_data_t    sequence;
  int           nseq,seqsz,blkseqs,index,i,fp;
  double        pd,lpd;
  long          st;

  // Record init start time
  st = time(NULL);

  // Initialize the master process
  mi->rank    = rank;
  mi->nprocs  = processes;
  mi->nslaves = processes-1;
  Vprint(SEV_NRML,"Slaves:   %d\t(processes)\n",mi->nslaves);
  Vprint(SEV_NRML,"Workers:  %d\t(threads)\n\n",mi->nslaves*NCORES);
  Init_Master(&mi);

  // Wait for all the ranks to fully init
  MPI_Barrier(MPI_COMM_WORLD);

  // Record the init end time
  init_time += time(NULL)-st;
  Vprint(SEV_NRML,"Finished with init in %f seconds.\n\n",init_time);

  // Post a receive work request from each slave
  for(i=0; i<mi->nslaves; i++) {
    MPI_Irecv(&(mi->slaves[i].request), sizeof(request_t), MPI_BYTE, mi->slaves[i].rank, 
              TAG_REQUEST, MPI_COMM_WORLD, &(mi->mpi_req[i]) );
  }

  // Hand out work units to requestors while there is work
  fp = 0;
  lpd = pd = 0;
  for(nseq=0; nseq < mi->nqueries; nseq+=blkseqs) {
    // Wait until a slave requests a work unit
    MPI_Waitany(mi->nslaves, mi->mpi_req, &index, MPI_STATUSES_IGNORE);
    // Wait for any previous work unit sends to this slave to complete
    if( mi->slaves[index].sflg ) {
      // These should return immediately.  This slave should not
      // request a new work unit until the send of the old one
      // (and work on it) completed.
      MPI_Wait(&(mi->mpi_req[mi->nslaves+index]),   MPI_STATUS_IGNORE);
      MPI_Wait(&(mi->mpi_req[mi->nslaves*2+index]), MPI_STATUS_IGNORE);
      mi->slaves[index].sflg = 0;
    }
    // We found a slave ready for work.  What is he requesting?
    blkseqs = 0;
    switch( mi->slaves[index].request.type ) {
    case RQ_WU:
      // Slave wants a work unit; send it to him
      blkseqs = mi->slaves[index].request.count;
      sequence = Get_SequenceBlocks(&seqsz,&blkseqs);
      mi->slaves[index].workunit.type = WU_TYPE_SEQS;
      mi->slaves[index].workunit.len  = seqsz;
      MPI_Isend(&(mi->slaves[index].workunit), sizeof(workunit_t), MPI_BYTE, 
		mi->slaves[index].rank, TAG_WORKUNIT, MPI_COMM_WORLD,
		&(mi->mpi_req[mi->nslaves+index]));
      // Now send the sequence data
      mi->slaves[index].sequence = sequence;
      MPI_Isend(mi->slaves[index].sequence, mi->slaves[index].workunit.len, MPI_BYTE,
		mi->slaves[index].rank, TAG_SEQDATA, MPI_COMM_WORLD,
		&(mi->mpi_req[mi->nslaves*2+index]));
      // Record that a send is in flight to this slave
      mi->slaves[index].sflg = 1;
      // And post a receive for another work unit request from this slave
      MPI_Irecv(&(mi->slaves[index].request), sizeof(request_t), MPI_BYTE,
		mi->slaves[index].rank, TAG_REQUEST, MPI_COMM_WORLD, 
		&(mi->mpi_req[index]));
      break;
    default:
      // Unknown request type
      Vprint(SEV_ERROR,"Master: Slave %d made unknown request type %d.  Terminating.\n",
	      index,mi->slaves[index].request.type);
      Abort(1);
    }
    // Progress update print
    pd = (((double)nseq) / mi->nqueries) * 100.0;
    if( !fp || ((pd-lpd) >= 1.0) ) {
      // We made at least 1% progress since last update or is first
      fp = 1;
      Vprint(SEV_NRML,"Dispached workunits: %3.0lf%c\n\n",pd,'%');
      lpd = pd;
    }
  }

  // Be a little verbose
  Vprint(SEV_NRML,"Master is done handing out work units.\n");
  Vprint(SEV_NRML,"Master responding WU_TYPE_EXIT to requests.\n\n");

  // We are out of work units.
  MPI_Waitall(mi->nslaves, mi->mpi_req, MPI_STATUSES_IGNORE);
  // Be sure all WU sends to slaves are done; this should return immediately,
  // as slaves should not be ready for new WUs unless the sends of the old
  // work units completed.
  for(i=0; i<mi->nslaves; i++) {
    if( mi->slaves[i].sflg ) {
      MPI_Wait(&(mi->mpi_req[mi->nslaves+i]),   MPI_STATUS_IGNORE);
      MPI_Wait(&(mi->mpi_req[mi->nslaves*2+i]), MPI_STATUS_IGNORE);
      mi->slaves[index].sflg = 0;
    }
  }

  // Tell slaves to exit
  for(i=0; i<mi->nslaves; i++) {
    mi->slaves[i].workunit.type = WU_TYPE_EXIT;
    mi->slaves[i].workunit.len  = 0;
    MPI_Isend(&(mi->slaves[i].workunit), sizeof(workunit_t), MPI_BYTE, 
              mi->slaves[i].rank, TAG_WORKUNIT, MPI_COMM_WORLD,
              &(mi->mpi_req[index]));
  }
  // Wait for all EXIT flag sends to slaves to complete
  MPI_Waitall(mi->nslaves, mi->mpi_req, MPI_STATUSES_IGNORE);

  // !!av: Gating hack
  i = mi->nprocs / ConcurrentWriters;
  if( mi->nprocs%ConcurrentWriters ) {
    i++;
  }
  while( i-- ) {
    MPI_Barrier(MPI_COMM_WORLD);
  }

  // Wait for everyone to report that they are exiting
  MPI_Barrier(MPI_COMM_WORLD);

  // Report times (yay! this call is all zeros!)
  Report_Timings(0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0);

  // Report that shutdown (pretty much all, but I still call it
  // "initial") is complete.
  Vprint(SEV_NRML,"Master reports initial shutdown complete.\n\n");

}


////////////////////////////////////////////////////////////////////////////////
//                               Worker Code                                  //
////////////////////////////////////////////////////////////////////////////////


static void Worker_WriteResults(char *data, int n, int wid)
{
  // Skip if no data
  if( !n ) {
    return;
  }
  
  // Lock and wait for "not full" status
  Vprint(SEV_DEBUG,"Slave %d Worker %d about to write results to buffer.\n",
	 SlaveInfo.rank,wid);
  pthread_mutex_lock(&(SlaveInfo.resultb_lock));
  while( (RESULTBUFF_GBL_SIZE-resultbuff->count) < RESULTBUFF_SIZE ) {
    Vprint(SEV_DEBUG,"Slave %d Worker %d waiting for room to write results to buffer.\n",
	   SlaveInfo.rank,wid);
    pthread_cond_wait(&(SlaveInfo.resultb_nfull), &(SlaveInfo.resultb_lock));
  }

  // Write into result shm
  memcpy((void*)(resultbuff->buff+resultbuff->count), data, n);
  resultbuff->count += n;

  // Signal that the buffer is not empty
  pthread_cond_signal(&(SlaveInfo.resultb_nempty));

  // Unlock
  pthread_mutex_unlock(&(SlaveInfo.resultb_lock));
  Vprint(SEV_DEBUG,"Slave %d Worker %d done writing to result buffer.\n",
	 SlaveInfo.rank,wid);
}


static float Worker_ChildIO(int rank, int pid, int wid, int rndx)
{
  struct timeval  st,et;
  int             status,w;
  float           io_time=0.0f;


  // Wait for the child to finish
  if( (w=waitpid(pid,&status,0)) < 0 ) {
    // Wait failed for some reason
    // There was an error with the child DB process
    Vprint(SEV_ERROR,"Worker's wait on child failed.  Terminating.\n");
    Abort(1);
  }

  // Check child exit status
  if( w ) {
    if( WIFEXITED(status) && !WEXITSTATUS(status) ) {
      // The slave's child seemes to have finished correctly
      Vprint(SEV_DEBUG,"Slave %d Worker %d Child exited normally.\n",
	     SlaveInfo.rank,wid);
    } else {
      // There was an error with the child DB process
      Vprint(SEV_ERROR,"Worker's child exited abnormally: %d.\n",WEXITSTATUS(status));
      if( WIFSIGNALED(status) ) {
	Vprint(SEV_ERROR,"Worker's child killed by signal: %d.\n",WTERMSIG(status));
      }
    } 
  }

  // The child process is gone:  Write any results to our node's buffer.
  gettimeofday(&st, NULL);
  Worker_WriteResults(file_sizes->fs[rndx].shm, file_sizes->fs[rndx].size, wid);
  gettimeofday(&et, NULL);
  io_time += ((et.tv_sec*1000000+et.tv_usec) - 
	      (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;

  // Return the total IO output time
  return io_time;
}


static void Worker_FreeArgv(char **argv)
{
  char **p;

  // Free all args, then free array of args.
  for(p=argv; *p; p++) {
    free(*p);
  }
  free(argv);
}

static char **Worker_BuildArgv(int rank, int procs, int wid)
{
  char **argv,*m,*w,buf[1024],*saveptr=NULL;
  int    na,node,nodes,loadstride;


  // These will be needed later
  node       = rank;
  nodes      = procs;
  loadstride = nodes/args.ndbs;


  // Put program name at the start of argv list
  if( !(argv=malloc(sizeof(char*))) ) {
    Vprint(SEV_ERROR, "Slave %d Worker %d Failed to create child's argv list.\n",rank,wid);
    Abort(1);
  }
  argv[0] = strdup(args.exe_base);
  na = 1;


  // Now add the args from the env var mode / line
  m = strdup(args.mode);
  w = strtok_r(m, " ", &saveptr);
  while( w ) {
    // Make room in array for another arg
    if( !(argv=realloc(argv,(na+1)*sizeof(char*))) ) {
      Vprint(SEV_ERROR, "Slave %d Worker %d Failed to grow child's argv list.\n",rank,wid);
      Abort(1);
    }
    // Put arg in list
    if( !strcmp(w,":DB:") ) {
      // DB macro
      sprintf(buf,"%s/%s%d/%s",
	      args.db_path,args.db_prefix,node/loadstride,args.db_prefix);
      argv[na] = strdup(buf);
      na++;
    } else if( !strcmp(w,":IN:") ) {
      // Input macro
      sprintf(buf,":STDIN%d",wid*2);
      argv[na] = strdup(buf);
      na++;
    } else if( !strcmp(w,":OUT:") ) {
      // Output macro
      sprintf(buf,":STDOUT%d",wid*2+1);
      argv[na] = strdup(buf);
      na++;
    } else { 
      // Just a regular word
      argv[na] = strdup(w);
      na++;
    }
    // Advance to next arg in list
    w = strtok_r(NULL, " ", &saveptr);
  }
  free(m);


  // Add a null pointer to the end
  if( !(argv=realloc(argv,(na+1)*sizeof(char*))) ) {
    Vprint(SEV_ERROR, "Slave %d Worker %d Failed to grow child's argv list.\n",rank,wid);
    Abort(1);
  }
  argv[na] = NULL;

  // Debug print the command line
  /*
  {
    char **p;
    
    buf[0] = '\0';
    for(p=argv; *p; p++) {
      strcat(buf,*p);
      strcat(buf," ");
    }
    Vprint(SEV_DEBUG, "Slave %d Worker %d Built argv for child: \"%s\".\n",
	   rank,wid,buf);
  }
  */

  // Return the created args list
  return argv;
}

/*
static char **Worker_BuildArgvInPlace(char **argv, char *buf,  int rank, int procs, int wid)
{
  char  *b,*m,*w,*saveptr=NULL;
  int    na,node,nodes,loadstride;


  // These will be needed later
  node       = rank;
  nodes      = procs;
  loadstride = nodes/args.ndbs;
  b          = buf;


  // Put program name at the start of argv list
  buf = strcpy(buf, args.exe_base)
  argv[0] = buf;
  na = 1;


  // Now add the args from the env var mode / line
  m = strdup(args.mode);
  w = strtok_r(m, " ", &saveptr);
  while( w ) {
    // Make room in array for another arg
    if( !(argv=realloc(argv,(na+1)*sizeof(char*))) ) {
      Vprint(SEV_ERROR, "Slave %d Worker %d Failed to grow child's argv list.\n",rank,wid);
      Abort(1);
    }
    // Put arg in list
    if( !strcmp(w,":DB:") ) {
      // DB macro
      sprintf(buf,"%s/%s%d/%s",
	      args.db_path,args.db_prefix,node/loadstride,args.db_prefix);
      argv[na] = strdup(buf);
      na++;
    } else if( !strcmp(w,":IN:") ) {
      // Input macro
      sprintf(buf,":STDIN%d",wid*2);
      argv[na] = strdup(buf);
      na++;
    } else if( !strcmp(w,":OUT:") ) {
      // Output macro
      sprintf(buf,":STDOUT%d",wid*2+1);
      argv[na] = strdup(buf);
      na++;
    } else { 
      // Just a regular word
      argv[na] = strdup(w);
      na++;
    }
    // Advance to next arg in list
    w = strtok_r(NULL, " ", &saveptr);
  }
  free(m);


  // Add a null pointer to the end
  if( !(argv=realloc(argv,(na+1)*sizeof(char*))) ) {
    Vprint(SEV_ERROR, "Slave %d Worker %d Failed to grow child's argv list.\n",rank,wid);
    Abort(1);
  }
  argv[na] = NULL;

  // Return the created args list
  return argv;
}
*/

static void Worker_Child_MapFDs(int rank, int wid)
{
  static int  devnull=-1;
  char       *m,*w,*saveptr=NULL;
  int         from,to;


  // Get access to /dev/null if needed
  if( devnull == -1 ) {
    if( (devnull=open("/dev/null", O_RDWR)) < 0 ) {
      fprintf(stderr,"Slave %d Worker %d's Child failed to open(\"/dev/null\").\n",rank,wid);
      exit(1);
    }
  }

  // Now apply the mappings
  m = strdup(args.dup2);
  w = strtok_r(m, ":", &saveptr);
  while( w ) {
    // Parse this one mapping
    if( sscanf(w,"%d,%d",&from,&to) != 2 ) {
      Vprint(SEV_ERROR, "Slave %d Worker %d's Child failed to parse fd mapping work.\n",rank,wid);
      exit(1);
    }
    if( from == -1 ) { from = devnull; }
    if( to   == -1 ) { to   = devnull; }
    // Perform the mapping
    if( dup2(to, from) < 0 ) {
      fprintf(stderr,"Slave %d Worker %d's Child failed to dup2(%d,%d).\n",rank,wid,to,from);
      exit(1);
    }
    // Advance to next mapping in list
    w = strtok_r(NULL, " ", &saveptr);
  }
  free(m);
}


static float Worker_SearchDB(int rank, int procs, int wid, int rndx)
{
  char **argv;
  char  name[256],exe_name[256];
  int   pid,node,nodes,loadstride;
  float io_time=0.0f;

  char s_argbuf[4096];
  char *s_argv[32];


  // These will be needed later
  node       = rank;
  nodes      = procs;
  loadstride = nodes/args.ndbs;

  // Parse the line to build argv[][] for child
  argv = Worker_BuildArgv(rank,procs,wid);
  char *argi = s_argbuf;
  int i;
  for (i=0; argv[i] && i<32; ++i) {
    char* arg = argv[i];
    int   len = strlen(arg);
    strcpy(argi, arg);
    s_argv[i] = argi;
    argi += len + 1;
  }
  s_argv[i] = NULL;
  // Free the now unneeded argv array
  Worker_FreeArgv(argv);

  // Setup the environment for the child process
  sprintf(name,"%d",file_sizes_fd);
  setenv("MCW_FI_SHM_FD",name,1);
  sprintf(name,"%d",wid);
  setenv("MCW_WID",name,1);
  sprintf(name,"%s/%s%d",
	  args.db_path,args.db_prefix,node/loadstride);
  setenv("BLASTMAT",name,1);
  sprintf(name,"%s/%s%d",
	  args.db_path,args.db_prefix,node/loadstride);
  setenv("BLASTDB",name,1);

  // Build a name for the exe
  sprintf(exe_name,"./%s",args.exe_base);

  
  pthread_mutex_lock(&(SlaveInfo.fork_lock));
  if( (pid=fork()) > 0 ) {
    // Sleep some arbitrary amount of time and pray the child execs
    sleep(4);
    pthread_mutex_unlock(&(SlaveInfo.fork_lock));
    // This is the MPI slave process (parent)
    Vprint(SEV_DEBUG, "Slave %d Worker %d's child's pid: %d.\n",SlaveInfo.rank,wid,pid);
    // Wait for child to finish; handle its IO
    io_time = Worker_ChildIO(rank,pid,wid,rndx);
  } else if( !pid ) {
    // This is the Child process
    //PG Worker_Child_MapFDs(rank,wid);
    // Run the DB search
    if( execv(exe_name,s_argv) < 0 ) {
      Vprint(SEV_ERROR,"Worker's child failed to exec DB.  Terminating.\n");
      exit(1);
    }
    Vprint(SEV_ERROR,"Worker's child failed to exec DB.  Terminating.\n");
    exit(1);
  } else {
    // fork() returned an error code
    Vprint(SEV_ERROR,"Worker failed to start DB search. Terminating.\n");
    Abort(1);
  }

  // Return the time it took to do IO.
  return io_time;
}


#define CHUNK 16384
int zinf_memcpy(unsigned char *dest, compressedb_t *scb, size_t size, size_t *dcsz)
{
  compressedb_t *cb;
  z_stream       strm;
  unsigned char  in[CHUNK],out[CHUNK],*odest=dest;
  unsigned       have;
  long           bsz;
  int            ret;


  for(cb=scb; (void*)cb < ((void*)scb)+size; cb=((void*)cb)+cb->len+sizeof(cb->len)) {
    // Init inflate state
    strm.zalloc   = Z_NULL;
    strm.zfree    = Z_NULL;
    strm.opaque   = Z_NULL;
    strm.next_in  = Z_NULL;
    strm.avail_in = 0;
    ret = inflateInit(&strm);
    if( ret != Z_OK ) {
      return ret;
    }
    
    // Decompress until we have processed bsz bytes
    bsz = cb->len;
    do {
      if( !(strm.avail_in=((bsz>=CHUNK)?(CHUNK):(bsz))) ) {
	inflateEnd(&strm);
	return Z_DATA_ERROR;
      }
      memcpy(in, &(cb->data)+cb->len-bsz, strm.avail_in);
      strm.next_in = in;
      bsz -= strm.avail_in;
      // run inflate() on input until output buffer not full
      do {
	strm.avail_out = CHUNK;
	strm.next_out = out;
	ret = inflate(&strm, Z_NO_FLUSH);
	switch( ret ) {
	case Z_BUF_ERROR:
	case Z_NEED_DICT:
	case Z_DATA_ERROR:
	case Z_MEM_ERROR:
	  inflateEnd(&strm);
	case Z_STREAM_ERROR:
	  return ret;
	}
	have = CHUNK - strm.avail_out;
	memcpy(dest,out,have);
	dest += have;
      } while( strm.avail_out == 0 );
      // Done when inflate() says it's done
    } while( ret != Z_STREAM_END );

    // clean up
    //PG:inflateEnd(&strm);

    // If the stream ended before using all the data
    // in the block, return error.
    if( bsz ) {
      return Z_DATA_ERROR;
    }
  }
  
  // Return success
  (*dcsz) = dest-odest;
  return Z_OK;
}
  

static void GrowSeqData(int new_len)
{
  // Calculate a reasonable new size
  if( new_len > SlaveInfo.seq_data_len ) {
    if( new_len > (SlaveInfo.seq_data_len*2) ) {
      SlaveInfo.seq_data_len = new_len;
    } else {
      SlaveInfo.seq_data_len *= 2;
    }
  }

  // Realloc space for the sequence data
  if( !(SlaveInfo.seq_data = realloc(SlaveInfo.seq_data,SlaveInfo.seq_data_len*sizeof(char))) ) {
    Vprint(SEV_ERROR,"Slave failed to grow sequence array. Terminating.\n");
    Abort(1);
  }
}


static void* Worker(void *arg)
{
  int             wid = (int)((long)arg);
  struct timeval  st,et;
  slaveinfo_t    *si=&SlaveInfo;
  request_t      *request;
  workunit_t     *workunit;
  size_t          dcsz;
  float           t_vi,t_vo,t_ic;
  int             i,q,r,done=0;
  long            ib,idb;
  char            name[256];

  
  // Find the in/out SHMs for this worker
  q = -1;
  sprintf(name,":STDIN%d",wid*2);
  for(i=0; i<file_sizes->nfiles; i++) {
    if( !strcmp(name,file_sizes->fs[i].name) ) {
      q = i;
      break;
    }
  }
  if( q == -1 ) {
    Vprint(SEV_ERROR,"Slave %d Worker %d failed to find q I/O SHM.\n",SlaveInfo.rank,wid);
    Abort(1);
  }
  r = -1;
  sprintf(name,":STDOUT%d",wid*2+1);
  for(i=0; i<file_sizes->nfiles; i++) {
    if( !strcmp(name,file_sizes->fs[i].name) ) {
      r = i;
      break;
    }
  }
  if( r == -1 ) {
    Vprint(SEV_ERROR,"Slave %d Worker %d failed to find r I/O SHM.\n",SlaveInfo.rank,wid);
    Abort(1);
  }


  // Done with init, start processing loop
  while( !done ) {
    // Time the request + receive
    t_vi = t_vo = t_ic = 0.0f;
    ib = idb = 0;
    gettimeofday(&st, NULL);

    // Get a new request entry object
    Vprint(SEV_DEBUG,"Slave %d Worker %d getting request object.\n",SlaveInfo.rank,wid);
    request = tscq_entry_new(si->rq);

    // Fill it in
    memset(request,0,sizeof(request_t));
    request->type    = RQ_WU;
    request->count   = 1;

    // "Send" the request up the command chain
    Vprint(SEV_DEBUG,"Slave %d Worker %d asking for work.\n",SlaveInfo.rank,wid);
    tscq_entry_put(si->rq,request);
    
    // Get the work unit we requested
    Vprint(SEV_DEBUG,"Slave %d Worker %d waiting for work.\n",SlaveInfo.rank,wid);
    workunit = tscq_entry_get(si->wq);
    
    // Find total request + receive time
    gettimeofday(&et, NULL);
    t_vi += ((et.tv_sec*1000000+et.tv_usec) - 
	     (st.tv_sec*1000000+st.tv_usec)) / 1000000.0f;

    // Process the work unit
    switch( workunit->type ) {
    case WU_TYPE_EXIT:
      // Master has told us we are done
      done = 1;
      break;
    case WU_TYPE_SEQF:
      // Now that we have sequence data, search the DB with it
      Vprint(SEV_DEBUG,"Slave %d Worker %d starting search.\n",SlaveInfo.rank,wid);
      // Inflate the zlib compressed block(s) into the SHM
      gettimeofday(&st, NULL);
      if( zinf_memcpy((unsigned char*)file_sizes->fs[q].shm,(compressedb_t*)(workunit->data),(size_t)workunit->len,&dcsz) != Z_OK ) {
	Vprint(SEV_ERROR,"Slave %d Worker %d zlib inflate failed. Terminating.\n",SlaveInfo.rank,wid);
	si->worker_error = wid;
	return NULL;
      }
      gettimeofday(&et, NULL);
      t_ic += ((et.tv_sec*1000000+et.tv_usec) - 
	       (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      ib  = workunit->len;
      idb = dcsz;
      file_sizes->fs[q].size = dcsz;
      file_sizes->fs[r].size = 0;
      t_vo = Worker_SearchDB(si->rank, si->nprocs, wid, r);
      // The producer of the work unit malloced this, so we need to free it.
      //PG:free(workunit->data);
      Vprint(SEV_DEBUG,"Slave %d Worker %d done with search.\n",SlaveInfo.rank,wid);
      break;
    default:
      Vprint(SEV_ERROR,"Worker received unknown work unit type. Terminating.\n");
      si->worker_error = wid;
      return NULL;
    }
    
    // Release the work unit we are done with
    tscq_entry_free(si->wq,workunit);

    // Add time spent into node count
    pthread_mutex_lock(&(SlaveInfo.time_lock));
    SlaveInfo.t_ic += t_ic;
    SlaveInfo.t_vi += t_vi;
    SlaveInfo.t_vo += t_vo;
    SlaveInfo.b_i  += ib;
    SlaveInfo.b_id += idb;
    pthread_mutex_unlock(&(SlaveInfo.time_lock));
  }

  Vprint(SEV_DEBUG,"Slave %d Worker %d done; exiting.\n",SlaveInfo.rank,wid);
  return NULL;
}


void Start_Workers()
{
  pthread_attr_t  attr;
  int             i;

  // Setup queues with which to interact with worker threads
  SlaveInfo.rq = tscq_new(1024*NCORES,sizeof(request_t));
  SlaveInfo.wq = tscq_new(1024*NCORES,sizeof(workunit_t));

  // Setup worker thread properties
  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  // Start worker threads
  for(i=0; i<NCORES; i++) {
    if( pthread_create(&(SlaveInfo.workers[i]), &attr, Worker, (void*)((long)i)) ) { 
      Vprint(SEV_ERROR,"Slave failed to start worker thread. Terminating.\n");
      Abort(1);
    }
  }

}


////////////////////////////////////////////////////////////////////////////////
//                                Slave Code                                  //
////////////////////////////////////////////////////////////////////////////////


#define ZCHUNK 16384

// Writes "sz" bytes from "source" to the stream "dest", compressing
// the data first with zlib compression strength "level".
//
// Special thanks to Mark Adler for providing the non-copyrighted
// public domain example program "zpipe.c", from which this function
// is based (Version 1.4  December 11th, 2005).
static float ZCompressWrite(void *source, int sz, FILE *dest, int level, long *bw)
{
  struct timeval  st,et;
  z_stream        strm;
  unsigned char   in[ZCHUNK],out[ZCHUNK];
  int             blk,tw=sz;
  int             rv,flush;
  unsigned        ndata;
  long            lenpos,len;
  float           ct=0.0;
  
  // Init zlib state
  gettimeofday(&st, NULL);
  strm.zalloc = Z_NULL;
  strm.zfree  = Z_NULL;
  strm.opaque = Z_NULL;
  rv = deflateInit(&strm, level);
  if( rv != Z_OK ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to init zlib.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
  }
  gettimeofday(&et, NULL);
  ct += ((et.tv_sec*1000000+et.tv_usec) -
	 (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
  (*bw) = 0;
  

  // Write a placeholder long that will eventually hold the 
  // compressed block size.
  if( (lenpos=ftell(dest)) < 0 ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to find block size pos.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
  }
  len = 0;
  if( fwrite(&len,sizeof(len),1,dest) != 1 ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to stub block size.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
  }

  // Compress while there is still data to write
  gettimeofday(&st, NULL);
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
	Vprint(SEV_ERROR,"Slave %d's Writer failed to compress output block.  Terminating.\n",
	       SlaveInfo.rank);
	result_thread_error = 1;
	pthread_exit(NULL);
      }
      // Write compressed data to destination
      ndata = ZCHUNK - strm.avail_out;
      gettimeofday(&et, NULL);
      ct += ((et.tv_sec*1000000+et.tv_usec) -
	     (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      if( (fwrite(out, 1, ndata, dest) != ndata) || ferror(dest) ) {
	deflateEnd(&strm);
	Vprint(SEV_ERROR,"Slave %d's Writer failed to fwrite() compressed output block.  Terminating.\n",
	       SlaveInfo.rank);
	result_thread_error = 1;
	pthread_exit(NULL);
      }
      gettimeofday(&st, NULL);
      len += ndata;
    } while( strm.avail_out == 0 );
    // Sanity check
    if( strm.avail_in != 0 ) {
      Vprint(SEV_ERROR,"Slave %d's Writer did not fully compress block.  Terminating.\n",
	     SlaveInfo.rank);
      result_thread_error = 1;
      pthread_exit(NULL);
    }
    // Update "to write" count
    tw -= blk;
  } while( flush != Z_FINISH );

  // Another sanity check
  if( rv != Z_STREAM_END ) {
    Vprint(SEV_ERROR,"Slave %d's Writer didn't finish compression properly.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
  }

  // Cleanup
  deflateEnd(&strm);
  gettimeofday(&et, NULL);
  ct += ((et.tv_sec*1000000+et.tv_usec) -
	 (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;

  // Now that the compressed data is written, write the
  // size of the compressed block at the front of the block
  if( fseek(dest,lenpos,SEEK_SET) ) {
    Vprint(SEV_ERROR,"Slave %d's Writer could not seek to len pos.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
  }
  if( fwrite(&len,sizeof(len),1,dest) != 1 ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to write len.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
  }
  if( fseek(dest,0,SEEK_END) ) {
    Vprint(SEV_ERROR,"Slave %d's Writer could not seek to end.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
  }

  // Now return the time spent on the compression overhead (and byte counts)
  (*bw) = len + sizeof(len);
  return ct;
}


// Compresses "sz" bytes from "source" to "dest", compressing
// the data with zlib compression strength "level".
//
// Special thanks to Mark Adler for providing the non-copyrighted
// public domain example program "zpipe.c", from which this function
// is based (Version 1.4  December 11th, 2005).
static long ZCompress(void *source, int sz, void *dest, int level)
{
  z_stream       strm;
  unsigned char  in[ZCHUNK],out[ZCHUNK];
  int            blk,tw=sz;
  int            rv,flush;
  unsigned       ndata;
  long           len=0;
  
  // Init zlib state
  strm.zalloc = Z_NULL;
  strm.zfree  = Z_NULL;
  strm.opaque = Z_NULL;
  rv = deflateInit(&strm, level);
  if( rv != Z_OK ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to init zlib.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
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
	Vprint(SEV_ERROR,"Slave %d's Writer failed to compress output block.  Terminating.\n",
	       SlaveInfo.rank);
	result_thread_error = 1;
	pthread_exit(NULL);
      }
      // Copy compressed data to destination
      ndata = ZCHUNK - strm.avail_out;
      memcpy(dest+sizeof(len)+len,out,ndata);
      len += ndata;
    } while( strm.avail_out == 0 );
    // Sanity check
    if( strm.avail_in != 0 ) {
      Vprint(SEV_ERROR,"Slave %d's Writer did not fully compress block.  Terminating.\n",
	     SlaveInfo.rank);
      result_thread_error = 1;
      pthread_exit(NULL);
    }
    // Update "to write" count
    tw -= blk;
  } while( flush != Z_FINISH );

  // Another sanity check
  if( rv != Z_STREAM_END ) {
    Vprint(SEV_ERROR,"Slave %d's Writer didn't finish compression properly.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    pthread_exit(NULL);
  }

  // Cleanup
  deflateEnd(&strm);

  // Now that the compressed data is coppied, copy the size of the compressed block to
  // the front of the block.  Then return the total size including the size field.
  return (*((long*)dest) = len) + sizeof(len);
}


static size_t Write(int fd, void *buf, size_t count)
{
  size_t tw,w;

  for(tw=count; tw; tw-=w) {
    w = write(fd, buf+(count-tw), tw);
    if( w == -1 ) {
      // Error writing, return -1
      perror("Write()");
      Vprint(SEV_ERROR,"write(%d,0x%X[0x%X],%lu) failed with %lu bytes written of %lu bytes total.\n",
	     fd,buf,buf+(count-tw),count-tw,count);
      return -1;
    }
  }
  // Success, return count
  return count;
}


void* ResultWriter(void *arg)
{
  struct timeval st,et;
  char           fn[512], *ucbuff, *cbuff;
  long           nuc=0,nc=0,bw,bc;
  float          writet,compt=0.0f;
  int            f;

  
  // Allocate space to hold the "double" of the double-buffer
  if( !(ucbuff=malloc(RESULTBUFF_GBL_SIZE*sizeof(char))) ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to allocate second buffer.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    return NULL;
  }

  // Allocate space to hold the compressed output buffer
  if( !(cbuff=malloc(RESULTBUFF_GBL_SIZE*sizeof(char))) ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to allocate compressed buffer.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    return NULL;
  }

  // Setup/Open output file 
  sprintf(fn,"slave-%d.stdout",SlaveInfo.rank);
  if( !(f=open(fn,O_CREAT|O_EXCL|O_WRONLY)) ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to open result file.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    return NULL;
  }

  // !!av: Gating hack
  Ucbuff = ucbuff;
  Cbuff = cbuff;
  Fd = f;

  // Repeat until our controlling slave is done
  while( !SlaveInfo.done ) {
    // Lock
    pthread_mutex_lock(&(SlaveInfo.resultb_lock));
    // Wait for "not empty" status
    while( !resultbuff->count && !SlaveInfo.done ) {
      Vprint(SEV_DEBUG,"Slave %d's Writer waiting for data.\n",SlaveInfo.rank);
      pthread_cond_wait(&(SlaveInfo.resultb_nempty), &(SlaveInfo.resultb_lock));
    }
    compt = writet = 0.0f;
    bw = bc = 0;

    // If there is not enough room, perform a flush of ucbuff first
    if( RESULTBUFF_GBL_SIZE <= (nuc+resultbuff->count) ) {
      gettimeofday(&st, NULL);
      nc += ZCompress((void*)ucbuff, nuc, ((void*)cbuff)+nc, Z_DEFAULT_COMPRESSION);
      bc += nuc;
      nuc = 0;
      gettimeofday(&et, NULL);
      compt += ((et.tv_sec*1000000+et.tv_usec) -
		(st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
    }
    
    // Copy the data from the global buffer to our local buffer
    memcpy(ucbuff+nuc,(char*)resultbuff->buff,resultbuff->count);
    nuc += resultbuff->count;

    // Now that we have a local copy, record that the global
    // buffer is no longer full and release the lock.
    resultbuff->count = 0;
    pthread_cond_signal(&(SlaveInfo.resultb_nfull));
    pthread_mutex_unlock(&(SlaveInfo.resultb_lock));

    // Flush uncompressed buffer if at least half full
    if( nuc >= (RESULTBUFF_GBL_SIZE/2) ) {
      gettimeofday(&st, NULL);
      nc += ZCompress((void*)ucbuff, nuc, ((void*)cbuff)+nc, Z_DEFAULT_COMPRESSION);
      bc += nuc;
      nuc = 0;
      gettimeofday(&et, NULL);
      compt += ((et.tv_sec*1000000+et.tv_usec) -
		(st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
    }

    // Flush compressed buffer if at least half full
    if( nc >= (RESULTBUFF_GBL_SIZE/2) ) {
      // Perform the actual write to the filesystem
      Vprint(SEV_DEBUG,"Slave %d's Writer writing %ld bytes.\n",SlaveInfo.rank,nc);
      gettimeofday(&st, NULL);
      if( Write(f,(void*)cbuff,nc) != nc ) {
	Vprint(SEV_ERROR,"Slave %d's Writer failed to write to result file.  Terminating.\n",
	       SlaveInfo.rank);
	result_thread_error = 1;
	return NULL;
      }
      bw = nc;
      nc = 0;
      gettimeofday(&et, NULL);
      writet = ((et.tv_sec*1000000+et.tv_usec) -
		(st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
    }

    // Record timings
    pthread_mutex_lock(&(SlaveInfo.time_lock));
    SlaveInfo.t_o  += writet;
    SlaveInfo.t_oc += compt;
    SlaveInfo.b_od += bc;
    SlaveInfo.b_o  += bw;
    pthread_mutex_unlock(&(SlaveInfo.time_lock));      
  }

  // Our parent slave is done; flush and exit
  writet = compt = 0.0f;
  bc = bw = 0;
  if( nuc ) {
    gettimeofday(&st, NULL);
    nc += ZCompress((void*)ucbuff, nuc, ((void*)cbuff)+nc, Z_DEFAULT_COMPRESSION);
    bc = nuc;
    nuc = 0;
    gettimeofday(&et, NULL);
    compt = ((et.tv_sec*1000000+et.tv_usec) -
	     (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
  }
  if( nc ) {
    Vprint(SEV_DEBUG,"Slave %d's Writer writing %ld bytes.\n",SlaveInfo.rank,nc);
    gettimeofday(&st, NULL);
    // !!av: Gating hack
    Nc = nc;
    //if( Write(f,(void*)cbuff,nc) != nc ) {
    //  Vprint(SEV_ERROR,"Slave %d's Writer failed to write to result file.  Terminating.\n",
    //         SlaveInfo.rank);
    //  result_thread_error = 1;
    //  return NULL;
    //}
    bw = nc;
    nc = 0;
    gettimeofday(&et, NULL);
    writet = ((et.tv_sec*1000000+et.tv_usec) -
	      (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
  }
  // !!av: gating hack
  // close(f);

  // Record timings
  pthread_mutex_lock(&(SlaveInfo.time_lock));
  SlaveInfo.t_o  += writet;
  SlaveInfo.t_oc += compt;
  SlaveInfo.b_od += bc;
  SlaveInfo.b_o  += bw;
  pthread_mutex_unlock(&(SlaveInfo.time_lock));      

  // Exit as lock should not be held here; return value is ignored
  Vprint(SEV_DEBUG,"Slave %d's Writer done; exiting.\n",SlaveInfo.rank);
  return NULL;
}


/*
void* ResultWriter(void *arg)
{
  struct timeval  st,et;
  FILE           *f;
  char            fn[256], *buff;
  long            count,bw;
  float           writet,compt=0.0f;
  

  
  // Allocate space to hold the "double" of the double-buffer
  if( !(buff=malloc(RESULTBUFF_GBL_SIZE*sizeof(char))) ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to allocate second buffer.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    return NULL;
  }

  // Setup/Open output file 
  sprintf(fn,"slave-%d.stdout",SlaveInfo.rank);
  if( !(f=fopen(fn,"w")) ) {
    Vprint(SEV_ERROR,"Slave %d's Writer failed to open result file.  Terminating.\n",
	   SlaveInfo.rank);
    result_thread_error = 1;
    return NULL;
  }

  // Repeat until our controlling slave is done
  while( !SlaveInfo.done ) {
    // Lock
    pthread_mutex_lock(&(SlaveInfo.resultb_lock));
    // Wait for "not empty" status
    while( !resultbuff->count && !SlaveInfo.done ) {
      Vprint(SEV_DEBUG,"Slave %d's Writer waiting for data.\n",SlaveInfo.rank);
      pthread_cond_wait(&(SlaveInfo.resultb_nempty), &(SlaveInfo.resultb_lock));
    }
    // Copy the data from the global buffer to our local buffer
    memcpy(buff,(char*)resultbuff->buff,resultbuff->count);
    count = resultbuff->count;
    // Now that we have a local copy, record that the global
    // buffer is no longer full and release the lock.
    resultbuff->count = 0;
    pthread_cond_signal(&(SlaveInfo.resultb_nfull));
    pthread_mutex_unlock(&(SlaveInfo.resultb_lock));
    // Write data to the output file
    if( count ) {
      Vprint(SEV_DEBUG,"Slave %d's Writer writing %ld bytes.\n",
	     SlaveInfo.rank,count);
      gettimeofday(&st, NULL);
#if COMPRESSION
      compt = ZCompressWrite((void*)buff, count, f, Z_DEFAULT_COMPRESSION, &bw);
#else
      if( fwrite((void*)buff,1,count,f) != count ) {
	Vprint(SEV_ERROR,"Slave %d's Writer failed to write to result file.  Terminating.\n",
	       SlaveInfo.rank);
	result_thread_error = 1;
	return NULL;
      }
#endif
      fflush(f);
      gettimeofday(&et, NULL);
      writet = ((et.tv_sec*1000000+et.tv_usec) -
		(st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      pthread_mutex_lock(&(SlaveInfo.time_lock));
      SlaveInfo.t_o  += writet;
      SlaveInfo.t_oc += compt;
      SlaveInfo.b_od += count;
      SlaveInfo.b_o  += bw;
      pthread_mutex_unlock(&(SlaveInfo.time_lock));      

    }
  }

  // Close output file
  fclose(f);

  // Exit as lock should not be held here; return value is ignored
  Vprint(SEV_DEBUG,"Slave %d's Writer done; exiting.\n",SlaveInfo.rank);
  return NULL;
}
*/


// Create the SHM (and synchronization) used to managed the shared global result buffer.
static void CreateResultBuffer()
{
  pthread_attr_t attr;

  // Create and init the result buffer SHM
  if( !(resultbuff=malloc(RESULTBUFF_SHM_SIZE)) ) {
    Vprint(SEV_ERROR,"Failed to allocate shared result buffer. Terminating.\n");
    Abort(1);
  }
  memset(resultbuff,0,RESULTBUFF_SHM_SIZE);
  
  // Initialize result writer synchronization objects
  pthread_mutex_init(&(SlaveInfo.resultb_lock),  NULL);
  pthread_cond_init(&(SlaveInfo.resultb_nfull),  NULL);
  pthread_cond_init(&(SlaveInfo.resultb_nempty), NULL);

  // Spawn a thread to handle writing the shared result buffer to disk
  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
  if( pthread_create(&resultbuff_thread, &attr, ResultWriter, (void*)(&SlaveInfo)) < 0 ) {
    Vprint(SEV_ERROR,"Failed to start resultbuff thread. Terminating.\n");
    Abort(1);
  }

}


static void Init_Slave()
{
  char fn[1024];
  long wr;
  int  rv,fd;
  

  // Build a file name for our private exe copy
  sprintf(fn,"%s",args.exe_base);
  // Open the file
  if( (fd=open(fn,O_WRONLY|O_CREAT|O_TRUNC,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)) < 0 ) {
    Vprint(SEV_ERROR,"Failed to open exe for writing. Terminating.\n");
    Abort(1);
  }
  // Write the data into the destination
  wr = 0;
  do {
    rv = write(fd, shm_exe+wr, shm_exe_sz-wr);
    if( rv < 0 ) {
      Vprint(SEV_ERROR,"Failed to write SHM into exe. Terminating.\n");
      Abort(1);
    }
    wr += rv;
  } while( wr < shm_exe_sz );
  // Close the exe file
  close(fd);

  // Create result buffer (only for ranks responsible)
  CreateResultBuffer();

  // Initialize experimental fork lock 
  pthread_mutex_init(&(SlaveInfo.fork_lock),  NULL);
}


static void Slave(int processes, int rank)
{
  struct timeval  st,et;
  slaveinfo_t    *si=&SlaveInfo;
  compressedb_t  *cb;
  workunit_t     *workunit;
  request_t      *request;
  int             qd=0,i,sz;
  
  memset(si,0,sizeof(slaveinfo_t));
  si->nprocs = processes;
  si->rank   = rank;
  
  Init_Slave();
  Start_Workers();

  // Wait for all the ranks to fully init
  MPI_Barrier(MPI_COMM_WORLD);

  while( !si->done ) {
    // Check our writer's status for error
    if( result_thread_error ) {
      Vprint(SEV_ERROR,"Slave noticed that the writer hit an error. Terminating.\n");
      Abort(1);
    }

    // Get a request from our worker threads
    request = tscq_entry_get(si->rq);
    Vprint(SEV_DEBUG,"Slave %d found a worker in need of work.\n",SlaveInfo.rank);

    // See if we can handle this request without the master
    if( qd > 0 ) {
      // We already put a at least one "extra" work unit on the work queue.
      qd--;
      if( qd >= NCORES ) {
	// If the number of queued work units is more than
	// the number of cores, we won't request more work
	// from the master.  Else, we will get more work.
	tscq_entry_free(si->rq,request);
	Vprint(SEV_DEBUG,"Slave %d not forwarding request: %d WUs queued.\n",
	       SlaveInfo.rank,qd);
	continue;
      }
    }

    // Request a work unit from the master
    Vprint(SEV_DEBUG,"Slave %d asking for more work from master (qd:%d).\n",
	   SlaveInfo.rank,qd);
    request->count = NCORES-qd;
    MPI_Send(request, sizeof(request_t), MPI_BYTE, MASTER_RANK,
             TAG_REQUEST, MPI_COMM_WORLD);
    tscq_entry_free(si->rq,request);

    // Read the work unit from master
    workunit = tscq_entry_new(si->wq);
    MPI_Recv(workunit, sizeof(workunit_t), MPI_BYTE, MASTER_RANK, 
             TAG_WORKUNIT, MPI_COMM_WORLD, MPI_STATUS_IGNORE );

    // Figure out what this work unit is telling us to do
    switch( workunit->type ) {
    case WU_TYPE_EXIT:
      // Master has told us we are done; send this to workers
      Vprint(SEV_DEBUG,"Slave %d told to exit by master.\n",SlaveInfo.rank);
      tscq_entry_free(si->wq,workunit);
      for(i=0; i<NCORES; i++) {
	workunit = tscq_entry_new(si->wq);
	memset(workunit,0,sizeof(workunit_t));
	workunit->type = WU_TYPE_EXIT;
	tscq_entry_put(si->wq,workunit);
      }

      // Wait for the worker threads to exit
      Vprint(SEV_DEBUG,"Slave %d waiting for workers to finish.\n",SlaveInfo.rank);
      for(i=0; i<NCORES; i++) {
	pthread_join(SlaveInfo.workers[i], NULL);
      }

      // Tell the writer thread it is done and wait for it
      Vprint(SEV_DEBUG,"Slave %d waiting for writer to finish.\n",SlaveInfo.rank);
      si->done = 1;
      pthread_mutex_lock(&(SlaveInfo.resultb_lock));
      pthread_cond_signal(&(SlaveInfo.resultb_nempty));
      pthread_mutex_unlock(&(SlaveInfo.resultb_lock));
      pthread_join(resultbuff_thread,NULL);
      
      // !!av: Gating hack
      i = SlaveInfo.nprocs / ConcurrentWriters;
      if( SlaveInfo.nprocs%ConcurrentWriters ) {
	i++;
      }
      gettimeofday(&st, NULL);
      while( i-- ) {
	if( !(i%SlaveInfo.rank) ) {
	  Write(Fd, Cbuff, Nc);
	}
	MPI_Barrier(MPI_COMM_WORLD);
      }
      gettimeofday(&et, NULL);
      SlaveInfo.t_o  = ((et.tv_sec*1000000+et.tv_usec) -
			(st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      break;
    case WU_TYPE_SEQS:
      // Master is sending us sequence data; prepare for and read it
      GrowSeqData(workunit->len+1);
      MPI_Recv(si->seq_data, workunit->len, MPI_BYTE, MASTER_RANK, 
               TAG_SEQDATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      sz = workunit->len;
      tscq_entry_free(si->wq,workunit);
      // Break up the larger work unit into smaller units
      // and send them to worker threads.
      for(i=0,cb=(compressedb_t*)si->seq_data; cb<(compressedb_t*)(si->seq_data+sz); cb=((void*)cb)+cb->len+sizeof(cb->len),i++) {
	// Get queue entry, fill it, and dispatch it
	workunit = tscq_entry_new(si->wq);
	workunit->type = WU_TYPE_SEQF;
	workunit->len  = cb->len+sizeof(cb->len);
	workunit->data = malloc(workunit->len);
	if( !workunit->data ) {
	  Vprint(SEV_ERROR,"Slave failed to allocate work unit data for worker. Terminating.\n");
	  Abort(1);
	}
	memcpy(workunit->data, cb, workunit->len);
	tscq_entry_put(si->wq,workunit);
	// Check for pre-caching case
	if( i ) {
	  qd++;
	}
      }
      Vprint(SEV_DEBUG,"Slave %d turned WU into %d worker WUs (qd:%d).\n",
	     SlaveInfo.rank,i,qd);
      break;
    default:
      Vprint(SEV_ERROR,"Slave received unknown work unit type. Terminating.\n");
      Abort(1);    
      break;
    }
  }

  // Wait for everyone to be ready to exit
  Vprint(SEV_DEBUG,"Slave %d done.\n",SlaveInfo.rank);
  MPI_Barrier(MPI_COMM_WORLD);

  // Now that everyone seems to be ready to exit, go ahead and report times
  Report_Timings(SlaveInfo.rank,
		 SlaveInfo.t_i,
		 SlaveInfo.t_o,
		 SlaveInfo.t_ic,
		 SlaveInfo.t_oc, 
		 SlaveInfo.t_vi,
		 SlaveInfo.t_vo,
		 SlaveInfo.b_i,
		 SlaveInfo.b_o,
		 SlaveInfo.b_id,
		 SlaveInfo.b_od);
  
}


////////////////////////////////////////////////////////////////////////////////
//                              DB Loading Code                               //
////////////////////////////////////////////////////////////////////////////////


static void Load_File(char *name, long size, void *dest)
{
  long rd;
  int  rv,f;

  // Open the DB file
  if( (f = open(name,O_RDONLY)) < 0 ) {
    Vprint(SEV_ERROR,"Failed to open() DB file. Terminating.\n");
    Abort(1);
  }

  // Read the DB into the destination
  rd = 0;
  do {
    rv = read(f, dest+rd, size-rd);
    if( rv < 0 ) {
      Vprint(SEV_ERROR,"Failed to read DB into SHM. Terminating.\n");
      Abort(1);
    }
    rd += rv;
  } while( rd < size );
  
  // Close the DB file
  close(f);
}


static char **Find_DBFiles(int rank, int *nfiles)
{
  char  *fl,*f,**files,*saveptr=NULL;
  int    nf;


  // Put the exe file in the front of the list
  if( !(files=malloc(sizeof(char*))) ) {
    Vprint(SEV_ERROR,"Failed to create file list. Terminating.\n");
    Abort(1);
  }
  files[0] = strdup(args.exe);
  nf = 1;
  
  // Build list of DB files from the file list
  fl = strdup(args.db_files);
  f  = strtok_r(fl, ":", &saveptr);
  while( f ) {
    // Make room in array for another file
    if( !(files=realloc(files,(nf+1)*sizeof(char*))) ) {
      Vprint(SEV_ERROR,"Failed to enlarge file list. Terminating.\n");
      Abort(1);
    }
    // Put file in list
    files[nf] = strdup(f);
    nf++;
    // Advance to next file in list
    f = strtok_r(NULL, ":", &saveptr);
  }
  free(fl);

  // Check number of DB files
  if( nf > MAX_DB_FILES ) {
    Vprint(SEV_ERROR,"Too many DB files. Terminating.\n");
    Abort(1);    
  }

  // Return list of DB files
  *nfiles = nf;
  return files;
}


static void Init_DB(int procs, int rank, float *lt, float *ct)
{
  struct timeval  st,et;
  struct stat     statbf;
  void           *shm;
  long            shmsz;
  char          **files,name[1024];
  int             i,rv,node,nodes,loadstride,range[1][3],nfiles;
  MPI_Group       oldgroup,group;
  MPI_Comm        newcomm=MPI_COMM_NULL,comm;

  // Get list of DB files
  files = Find_DBFiles(rank, &nfiles);

  // These will be used a lot later on
  node       = rank;
  nodes      = procs;
  loadstride = nodes/args.ndbs;

  // This is just for timing data, really
  (*lt)=0.0f;
  (*ct)=0.0f;
  MPI_Barrier(MPI_COMM_WORLD);

  // Figure out our role w.r.t the DB loading
  if( loadstride > 1 ) {
    // A DB load will cover more than one node; comm group needed
    for(i=0; i<args.ndbs; i++) {
      // Each DB represents one of the new comm groups.
      //
      // Note that these calls are collective, so _every_ rank
      // in MPI_COMM_WORLD needs to make the same call, even if
      // one of the ranks is not going to be placed in the new
      // communicator.
      range[0][0] = i*loadstride;
      range[0][1] = range[0][0] + loadstride - 1;
      range[0][2] = 1;
      MPI_Comm_group(MPI_COMM_WORLD,&oldgroup);
      rv = MPI_Group_range_incl(oldgroup, 1, range, &group);
      if( rv != MPI_SUCCESS ) {
        Vprint(SEV_ERROR,"Failed to create commgroup for DB bcast. Terminating.\n");
        Abort(1);
      }
      rv = MPI_Comm_create(MPI_COMM_WORLD, group, &comm);
      if( rv != MPI_SUCCESS ) {
        Vprint(SEV_ERROR,"Failed to create comm for DB bcast. Terminating.\n");
        Abort(1);
      }
      if( i == node/loadstride ) {
        newcomm = comm;
      } else {
        // Cleanup
        //MPI_Comm_free(&comm);
        //MPI_Group_free(&group);
      }
    }
    if( !(node%loadstride) ) {
      ////////////////////////////////////////////////////////////
      // We are a loading rank and we are a bcast send rank.
      ////////////////////////////////////////////////////////////
      // Get file size array ready
      file_sizes = Create_SHM(sizeof(filesizes_t),&file_sizes_fd);
      memset(file_sizes,0,sizeof(filesizes_t));
      // Create the two in/out SHMs per core
      for(i=0; i<NCORES; i++) {
	sprintf(name,":STDIN%d",i*2);
	Create_DBSHM(name,QUERYBUFF_SIZE);
	sprintf(name,":STDOUT%d",i*2+1);
	Create_DBSHM(name,RESULTBUFF_SIZE);
      }
      // For each DB file
      for(i=0; i < nfiles; i++) {
	// Build full name
	if( i ) {
	  // DB file
	  sprintf(name,"%s/%s%d/%s",
		  args.db_path,args.db_prefix,node/loadstride,files[i]);
	} else {
	  // exe file
	  sprintf(name,"%s",files[i]);
	}
        // Find size
	gettimeofday(&st, NULL);
        if( stat(name, &statbf) < 0 ) {
          Vprint(SEV_ERROR,"Failed to stat() DB file \"%s\". Terminating.\n",name);
          Abort(1);
        }
	gettimeofday(&et, NULL);
	(*lt) += ((et.tv_sec*1000000+et.tv_usec) - 
		  (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
        // Bcast size
        shmsz = statbf.st_size;
	gettimeofday(&st, NULL);
        MPI_Bcast(&shmsz, sizeof(shmsz), MPI_BYTE, 0, newcomm);
	gettimeofday(&et, NULL);
	(*ct) += ((et.tv_sec*1000000+et.tv_usec) - 
		  (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
        // Load data
        shm = Create_DBSHM(name,shmsz);
	gettimeofday(&st, NULL);	
        Load_File(name, shmsz, shm);
	gettimeofday(&et, NULL);
	(*lt) += ((et.tv_sec*1000000+et.tv_usec) - 
		  (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
        // Bcast data
	gettimeofday(&st, NULL);
        MPI_Bcast(shm, shmsz, MPI_BYTE, 0, newcomm);
	(*ct) += ((et.tv_sec*1000000+et.tv_usec) - 
		  (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
	// Save exe SHM
	if( !i ) {
	  // exe data
	  shm_exe    = shm;
	  shm_exe_sz = shmsz;
	}
      }
      // We are done loading the DB, we don't need the comm or group any more
      //MPI_Comm_free(&newcomm);
      //MPI_Group_free(&group);
    } else if ( node%loadstride ) {
      ////////////////////////////////////////////////////////////
      // We are a bcast receive rank.
      ////////////////////////////////////////////////////////////
      // Get file size array ready
      file_sizes = Create_SHM(sizeof(filesizes_t),&file_sizes_fd);
      memset(file_sizes,0,sizeof(filesizes_t));
      // Create the two in/out SHMs per core
      for(i=0; i<NCORES; i++) {
	sprintf(name,":STDIN%d",i*2);
	Create_DBSHM(name,QUERYBUFF_SIZE);
	sprintf(name,":STDOUT%d",i*2+1);
	Create_DBSHM(name,RESULTBUFF_SIZE);
      }
      // For each DB file
      for(i=0; i < nfiles; i++) {
        // Receive size
	gettimeofday(&st, NULL);
        MPI_Bcast(&shmsz, sizeof(shmsz), MPI_BYTE, 0, newcomm);
	gettimeofday(&et, NULL);
	(*ct) += ((et.tv_sec*1000000+et.tv_usec) - 
		  (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
        // Receive data
	if( i ) {
	  // DB data
	  sprintf(name,"%s/%s%d/%s",
		  args.db_path,args.db_prefix,node/loadstride,files[i]);
	} else {
	  // exe data
	  sprintf(name,"%s",files[i]);
	}
        shm = Create_DBSHM(name,shmsz);
	gettimeofday(&st, NULL);
        MPI_Bcast(shm, shmsz, MPI_BYTE, 0, newcomm);
	gettimeofday(&et, NULL);
	(*ct) += ((et.tv_sec*1000000+et.tv_usec) - 
		  (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
	// Save exe SHM
	if( !i ) {
	  // exe data
	  shm_exe    = shm;
	  shm_exe_sz = shmsz;
	}
      }
      // We are done loading the DB, we don't need the comm or group any more
      //MPI_Comm_free(&newcomm);
      //MPI_Group_free(&group);
    } else {
      ////////////////////////////////////////////////////////////
      // We are not involved in DB loading
      ////////////////////////////////////////////////////////////
    }
  } else {
    ////////////////////////////////////////////////////////////
    // A DB load will cover only one node.  Load once per node.
    ////////////////////////////////////////////////////////////
    // Get file size array ready
    file_sizes = Create_SHM(sizeof(filesizes_t),&file_sizes_fd);
    memset(file_sizes,0,sizeof(filesizes_t));
    // Create the two in/out SHMs per core
    for(i=0; i<NCORES; i++) {
      sprintf(name,":STDIN%d",i*2);
      Create_DBSHM(name,QUERYBUFF_SIZE);
      sprintf(name,":STDOUT%d",i*2+1);
      Create_DBSHM(name,RESULTBUFF_SIZE);
    }
    // For each DB file
    for(i=0; i < nfiles; i++) {
      // Build full path name
      if( i ) {
	// DB file
	sprintf(name,"%s/%s%d/%s",
		args.db_path,args.db_prefix,node/loadstride,files[i]);
      } else {
	// exe file
	sprintf(name,"%s",files[i]);
      }
      // Find size
      gettimeofday(&st, NULL);
      if( stat(name, &statbf) < 0 ) {
	Vprint(SEV_ERROR,"Failed to stat() DB file. Terminating.\n");
	Abort(1);
      }
      gettimeofday(&et, NULL);
      (*lt) += ((et.tv_sec*1000000+et.tv_usec) - 
		(st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      shmsz = statbf.st_size;
      // Load data
      shm = Create_DBSHM(name,shmsz);
      gettimeofday(&st, NULL);
      Load_File(name, shmsz, shm);
      gettimeofday(&et, NULL);
      (*lt) += ((et.tv_sec*1000000+et.tv_usec) - 
		(st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;
      // Save exe SHM
      if( !i ) {
	shm_exe    = shm;
	shm_exe_sz = shmsz;
      }
    }
  }

}


////////////////////////////////////////////////////////////////////////////////
//                       Application Entry + Helpers                          //
////////////////////////////////////////////////////////////////////////////////


static void Init_MPI(int *procs, int *rank, int *argc, char ***argv)
{
  int rc;

  // Init MPI, get the number of MPI processes and our rank
  rc = MPI_Init(argc,argv);
  if(rc != MPI_SUCCESS) {
    Vprint(SEV_ERROR,"Error starting MPI program. Terminating.\n");
    Abort(rc);
  }
  MPI_Comm_size(MPI_COMM_WORLD,procs);
  MPI_Comm_rank(MPI_COMM_WORLD,rank);
  if( !(*rank) ) {
    Vprint(SEV_NRML,"MPI ranks: %d\n\n",*procs);
  }
  if( (*procs) < 2 ) {
    Vprint(SEV_ERROR,"At least two MPI processes required. Terminating.\n");
    Abort(rc);
  }

  // Save our rank to exclude/include our rank's print on caught signal
  Rank = *rank;

  // Get current working directory.
  if( !(*rank) ) {
    Vprint(SEV_NRML,"Setting up output directories.\n\n");
  }
  if( *rank ) {
    char fn[1024];
    
    // This slave has it's own output directory.
    // I don't want it touching anything that is shared in any way
    // more than it has to.  So, I will create the dir for the slave
    // on startup; after which it can just stay there.
    sprintf(fn,"slaves-%d",(*rank)/96);
    if( mkdir(fn,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) < 0 ) {
      // Do I really care if the dir can't be made or already exists?
      // The next syscall call will fail anyways...
    }
    sprintf(fn,"slaves-%d/slave-%d",(*rank)/96,*rank);
    if( mkdir(fn,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) < 0 ) {
      // Do I really care if the dir can't be made or already exists?
      // The next syscall will fail anyways...
    }
    // The needed directories should now exist.
    // Make our directory the current working dir.
    if( chdir(fn) < 0 ) {
      Vprint(SEV_ERROR,"Slave failed to chdir to work directory. Terminating.\n");
      Abort(1);
    }
  }

  // Record our hostname and rank
  if( !(*rank) ) {
    Vprint(SEV_NRML,"Recording node hostnames.\n\n");
  }
  {
    FILE *f;
    char  b[256];

    if( (f=fopen("host","w")) ) {
      if( !gethostname(b,sizeof(b)) ) {
	fprintf(f,"%s %d\n",b,*rank);
      }
      fclose(f);
    }
  }

}


static void UsageError()
{
  Vprint(SEV_ERROR,"Usage: Set the following env vars correctly:\n");
  Vprint(SEV_ERROR,"\tMCW_DB_PATH\n");
  Vprint(SEV_ERROR,"\tMCW_DB_PREFIX\n");
  Vprint(SEV_ERROR,"\tMCW_DB_COUNT\n");
  Vprint(SEV_ERROR,"\tMCW_DB_FILES\n");
  Vprint(SEV_ERROR,"\tMCW_Q_FILE\n");
  Vprint(SEV_ERROR,"\tMCW_Q_WUM\n");
  Vprint(SEV_ERROR,"\tMCW_S_TIMELIMIT\n");
  Vprint(SEV_ERROR,"\tMCW_S_EXE\n");
  Vprint(SEV_ERROR,"\tMCW_S_LINE\n");
  Vprint(SEV_ERROR,"\tMCW_S_DUP2\n");
  Abort(1);
}


static void Parse_Environment(int procs)
{
  char *p;


  // Read DB settings
  if( !(p=getenv("MCW_DB_PATH")) ) {
    UsageError();
  }
  args.db_path = strdup(p);
  if( !(p=getenv("MCW_DB_PREFIX")) ) {
    UsageError();
  }
  args.db_prefix = strdup(p);
  if( !(p=getenv("MCW_DB_COUNT")) ) {
    UsageError();
  }
  if( sscanf(p,"%d",&(args.ndbs)) != 1 ) {
    UsageError();
  }
  if( (procs)%args.ndbs ) {
    Vprint(SEV_ERROR,"(MPI processes) %c nDBs != 0.\n",'%');
    UsageError();
  }
  if( !(p=getenv("MCW_DB_FILES")) ) {
    UsageError();
  }
  args.db_files = strdup(p);

  // Read Query settings
  if( !(p=getenv("MCW_Q_FILE")) ) {
    UsageError();
  }
  args.queryf = strdup(p);
  if( !(p=getenv("MCW_Q_WUM")) ) {
    UsageError();
  }
  if( sscanf(p,"%d",&(args.wum)) != 1 ) {
    UsageError();
  }

  // Read search settings
  if( !(p=getenv("MCW_S_TIMELIMIT")) ) {
    UsageError();
  }
  if( sscanf(p,"%ld",&(args.time_limit)) != 1 ) {
    UsageError();
  }
  if( !(p=getenv("MCW_S_EXE")) ) {
    UsageError();
  }
  args.exe = strdup(p);
  for(p=args.exe_base=args.exe; *p != '\0'; p++) {
    if( *p == '/' ) {
      args.exe_base = p+1;
    }
  }
  args.exe_base = strdup(args.exe_base);
  if( !(p=getenv("MCW_S_LINE")) ) {
    UsageError();
  }
  args.mode = strdup(p);
  if( !(p=getenv("MCW_S_DUP2")) ) {
    UsageError();
  }
  args.dup2 = strdup(p);

}


static void cleanup()
{
}


static void sighndler(int arg)
{
  // Be verbose
  if( !Rank ) {
    write(2,"ms: Caught signal; cleaning up.\n",32);
  }

  cleanup();
  kill(getpid(), SIGKILL);
}

static void exitfunc()
{
  // Be verbose
  if( !Rank ) {
    write(2,"ms: atexit(); cleaning up.\n",27);
  }

  cleanup();
}


void* killtimer(void *arg)
{
  long st;

  // Record start time
  st = time(NULL);

  // Wait until our time limit is exceeded
  while( (time(NULL)-st) <= args.time_limit ) {
    sleep(1);
  }

  // Be a little verbose
  Vprint(SEV_ERROR,"Master noticed time limit exceeded.  Terminating.\n\n");

  // Terminate our process in a way the MPI subsystem will pick up on
  cleanup();
  kill(getpid(), SIGKILL);
  Abort(1);

  // This will be ignored
  return NULL;
}


int main(int argc, char **argv)
{
  struct timeval  st,et;
  float           lt,ct;
  int             processes,rank;


  // Be sure we can catch some basic signals to ensure
  // all SHMs can be removed, even in case of error.
  atexit(exitfunc);
  signal(SIGTERM, sighndler);
  signal(SIGPIPE, SIG_IGN);

  // Initialize basic MPI subsystem
  gettimeofday(&st, NULL);
  Init_MPI(&processes, &rank, &argc, &argv);

  // Let the master take a look at the args first;
  // no need to see thousands of error messages.
  if( !rank ) {
    Parse_Environment(processes);
  }

  // Everyone else needs to parse args too.
  MPI_Barrier(MPI_COMM_WORLD);
  Parse_Environment(processes);
  MPI_Barrier(MPI_COMM_WORLD);

  // Record this portion of init time
  gettimeofday(&et, NULL);
  init_time = ((et.tv_sec*1000000+et.tv_usec) -
	       (st.tv_sec*1000000+st.tv_usec))  / 1000000.0f;

  // Start timer now that we know the limit from the command line
  if( !rank ) {
    pthread_attr_t attr;
    pthread_t      thread;

    // Setup kill timer thread properties
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    // Start kill timer threads
    if( pthread_create(&thread, &attr, killtimer, NULL) )  { 
      Vprint(SEV_ERROR,"Master failed to start kill timer thread. Terminating.\n");
      Abort(1);
    }
  }

  // The master will record some timings for us
  if( !rank ) {
    memset(&MasterInfo,0,sizeof(masterinfo_t));
    MasterInfo.st = time(NULL);
  }

  // Initialize DB and make data available to all nodes
  if( !rank ) {
    Vprint(SEV_NRML,"Distributing DB files to nodes.\n\n");
  }
  Init_DB(processes,rank,&lt,&ct);

  // Wait for all the ranks to finish DB loading
  MPI_Barrier(MPI_COMM_WORLD);

  // Master reports DB load time
  if( !rank ) {
    Vprint(SEV_NRML,"Finished with DB load in %.3f seconds (%.3f I/O).\n",
	   ct+lt,lt);
  }

  // Which role do we play?
  if( !rank ) {
    // We will hand out work units (queries) to the workers
    Master(processes,rank);
  } else {
    // We will request and receive work units from the master
    Slave(processes,rank);
  }

  // Wait for all the ranks to get ready before cleanup
  MPI_Barrier(MPI_COMM_WORLD);

  // Free any SHM segments
  cleanup();

  // Done with MPI
  MPI_Finalize();
  return 0;
}