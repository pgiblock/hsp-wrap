/* $Id: urlquery.c,v 6.50 2010/10/16 20:11:44 lavr Exp $
 * ===========================================================================
 *
 *                            PUBLIC DOMAIN NOTICE
 *            National Center for Biotechnology Information (NCBI)
 *
 *  This software/database is a "United States Government Work" under the
 *  terms of the United States Copyright Act.  It was written as part of
 *  the author's official duties as a United States Government employee and
 *  thus cannot be copyrighted.  This software/database is freely available
 *  to the public for use. The National Library of Medicine and the U.S.
 *  Government do not place any restriction on its use or reproduction.
 *  We would, however, appreciate having the NCBI and the author cited in
 *  any work or product based on this material
 *
 *  Although all reasonable efforts have been taken to ensure the accuracy
 *  and reliability of the software and data, the NLM and the U.S.
 *  Government do not and cannot warrant the performance or results that
 *  may be obtained by using this software or data. The NLM and the U.S.
 *  Government disclaim all warranties, express or implied, including
 *  warranties of performance, merchantability or fitness for any particular
 *  purpose.
 *
 * ===========================================================================
 *
 * File Name:  urlquery.c
 *
 * Author:  Jonathan Kans
 *
 * Version Creation Date:   4/16/98
 *
 * $Revision: 6.50 $
 *
 * File Description: 
 *
 * Modifications:  
 * --------------------------------------------------------------------------
 * $Log: urlquery.c,v $
 * Revision 6.50  2010/10/16 20:11:44  lavr
 * Adjust timeout setting sequence
 *
 * Revision 6.49  2009/09/24 14:58:19  lavr
 * Fix timeout comparison bug
 *
 * Revision 6.48  2009/09/22 13:46:33  lavr
 * Speedup queue insertion
 *
 * Revision 6.47  2009/09/21 20:38:30  lavr
 * Properly treat timeout settings
 *
 * Revision 6.46  2009/09/18 19:58:40  lavr
 * +QUERY_QueueSize()
 *
 * Revision 6.45  2009/09/12 01:28:49  lavr
 * Correct comment in setting HTTP header
 *
 * Revision 6.44  2009/08/14 19:02:44  lavr
 * x_SetupUserHeader() to override C-T only and to extend User-Agent
 *
 * Revision 6.43  2009/08/14 18:14:43  lavr
 * Formatting
 *
 * Revision 6.42  2009/08/14 18:04:53  lavr
 * StringHasText->StringDoesHaveText
 *
 * Revision 6.41  2009/08/14 18:02:24  lavr
 * Use GetProgramName() when setting HTTP user-header
 *
 * Revision 6.40  2006/10/17 02:19:07  lavr
 * Use "const char*" wherever appropriate
 *
 * Revision 6.39  2006/04/19 02:11:23  lavr
 * QUERY_OpenServiceQuery: Use PostOverrideArg instead of AppendArg
 *
 * Revision 6.38  2006/04/19 01:41:52  lavr
 * QUERY_OpenServiceQuery(): Take advantage of ConnNetInfo_*Arg API
 *
 * Revision 6.37  2006/04/17 16:47:50  lavr
 * QUERY_OpenServiceQueryEx not to override but append argument in net_info
 *
 * Revision 6.36  2006/04/15 01:59:01  lavr
 * +QUERY_OpenServiceQueryEx
 *
 * Revision 6.35  2006/01/19 21:11:15  lavr
 * QUERY_SendQuery() to return EIO_Status
 *
 * Revision 6.34  2006/01/17 21:11:51  lavr
 * Formatting; a few minor code improvements; a few minor bugfixes
 *
 * Revision 6.33  2005/12/01 18:47:49  lavr
 * Code formatting
 *
 * Revision 6.32  2004/09/16 19:16:45  lavr
 * QUERY_OpenUrlQuery() to set port only if provided as non-zero
 *
 * Revision 6.31  2004/09/14 19:09:41  lavr
 * Correct last change log entry
 *
 * Revision 6.30  2004/09/14 18:52:12  kans
 * QUERY_OpenUrlQuery makes content type only if type < eMIME_T_Unknown
 *
 * Revision 6.29  2004/09/14 18:47:51  kans
 * QUERY_OpenUrlQuery looks for eMIME_T_Unknown, does not compost content type
 *
 * Revision 6.28  2004/02/23 15:30:02  lavr
 * New (last) parameter "how" added in CONN_Write() API call
 *
 * Revision 6.27  2003/10/21 18:27:43  lavr
 * QUERY_OpenServiceQuery(): Timeout override changed
 *
 * Revision 6.26  2003/09/03 21:15:29  lavr
 * Reuse "arguments" in QUERY_OpenServiceQuery() to be real service argument
 * (formely it was to modify the dispatcher and was not really used anywhere)
 *
 * Revision 6.25  2003/05/29 21:54:20  kans
 * QUERY_CheckQueue calls callback and dequeues connection if status is
 * eIO_Closed as well as eIO_Success  - callbacks already check status
 * before trying to read
 *
 * Revision 6.24  2003/05/29 19:02:56  kans
 * badstatus only used for future debugging purposes, does not block further
 * checks, also increments count of remaining queued connections for return
 * value
 *
 * Revision 6.23  2003/05/29 18:39:54  kans
 * QUERY_CheckQueue sets new badstatus field, protects against further checks,
 * if eIO_Timeout or eIO_Closed
 *
 * Revision 6.22  2002/11/21 20:35:41  kans
 * forgot to call ConnNetInfo_Destroy if bailing due to NULL connector
 *
 * Revision 6.21  2002/11/21 19:46:32  kans
 * if connector is NULL, do not call CONN_Create
 *
 * Revision 6.20  2002/08/07 18:45:17  lavr
 * Change from deprecated to current EIO_ReadMethod enums
 *
 * Revision 6.19  2002/07/02 17:17:58  kans
 * fixed error message
 *
 * Revision 6.18  2001/09/25 13:20:56  lavr
 * SERVICE_CreateConnectorEx() - number of args adjusted
 *
 * Revision 6.17  2001/08/16 18:07:04  kans
 * protect against calling CONN_Read or CONN_Write with NULL conn parameter
 *
 * Revision 6.16  2001/06/07 20:17:34  kans
 * in QUERY_OpenServiceQuery, pass service to ConnNetInfo_Create
 *
 * Revision 6.15  2001/06/07 20:07:41  kans
 * added QUERY_OpenServiceQuery
 *
 * Revision 6.14  2001/04/25 15:14:27  lavr
 * SConnNetInfo::timeout is now a pointer
 *
 * Revision 6.13  2001/02/25 21:42:27  kans
 * changed several Uint4s to size_t due to new prototypes
 *
 * Revision 6.12  2001/02/21 22:02:04  lavr
 * Changes for use new CONN interface
 *
 * Revision 6.11  2000/08/18 19:08:58  kans
 * added QUERY_WaitForNextMacEvent, otherwise QuickDraw collides with mmdbapi
 *
 * Revision 6.10  2000/06/30 18:16:09  kans
 * protect against reentrant calls if resultproc is GUI and processes timer -
 * showed up on PC/Windows, not Mac or UNIX version of Sequin
 *
 * Revision 6.9  2000/06/30 12:46:10  kans
 * added QUERY_CloseQueue
 *
 * Revision 6.8  2000/06/29 18:27:10  kans
 * QUERY_OpenUrlQuery has new EMIME_Type type and EMIME_Encoding
 * encoding parameters
 *
 * Revision 6.7  2000/06/13 12:58:14  kans
 * added closeConn parameter to QUERY_AddToQueue
 *
 * Revision 6.6  1999/07/28 21:09:15  vakatov
 * Multiple fixes in QUERY_OpenUrlQuery() to make it work with a generic
 * URL server;  also, pass arguments in the cmd.-line
 *
 * ==========================================================================
 */

#include "asnbuild.h"
#include <urlquery.h>


#ifdef OS_MAC
#include <Events.h>

NLM_EXTERN void QUERY_WaitForNextMacEvent (void)
{
  EventRecord  currEvent;

  WaitNextEvent (0, &currEvent, 0, NULL);
}
#endif


/* Set HTTP user header */
static void x_SetupUserHeader (
  SConnNetInfo*  net_info,
  const char*    appName,
  EMIME_Type     type,
  EMIME_SubType  subtype,
  EMIME_Encoding encoding
)
{
  const char* userAgentName = NULL;
  char        user_header [MAX_CONTENT_TYPE_LEN + 80];

  /* content-type if specified */
  if (type < eMIME_T_Unknown) {
    VERIFY( MIME_ComposeContentTypeEx (type, subtype, encoding,
                                       user_header, MAX_CONTENT_TYPE_LEN) );
    ConnNetInfo_OverrideUserHeader (net_info, user_header);
  }

  /* allow the user to specify a prog. name, otherwise get it from elsewhere */
  if (StringHasNoText (appName)) {
    const char* progName = GetProgramName();
    if (StringHasNoText (progName)) {
      char path [PATH_MAX];
      Nlm_ProgramPath (path, sizeof (path));
      userAgentName = StringRChr (path, DIRDELIMCHR);
      if (userAgentName)
        ++userAgentName;
    } else
      userAgentName = progName;
  } else
    userAgentName = appName;
  if (StringDoesHaveText (userAgentName)) {
    sprintf (user_header, "User-Agent: %.80s\r\n", userAgentName);
    ConnNetInfo_ExtendUserHeader (net_info, user_header);
  }
}


NLM_EXTERN CONN QUERY_OpenUrlQuery (
  const char*     host_machine,
  Nlm_Uint2       host_port,
  const char*     host_path,
  const char*     arguments,
  const char*     appName,
  Nlm_Uint4       timeoutsec,
  EMIME_Type      type,
  EMIME_SubType   subtype,
  EMIME_Encoding  encoding,
  THCC_Flags      flags
)
{
  CONN           conn;
  CONNECTOR      connector;
  SConnNetInfo*  net_info;
  EIO_Status     status;

  if (StringHasNoText (host_path))
    return NULL;

  /* fill in connection info fields and create the connection */
  net_info = ConnNetInfo_Create(0);
  ASSERT ( net_info );

  x_SetupUserHeader (net_info, appName, type, subtype, encoding);

  if (StringDoesHaveText (host_machine)) {
    StringNCpy_0 (net_info->host, host_machine, sizeof (net_info->host));
  }
  if ( host_port ) {
    net_info->port = host_port;
  }
  StringNCpy_0 (net_info->path, host_path, sizeof (net_info->path));
  if (StringDoesHaveText (arguments)) {
    StringNCpy_0 (net_info->args, arguments, sizeof (net_info->args));
  }

  if (timeoutsec == (Nlm_Uint4)(-1L)) {
    net_info->timeout  = kInfiniteTimeout;
  } else if ( timeoutsec ) {
    net_info->tmo.sec  = timeoutsec;
    net_info->tmo.usec = timeoutsec;
    net_info->timeout  = &net_info->tmo;
  }

  connector = HTTP_CreateConnector (net_info, NULL, flags);

  ConnNetInfo_Destroy (net_info);

  if (connector == NULL) {
    ErrPostEx (SEV_ERROR, 0, 0, "QUERY_OpenUrlQuery failed in HTTP_CreateConnector");
    conn = NULL;
  } else if ((status = CONN_Create (connector, &conn)) != eIO_Success) {
    ErrPostEx (SEV_ERROR, 0, 0, "QUERY_OpenUrlQuery failed in CONN_Create: %s",
              IO_StatusStr (status));
    ASSERT (conn == NULL);
  }

  return conn;
}


NLM_EXTERN CONN QUERY_OpenServiceQueryEx (
  const char* service,
  const char* parameters,
  Nlm_Uint4   timeoutsec,
  const char* arguments
)
{
  CONN           conn;
  CONNECTOR      connector;
  SConnNetInfo*  net_info;
  size_t         n_written;
  EIO_Status     status;

  /* fill in connection info fields and create the connection */
  net_info = ConnNetInfo_Create (service);
  ASSERT ( net_info );

  /* let the user agent be set with a program name */
  x_SetupUserHeader (net_info,
                     NULL, eMIME_T_Unknown, eMIME_Unknown, eENCOD_None);

  if (timeoutsec == (Nlm_Uint4)(-1L)) {
    net_info->timeout  = kInfiniteTimeout;
  } else if ( timeoutsec ) {
    net_info->tmo.sec  = timeoutsec;
    net_info->tmo.usec = timeoutsec;
    net_info->timeout  = &net_info->tmo;
  }

  ConnNetInfo_PostOverrideArg (net_info, arguments, 0);

  connector = SERVICE_CreateConnectorEx (service, fSERV_Any, net_info, 0);

  ConnNetInfo_Destroy (net_info);

  if (connector == NULL) {
    ErrPostEx (SEV_ERROR, 0, 0, "QUERY_OpenServiceQuery failed in SERVICE_CreateConnectorEx");
    conn = NULL;
  } else if ((status = CONN_Create (connector, &conn)) != eIO_Success) {
    ErrPostEx (SEV_ERROR, 0, 0, "QUERY_OpenServiceQuery failed in CONN_Create:"
               " %s", IO_StatusStr (status));
    ASSERT (conn == NULL);
  } else if (StringDoesHaveText (parameters)) {
    status = CONN_Write (conn, parameters, StringLen (parameters),
                         &n_written, eIO_WritePersist);
    if (status != eIO_Success) {
      ErrPostEx (SEV_ERROR, 0, 0, "QUERY_OpenServiceQuery failed to write service parameters in CONN_Write: %s", IO_StatusStr (status));
      CONN_Close (conn);
      conn = NULL;
    }
  }

  return conn;
}


NLM_EXTERN CONN QUERY_OpenServiceQuery (
  const char* service,
  const char* parameters,
  Nlm_Uint4   timeoutsec
)
{
  return QUERY_OpenServiceQueryEx (service, parameters, timeoutsec, 0);
}


NLM_EXTERN EIO_Status QUERY_SendQuery (
  CONN conn
)

{
  static const STimeout kPollTimeout = { 0 };
  EIO_Status            status;

  if (conn == NULL) return eIO_Closed;

  /* flush buffer, sending query, without waiting for response */
  status = CONN_Wait (conn, eIO_Read, &kPollTimeout);
  return status == eIO_Timeout ? eIO_Success : status;
}


#define URL_QUERY_BUFLEN  4096

NLM_EXTERN void QUERY_CopyFileToQuery (
  CONN conn,
  FILE *fp
)
{
  char*        buffer;
  size_t       ct;
  size_t       n_written;
  EIO_Status   status;

  if (conn == NULL || fp == NULL) return;

  buffer = (char*) MemNew(URL_QUERY_BUFLEN + 1);
  if (buffer != NULL) {
    while ((ct = FileRead (buffer, 1, URL_QUERY_BUFLEN, fp)) > 0) {
      status = CONN_Write (conn, (const void *) buffer, ct,
                           &n_written, eIO_WritePersist);
      if (status != eIO_Success)
        break;
    }
    MemFree (buffer);
  }
}


NLM_EXTERN void QUERY_CopyResultsToFile (
  CONN conn,
  FILE *fp
)
{
  char*        buffer;
  size_t       n_read;
  EIO_Status   status;

  if (conn == NULL || fp == NULL) return;

  buffer = (char*) MemNew(URL_QUERY_BUFLEN + 1);
  if (buffer != NULL) {
    do {
      status = CONN_Read (conn, buffer, URL_QUERY_BUFLEN, &n_read, eIO_ReadPlain);
      if ( n_read )
        FileWrite (buffer, 1, n_read, fp);
    } while (status == eIO_Success);
    MemFree (buffer);
  }
}


static Nlm_Int2 LIBCALL AsnIoConnWrite (
  Pointer ptr,
  CharPtr buf,
  Nlm_Uint2 count
)
{
  size_t        bytes;
  AsnIoConnPtr  aicp;

  aicp = (AsnIoConnPtr) ptr;
  if (aicp == NULL || aicp->conn == NULL) return 0;
  CONN_Write (aicp->conn, buf, (size_t) count, &bytes, eIO_WritePersist);
  return (Nlm_Int2) bytes;
}


static Nlm_Int2 LIBCALL AsnIoConnRead (
  Pointer ptr,
  CharPtr buf,
  Nlm_Uint2 count
)
{
  size_t        bytes;
  AsnIoConnPtr  aicp;

  aicp = (AsnIoConnPtr) ptr;
  if (aicp == NULL || aicp->conn == NULL) return 0;
  CONN_Read (aicp->conn, buf, (size_t) count, &bytes, eIO_ReadPlain);
  return (Nlm_Int2) bytes;
}


NLM_EXTERN AsnIoConnPtr QUERY_AsnIoConnOpen (
  const char* mode,
  CONN conn
)
{
  Int1          type;
  AsnIoConnPtr  aicp;

  if (strcmp(mode, "r") == 0)
    type = (ASNIO_IN | ASNIO_TEXT);
  else if (strcmp(mode, "rb") == 0)
    type = (ASNIO_IN | ASNIO_BIN);
  else if (strcmp(mode, "w") == 0)
    type = (ASNIO_OUT | ASNIO_TEXT);
  else if (strcmp(mode, "wb") == 0)
    type = (ASNIO_OUT | ASNIO_BIN);
  else {
    AsnIoErrorMsg (NULL, 81, mode);
    return NULL;
  }

  aicp = (AsnIoConnPtr) MemNew (sizeof (AsnIoConn));
  aicp->aip = AsnIoNew (type, NULL, (Pointer) aicp, AsnIoConnRead, AsnIoConnWrite);
  aicp->conn = conn;
  return aicp;
}


NLM_EXTERN AsnIoConnPtr QUERY_AsnIoConnClose (
  AsnIoConnPtr aicp
)
{
  if (aicp == NULL) return NULL;
  AsnIoClose (aicp->aip);
  aicp->conn = NULL;
  return (AsnIoConnPtr) MemFree (aicp);
}


typedef struct SQueueTag {
  CONN                conn;
  QueryResultProc     resultproc;
  Nlm_VoidPtr         userdata;
  Nlm_Boolean         closeConn;
  Nlm_Boolean         protect;
  struct SQueueTag*   next;
} SConnQueue, PNTR QueuePtr;


NLM_EXTERN void QUERY_AddToQueue (
  QUEUE* queue,
  CONN conn,
  QueryResultProc resultproc,
  Nlm_VoidPtr userdata,
  Nlm_Boolean closeConn
)
{
  QueuePtr       cqp;
  QueuePtr PNTR  qptr;

  ASSERT (queue != NULL);

  if (conn == NULL || resultproc == NULL) return;

  /* allocate queue element */

  cqp = (QueuePtr) MemNew (sizeof (SConnQueue));
  if (cqp == NULL) return;

  cqp->conn = conn;
  cqp->resultproc = resultproc;
  cqp->userdata = userdata;
  cqp->closeConn = closeConn;
  cqp->protect = FALSE;

  /* add to polling queue */

  for (qptr = (QueuePtr PNTR) queue;  *qptr != NULL;  qptr = &(*qptr)->next);
  *qptr = cqp;
}


static void QUERY_RemoveFromQueue (
  QUEUE* queue,
  CONN conn
)
{
  QueuePtr       curr;
  QueuePtr       next;
  QueuePtr PNTR  prev;
  QueuePtr PNTR  qptr;

  qptr = (QueuePtr PNTR) queue;
  if (qptr == NULL || *qptr == NULL || conn == NULL) return;

  prev = qptr;
  curr = *qptr;

  while (curr != NULL) {
    next = curr->next;
    if (curr->conn == conn) {
      *prev = next;
      curr->next = NULL;
      MemFree (curr);
    } else {
      prev = &(curr->next);
    }
    curr = next;
  }
}


NLM_EXTERN Nlm_Int4 QUERY_CheckQueue (
  QUEUE* queue
)
{
  static const STimeout kPollTimeout = { 0 };
  Nlm_Int4       count = 0;
  QueuePtr       curr;
  QueuePtr       next;
  QueuePtr PNTR  qptr;
  EIO_Status     status;

  qptr = (QueuePtr PNTR) queue;
  if (qptr == NULL || *qptr == NULL) return 0;

  curr = *qptr;

  while (curr != NULL) {
    next = curr->next;

    if (curr->conn != NULL && (! curr->protect)) {
      status = CONN_Wait (curr->conn, eIO_Read, &kPollTimeout);

      if (status == eIO_Success || status == eIO_Closed) {
        /* protect against reentrant calls if resultproc is GUI and processes timer */
        curr->protect = TRUE;
        if (curr->resultproc != NULL) {
          /* result could eventually be used to reconnect on timeout */
          curr->resultproc (curr->conn, curr->userdata, status);
        }
        if (curr->closeConn) {
          CONN_Close (curr->conn);
        }
        QUERY_RemoveFromQueue (queue, curr->conn);
      } else {
        count++;
      }
    }

    curr = next;
  }

  return count;
}


NLM_EXTERN Nlm_Int4 QUERY_QueueSize (
  QUEUE queue
)
{
  Nlm_Int4  count = 0;
  QueuePtr  qptr;

  for (qptr = (QueuePtr) queue;  qptr;  qptr = qptr->next) {
    count++;
  }

  return count;
}


NLM_EXTERN void QUERY_CloseQueue (
  QUEUE* queue
)
{
  QueuePtr       curr;
  QueuePtr       next;
  QueuePtr PNTR  qptr;

  qptr = (QueuePtr PNTR) queue;
  if (qptr == NULL || *qptr == NULL) return;

  curr = *qptr;

  while (curr != NULL) {
    next = curr->next;

    if (curr->conn != NULL) {
      CONN_Close (curr->conn);
      QUERY_RemoveFromQueue (queue, curr->conn);
    }

    curr = next;
  }
}
