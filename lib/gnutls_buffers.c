/*
 * Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation
 *
 * Author: Nikos Mavroyanopoulos
 *
 * This file is part of GNUTLS.
 *
 * The GNUTLS library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA
 *
 */

/* This is the only file that uses the berkeley sockets API.
 * 
 * Also holds all the buffering code used in gnutls.
 * The buffering code works as:
 *
 * RECORD LAYER: 
 *  1. uses a buffer to hold data (application/handshake),
 *    we got but they were not requested, yet.
 *  (see gnutls_record_buffer_put(), gnutls_record_buffer_get_size() etc.)
 *
 *  2. uses a buffer to hold data that were incomplete (ie the read/write
 *    was interrupted)
 *  (see _gnutls_io_read_buffered(), _gnutls_io_write_buffered() etc.)
 * 
 * HANDSHAKE LAYER:
 *  1. Uses a buffer to hold data that was not sent or received
 *  complete. (E.g. sent 10 bytes of a handshake packet that is 20 bytes
 *  long).
 * (see _gnutls_handshake_send_int(), _gnutls_handshake_recv_int())
 *
 *  2. Uses buffer to hold the last received handshake message.
 *  (see _gnutls_handshake_buffer_put() etc.)
 *
 */

#include <gnutls_int.h>
#include <gnutls_errors.h>
#include <gnutls_num.h>
#include <gnutls_record.h>
#include <gnutls_buffers.h>

#ifdef HAVE_ERRNO_H
# include <errno.h>
#endif

#ifdef _WIN32
# include <winsock2.h>
#endif

#ifndef EAGAIN
# define EAGAIN EWOULDBLOCK
#endif

inline static int
RET (int err)
{
  if (err == EAGAIN)
    return GNUTLS_E_AGAIN;
  return GNUTLS_E_INTERRUPTED;
}

#ifdef IO_DEBUG
# include <io_debug.h>
#endif

/* Buffers received packets of type APPLICATION DATA and
 * HANDSHAKE DATA.
 */
int
_gnutls_record_buffer_put (content_type_t type,
			   gnutls_session_t session, opaque * data,
			   size_t length)
{
  if (length == 0)
    return 0;
  switch (type)
    {
    case GNUTLS_APPLICATION_DATA:

      if (_gnutls_buffer_append
	  (&session->internals.application_data_buffer, data, length) < 0)
	{
	  gnutls_assert ();
	  return GNUTLS_E_MEMORY_ERROR;
	}
      _gnutls_buffers_log ("BUF[REC]: Inserted %d bytes of Data(%d)\n",
			   length, type);

      break;
    case GNUTLS_HANDSHAKE:
      if (_gnutls_buffer_append
	  (&session->internals.handshake_data_buffer, data, length) < 0)
	{
	  gnutls_assert ();
	  return GNUTLS_E_MEMORY_ERROR;
	}

      _gnutls_buffers_log ("BUF[HSK]: Inserted %d bytes of Data(%d)\n",
			   length, type);

      break;

    default:
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  return 0;

}

int
_gnutls_record_buffer_get_size (content_type_t type, gnutls_session_t session)
{
  switch (type)
    {
    case GNUTLS_APPLICATION_DATA:
      return session->internals.application_data_buffer.length;

    case GNUTLS_HANDSHAKE:
      return session->internals.handshake_data_buffer.length;

    default:
      return GNUTLS_E_INVALID_REQUEST;
    }
}

/**
  * gnutls_record_check_pending - checks if there are any data to receive in gnutls buffers.
  * @session: is a #gnutls_session_t structure.
  *
  * This function checks if there are any data to receive
  * in the gnutls buffers. Returns the size of that data or 0.
  * Notice that you may also use select() to check for data in
  * a TCP connection, instead of this function.
  * (gnutls leaves some data in the tcp buffer in order for select
  * to work).
  **/
size_t
gnutls_record_check_pending (gnutls_session_t session)
{
  return _gnutls_record_buffer_get_size (GNUTLS_APPLICATION_DATA, session);
}

int
_gnutls_record_buffer_get (content_type_t type,
			   gnutls_session_t session, opaque * data,
			   size_t length)
{
  if (length == 0 || data == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  switch (type)
    {
    case GNUTLS_APPLICATION_DATA:

      if (length > session->internals.application_data_buffer.length)
	{
	  length = session->internals.application_data_buffer.length;
	}

      _gnutls_buffers_log ("BUFFER[REC][AD]: Read %d bytes of Data(%d)\n",
			   length, type);

      session->internals.application_data_buffer.length -= length;
      memcpy (data, session->internals.application_data_buffer.data, length);

      /* overwrite buffer */
      memmove (session->internals.application_data_buffer.data,
	       &session->internals.application_data_buffer.data[length],
	       session->internals.application_data_buffer.length);

      /* we do no longer realloc the application_data_buffer.data,
       * since it serves no practical reason. It also decreases
       * performance.
       */
      break;

    case GNUTLS_HANDSHAKE:
      if (length > session->internals.handshake_data_buffer.length)
	{
	  length = session->internals.handshake_data_buffer.length;
	}

      _gnutls_buffers_log ("BUF[REC][HD]: Read %d bytes of Data(%d)\n",
			   length, type);

      session->internals.handshake_data_buffer.length -= length;
      memcpy (data, session->internals.handshake_data_buffer.data, length);

      /* overwrite buffer */
      memmove (session->internals.handshake_data_buffer.data,
	       &session->internals.handshake_data_buffer.data[length],
	       session->internals.handshake_data_buffer.length);

      break;
    default:
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }


  return length;
}


/* This function is like read. But it does not return -1 on error.
 * It does return gnutls_errno instead.
 *
 * Flags are only used if the default recv() function is being used.
 */
static ssize_t
_gnutls_read (gnutls_session_t session, void *iptr,
	      size_t sizeOfPtr, int flags)
{
  size_t left;
  ssize_t i = 0;
  char *ptr = iptr;
  uint j, x, sum = 0;
  gnutls_transport_ptr_t fd = session->internals.transport_recv_ptr;

  session->internals.direction = 0;

  left = sizeOfPtr;
  while (left > 0)
    {

      if (session->internals._gnutls_pull_func == NULL)
	i = recv ((int) fd, &ptr[sizeOfPtr - left], left, flags);
      else
	i = session->internals._gnutls_pull_func (fd,
						  &ptr[sizeOfPtr -
						       left], left);

      if (i < 0)
	{
	  _gnutls_read_log ("READ: %d returned from %d, errno=%d\n", i,
			    fd, errno);

	  if (errno == EAGAIN || errno == EINTR)
	    {
	      if (sizeOfPtr - left > 0)
		{

		  _gnutls_read_log ("READ: returning %d bytes from %d\n",
				    sizeOfPtr - left, fd);

		  goto finish;
		}
	      gnutls_assert ();

	      return RET (errno);
	    }
	  else
	    {
	      gnutls_assert ();
	      return GNUTLS_E_PULL_ERROR;
	    }
	}
      else
	{

	  _gnutls_read_log ("READ: Got %d bytes from %d\n", i, fd);

	  if (i == 0)
	    break;		/* EOF */
	}

      left -= i;

    }

finish:

  if (_gnutls_log_level >= 7)
    {
      char line[128];
      char tmp[16];


      _gnutls_read_log ("READ: read %d bytes from %d\n",
			(sizeOfPtr - left), fd);

      for (x = 0; x < ((sizeOfPtr - left) / 16) + 1; x++)
	{
	  line[0] = 0;

	  sprintf (tmp, "%.4x - ", x);
	  _gnutls_str_cat (line, sizeof (line), tmp);

	  for (j = 0; j < 16; j++)
	    {
	      if (sum < (sizeOfPtr - left))
		{
		  sprintf (tmp, "%.2x ", ((unsigned char *) ptr)[sum++]);
		  _gnutls_str_cat (line, sizeof (line), tmp);
		}
	    }
	  _gnutls_read_log ("%s\n", line);
	}
    }

  return (sizeOfPtr - left);
}


#define RCVLOWAT session->internals.lowat

/* This function is only used with berkeley style sockets.
 * Clears the peeked data (read with MSG_PEEK).
 */
int
_gnutls_io_clear_peeked_data (gnutls_session_t session)
{
  char *peekdata;
  int ret, sum;

  if (session->internals.have_peeked_data == 0 || RCVLOWAT == 0)
    return 0;

  peekdata = gnutls_alloca (RCVLOWAT);
  if (peekdata == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_MEMORY_ERROR;
    }

  /* this was already read by using MSG_PEEK - so it shouldn't fail */
  sum = 0;
  do
    {				/* we need this to finish now */
      ret = _gnutls_read (session, peekdata, RCVLOWAT - sum, 0);
      if (ret > 0)
	sum += ret;
    }
  while (ret == GNUTLS_E_INTERRUPTED || ret == GNUTLS_E_AGAIN
	 || sum < RCVLOWAT);

  gnutls_afree (peekdata);

  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  session->internals.have_peeked_data = 0;

  return 0;
}


void
_gnutls_io_clear_read_buffer (gnutls_session_t session)
{
  session->internals.record_recv_buffer.length = 0;
}

/* This function is like recv(with MSG_PEEK). But it does not return -1 on error.
 * It does return gnutls_errno instead.
 * This function reads data from the socket and keeps them in a buffer, of up to
 * MAX_RECV_SIZE. 
 *
 * This is not a general purpose function. It returns EXACTLY the data requested,
 * which are stored in a local (in the session) buffer. A pointer (iptr) to this buffer is returned.
 *
 */
ssize_t
_gnutls_io_read_buffered (gnutls_session_t session, opaque ** iptr,
			  size_t sizeOfPtr, content_type_t recv_type)
{
  ssize_t ret = 0, ret2 = 0;
  size_t min;
  int buf_pos;
  opaque *buf;
  int recvlowat;
  int recvdata, alloc_size;

  *iptr = session->internals.record_recv_buffer.data;

  if (sizeOfPtr > MAX_RECV_SIZE || sizeOfPtr == 0)
    {
      gnutls_assert ();		/* internal error */
      return GNUTLS_E_INVALID_REQUEST;
    }

  /* If an external pull function is used, then do not leave
   * any data into the kernel buffer.
   */
  if (session->internals._gnutls_pull_func != NULL)
    {
      recvlowat = 0;
    }
  else
    {
      /* leave peeked data to the kernel space only if application data
       * is received and we don't have any peeked 
       * data in gnutls session.
       */
      if (recv_type != GNUTLS_APPLICATION_DATA
	  && session->internals.have_peeked_data == 0)
	recvlowat = 0;
      else
	recvlowat = RCVLOWAT;
    }



  /* calculate the actual size, ie. get the minimum of the
   * buffered data and the requested data.
   */
  min = MIN (session->internals.record_recv_buffer.length, sizeOfPtr);
  if (min > 0)
    {
      /* if we have enough buffered data
       * then just return them.
       */
      if (min == sizeOfPtr)
	{
	  return min;
	}
    }

  /* min is over zero. recvdata is the data we must
   * receive in order to return the requested data.
   */
  recvdata = sizeOfPtr - min;

  /* Check if the previously read data plus the new data to
   * receive are longer than the maximum receive buffer size.
   */
  if ((session->internals.record_recv_buffer.length + recvdata) >
      MAX_RECV_SIZE)
    {
      gnutls_assert ();		/* internal error */
      return GNUTLS_E_INVALID_REQUEST;
    }

  /* Allocate the data required to store the new packet.
   */
  alloc_size = recvdata + session->internals.record_recv_buffer.length;
  session->internals.record_recv_buffer.data =
    gnutls_realloc_fast (session->internals.record_recv_buffer.data,
			 alloc_size);
  if (session->internals.record_recv_buffer.data == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_MEMORY_ERROR;
    }

  buf_pos = session->internals.record_recv_buffer.length;
  buf = session->internals.record_recv_buffer.data;
  *iptr = buf;

  /* READ DATA - but leave RCVLOWAT bytes in the kernel buffer.
   */
  if (recvdata - recvlowat > 0)
    {
      ret = _gnutls_read (session, &buf[buf_pos], recvdata - recvlowat, 0);

      /* return immediately if we got an interrupt or eagain
       * error.
       */
      if (ret < 0 && gnutls_error_is_fatal (ret) == 0)
	{
	  return ret;
	}
    }

  /* copy fresh data to our buffer.
   */
  if (ret > 0)
    {
      _gnutls_read_log
	("RB: Have %d bytes into buffer. Adding %d bytes.\n",
	 session->internals.record_recv_buffer.length, ret);
      _gnutls_read_log ("RB: Requested %d bytes\n", sizeOfPtr);
      session->internals.record_recv_buffer.length += ret;
    }

  buf_pos = session->internals.record_recv_buffer.length;

  /* This is hack in order for select to work. Just leave recvlowat data,
   * into the kernel buffer (using a read with MSG_PEEK), thus making
   * select think, that the socket is ready for reading.
   * MSG_PEEK is only used with berkeley style sockets.
   */
  if (ret == (recvdata - recvlowat) && recvlowat > 0)
    {
      ret2 = _gnutls_read (session, &buf[buf_pos], recvlowat, MSG_PEEK);

      if (ret2 < 0 && gnutls_error_is_fatal (ret2) == 0)
	{
	  return ret2;
	}

      if (ret2 > 0)
	{
	  _gnutls_read_log ("RB-PEEK: Read %d bytes in PEEK MODE.\n", ret2);
	  _gnutls_read_log
	    ("RB-PEEK: Have %d bytes into buffer. Adding %d bytes.\nRB: Requested %d bytes\n",
	     session->internals.record_recv_buffer.length, ret2, sizeOfPtr);
	  session->internals.have_peeked_data = 1;
	  session->internals.record_recv_buffer.length += ret2;

	}
    }

  if (ret < 0 || ret2 < 0)
    {
      gnutls_assert ();
      /* that's because they are initialized to 0 */
      return MIN (ret, ret2);
    }

  ret += ret2;

  if (ret > 0 && ret < recvlowat)
    {
      gnutls_assert ();
      return GNUTLS_E_AGAIN;
    }

  if (ret == 0)
    {				/* EOF */
      gnutls_assert ();
      return 0;
    }

  ret = session->internals.record_recv_buffer.length;

  if ((ret > 0) && ((size_t) ret < sizeOfPtr))
    {
      /* Short Read */
      gnutls_assert ();
      return GNUTLS_E_AGAIN;
    }
  else
    {
      return ret;
    }
}


/* These two functions are used to insert data to the send buffer of the handshake or
 * record protocol. The send buffer is kept if a send is interrupted and we need to keep
 * the data left to sent, in order to send them later.
 */

#define MEMSUB(x,y) ((ssize_t)((ptrdiff_t)x-(ptrdiff_t)y))

inline static int
_gnutls_buffer_insert (gnutls_buffer * buffer,
		       const opaque * _data, size_t data_size)
{

  if ((MEMSUB (_data, buffer->data) >= 0)
      && (MEMSUB (_data, buffer->data) < (ssize_t) buffer->length))
    {
      /* the given _data is part of the buffer.
       */
      if (data_size > buffer->length)
	{
	  gnutls_assert ();
	  /* this shouldn't have happened */
	  return GNUTLS_E_INTERNAL_ERROR;
	}

      if (_data == buffer->data)
	{			/* then don't even memmove */
	  buffer->length = data_size;
	  return 0;
	}

      memmove (buffer->data, _data, data_size);
      buffer->length = data_size;

      return 0;

    }

  if (_gnutls_buffer_append (buffer, _data, data_size) < 0)
    {
      gnutls_assert ();
      return GNUTLS_E_MEMORY_ERROR;
    }

  return 0;
}

inline static int
_gnutls_buffer_get (gnutls_buffer * buffer,
		    const opaque ** ptr, size_t * ptr_size)
{
  *ptr_size = buffer->length;
  *ptr = buffer->data;

  return 0;
}


/* This function is like write. But it does not return -1 on error.
 * It does return gnutls_errno instead.
 *
 * In case of E_AGAIN and E_INTERRUPTED errors, you must call gnutls_write_flush(),
 * until it returns ok (0).
 *
 * We need to push exactly the data in n, since we cannot send less
 * data. In TLS the peer must receive the whole packet in order
 * to decrypt and verify the integrity. 
 *
 */
ssize_t
_gnutls_io_write_buffered (gnutls_session_t session,
			   const void *iptr, size_t n)
{
  size_t left;
  uint j, x, sum = 0;
  ssize_t retval, i;
  const opaque *ptr;
  int ret;
  gnutls_transport_ptr_t fd = session->internals.transport_send_ptr;

  /* to know where the procedure was interrupted.
   */
  session->internals.direction = 1;

  ptr = iptr;

  /* In case the previous write was interrupted, check if the
   * iptr != NULL and we have data in the buffer.
   * If this is true then return an error.
   */
  if (session->internals.record_send_buffer.length > 0 && iptr != NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  /* If data in the buffer exist
   */
  if (iptr == NULL)
    {
      /* checking is handled above */
      ret =
	_gnutls_buffer_get (&session->internals.record_send_buffer, &ptr, &n);
      if (ret < 0)
	{
	  gnutls_assert ();
	  return ret;
	}

      _gnutls_write_log
	("WRITE: Restoring old write. (%d bytes to send)\n", n);
    }

  _gnutls_write_log ("WRITE: Will write %d bytes to %d.\n", n, fd);

  i = 0;
  left = n;
  while (left > 0)
    {

      if (session->internals._gnutls_push_func == NULL)
	i = send ((int) fd, &ptr[n - left], left, 0);
      else
	i = session->internals._gnutls_push_func (fd, &ptr[n - left], left);

      if (i == -1)
	{
	  if (errno == EAGAIN || errno == EINTR)
	    {
	      session->internals.record_send_buffer_prev_size += n - left;

	      retval =
		_gnutls_buffer_insert (&session->internals.
				       record_send_buffer,
				       &ptr[n - left], left);
	      if (retval < 0)
		{
		  gnutls_assert ();
		  return retval;
		}

	      _gnutls_write_log
		("WRITE: Interrupted. Stored %d bytes to buffer. Already sent %d bytes.\n",
		 left, n - left);

	      retval = RET (errno);

	      return retval;
	    }
	  else
	    {
	      gnutls_assert ();
	      return GNUTLS_E_PUSH_ERROR;
	    }
	}
      left -= i;


      if (_gnutls_log_level >= 7)
	{
	  char line[128];
	  char tmp[16];


	  _gnutls_write_log
	    ("WRITE: wrote %d bytes to %d. Left %d bytes. Total %d bytes.\n",
	     i, fd, left, n);
	  for (x = 0; x < (uint) ((i) / 16) + 1; x++)
	    {
	      line[0] = 0;

	      if (sum > n - left)
		break;

	      sprintf (tmp, "%.4x - ", x);
	      _gnutls_str_cat (line, sizeof (line), tmp);

	      for (j = 0; j < 16; j++)
		{
		  if (sum < n - left)
		    {
		      sprintf (tmp, "%.2x ", ((unsigned char *) ptr)[sum++]);
		      _gnutls_str_cat (line, sizeof (line), tmp);
		    }
		  else
		    break;
		}
	      _gnutls_write_log ("%s\n", line);
	    }
	}
    }

  retval = n + session->internals.record_send_buffer_prev_size;

  session->internals.record_send_buffer.length = 0;
  session->internals.record_send_buffer_prev_size = 0;

  return retval;

}

/* This is exactly like write_buffered, but will use two buffers to read
 * from.
 */
ssize_t
_gnutls_io_write_buffered2 (gnutls_session_t session,
			    const void *iptr, size_t n,
			    const void *iptr2, size_t n2)
{

  if (n == 0)
    {
      return _gnutls_io_write_buffered (session, iptr2, n2);
    }
  else
    {
      opaque *sptr;
      ssize_t ret;

      sptr = gnutls_alloca (n + n2);
      if (sptr == NULL)
	{
	  gnutls_assert ();
	  return GNUTLS_E_MEMORY_ERROR;
	}

      memcpy (sptr, iptr, n);
      memcpy (&sptr[n], iptr2, n2);

      ret = _gnutls_io_write_buffered (session, sptr, n + n2);
      gnutls_afree (sptr);

      return ret;
    }
}


/* This function writes the data that are left in the
 * TLS write buffer (ie. because the previous write was
 * interrupted.
 */
ssize_t
_gnutls_io_write_flush (gnutls_session_t session)
{
  ssize_t ret;

  if (session->internals.record_send_buffer.length == 0)
    return 0;			/* done */

  ret = _gnutls_io_write_buffered (session, NULL, 0);
  _gnutls_write_log ("WRITE FLUSH: %d [buffer: %d]\n", ret,
		     session->internals.record_send_buffer.length);

  return ret;
}

/* This function writes the data that are left in the
 * Handshake write buffer (ie. because the previous write was
 * interrupted.
 */
ssize_t
_gnutls_handshake_io_write_flush (gnutls_session_t session)
{
  ssize_t ret;
  ret = _gnutls_handshake_io_send_int (session, 0, 0, NULL, 0);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  _gnutls_write_log ("HANDSHAKE_FLUSH: written[1] %d bytes\n", ret);

  if (session->internals.handshake_send_buffer.length == 0)
    {
      ret = session->internals.handshake_send_buffer_prev_size;	/* done */
      session->internals.handshake_send_buffer_prev_size = 0;
    }

  return ret;
}


/* This is a send function for the gnutls handshake 
 * protocol. Just makes sure that all data have been sent.
 */
ssize_t
_gnutls_handshake_io_send_int (gnutls_session_t session,
			       content_type_t type,
			       gnutls_handshake_description_t htype,
			       const void *iptr, size_t n)
{
  size_t left;
  ssize_t ret = 0;
  const opaque *ptr;
  ssize_t retval = 0;

  ptr = iptr;

  if (session->internals.handshake_send_buffer.length > 0 && ptr == NULL
      && n == 0)
    {
      /* resuming previously interrupted write
       */
      gnutls_assert ();
      ret =
	_gnutls_buffer_get (&session->internals.handshake_send_buffer,
			    &ptr, &n);
      if (ret < 0)
	{
	  gnutls_assert ();
	  return retval;
	}

      type = session->internals.handshake_send_buffer_type;
      htype = session->internals.handshake_send_buffer_htype;

    }
  else if (session->internals.handshake_send_buffer.length > 0)
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }
#ifdef WRITE_DEBUG
  else
    {
      size_t sum = 0, x, j;

      _gnutls_write_log ("HWRITE: will write %d bytes to %d.\n", n,
			 gnutls_transport_get_ptr (session));
      for (x = 0; x < ((n) / 16) + 1; x++)
	{
	  if (sum > n)
	    break;

	  _gnutls_write_log ("%.4x - ", x);
	  for (j = 0; j < 16; j++)
	    {
	      if (sum < n)
		{
		  _gnutls_write_log ("%.2x ", ((unsigned char *) ptr)[sum++]);
		}
	      else
		break;
	    }
	  _gnutls_write_log ("\n");
	}
      _gnutls_write_log ("\n");
    }
#endif

  if (n == 0)
    {				/* if we have no data to send */
      gnutls_assert ();
      return 0;
    }
  else if (ptr == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }


  left = n;
  while (left > 0)
    {
      ret = _gnutls_send_int (session, type, htype, &ptr[n - left], left);

      if (ret <= 0)
	{
	  if (ret == 0)
	    {
	      gnutls_assert ();
	      ret = GNUTLS_E_INTERNAL_ERROR;
	    }

	  if (left > 0
	      && (ret == GNUTLS_E_INTERRUPTED || ret == GNUTLS_E_AGAIN))
	    {
	      gnutls_assert ();

	      retval =
		_gnutls_buffer_insert (&session->internals.
				       handshake_send_buffer,
				       &ptr[n - left], left);
	      if (retval < 0)
		{
		  gnutls_assert ();
		  return retval;
		}

	      session->internals.handshake_send_buffer_prev_size += n - left;

	      session->internals.handshake_send_buffer_type = type;
	      session->internals.handshake_send_buffer_htype = htype;

	    }
	  else
	    {
	      session->internals.handshake_send_buffer_prev_size = 0;
	      session->internals.handshake_send_buffer.length = 0;
	    }

	  gnutls_assert ();
	  return ret;
	}
      left -= ret;
    }

  retval = n + session->internals.handshake_send_buffer_prev_size;

  session->internals.handshake_send_buffer.length = 0;
  session->internals.handshake_send_buffer_prev_size = 0;

  return retval;

}

/* This is a receive function for the gnutls handshake 
 * protocol. Makes sure that we have received all data.
 */
ssize_t
_gnutls_handshake_io_recv_int (gnutls_session_t session,
			       content_type_t type,
			       gnutls_handshake_description_t htype,
			       void *iptr, size_t sizeOfPtr)
{
  size_t left;
  ssize_t i;
  opaque *ptr;
  size_t dsize;

  ptr = iptr;
  left = sizeOfPtr;

  if (sizeOfPtr == 0 || iptr == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  if (session->internals.handshake_recv_buffer.length > 0)
    {
      /* if we have already received some data */
      if (sizeOfPtr <= session->internals.handshake_recv_buffer.length)
	{
	  /* if requested less data then return it.
	   */
	  gnutls_assert ();
	  memcpy (iptr, session->internals.handshake_recv_buffer.data,
		  sizeOfPtr);

	  session->internals.handshake_recv_buffer.length -= sizeOfPtr;

	  memmove (session->internals.handshake_recv_buffer.data,
		   &session->internals.handshake_recv_buffer.
		   data[sizeOfPtr],
		   session->internals.handshake_recv_buffer.length);

	  return sizeOfPtr;
	}
      gnutls_assert ();
      memcpy (iptr, session->internals.handshake_recv_buffer.data,
	      session->internals.handshake_recv_buffer.length);

      htype = session->internals.handshake_recv_buffer_htype;
      type = session->internals.handshake_recv_buffer_type;

      left -= session->internals.handshake_recv_buffer.length;

      session->internals.handshake_recv_buffer.length = 0;
    }

  while (left > 0)
    {
      dsize = sizeOfPtr - left;
      i = _gnutls_recv_int (session, type, htype, &ptr[dsize], left);
      if (i < 0)
	{

	  if (dsize > 0 && (i == GNUTLS_E_INTERRUPTED || i == GNUTLS_E_AGAIN))
	    {
	      gnutls_assert ();

	      session->internals.handshake_recv_buffer.data =
		gnutls_realloc_fast (session->internals.
				     handshake_recv_buffer.data, dsize);
	      if (session->internals.handshake_recv_buffer.data == NULL)
		{
		  gnutls_assert ();
		  return GNUTLS_E_MEMORY_ERROR;
		}

	      memcpy (session->internals.handshake_recv_buffer.data, iptr,
		      dsize);

	      session->internals.handshake_recv_buffer_htype = htype;
	      session->internals.handshake_recv_buffer_type = type;

	      session->internals.handshake_recv_buffer.length = dsize;
	    }
	  else
	    session->internals.handshake_recv_buffer.length = 0;

	  gnutls_assert ();

	  return i;
	}
      else
	{
	  if (i == 0)
	    break;		/* EOF */
	}

      left -= i;

    }

  session->internals.handshake_recv_buffer.length = 0;

  return sizeOfPtr - left;
}

/* Buffer for handshake packets. Keeps the packets in order
 * for finished messages to use them. Used in HMAC calculation
 * and finished messages.
 */
int
_gnutls_handshake_buffer_put (gnutls_session_t session, opaque * data,
			      size_t length)
{

  if (length == 0)
    return 0;

  if ((session->internals.max_handshake_data_buffer_size > 0) &&
      ((length + session->internals.handshake_hash_buffer.length) >
       session->internals.max_handshake_data_buffer_size))
    {
      gnutls_assert ();
      return GNUTLS_E_MEMORY_ERROR;
    }

  _gnutls_buffers_log ("BUF[HSK]: Inserted %d bytes of Data\n", length);

  if (_gnutls_buffer_append (&session->internals.handshake_hash_buffer,
			     data, length) < 0)
    {
      gnutls_assert ();
      return GNUTLS_E_MEMORY_ERROR;
    }

  return 0;
}

int
_gnutls_handshake_buffer_get_size (gnutls_session_t session)
{

  return session->internals.handshake_hash_buffer.length;
}

/* this function does not touch the buffer
 * and returns data from it (peek mode!)
 */
int
_gnutls_handshake_buffer_peek (gnutls_session_t session, opaque * data,
			       size_t length)
{
  if (length > session->internals.handshake_hash_buffer.length)
    {
      length = session->internals.handshake_hash_buffer.length;
    }

  _gnutls_buffers_log ("BUF[HSK]: Peeked %d bytes of Data\n", length);

  memcpy (data, session->internals.handshake_hash_buffer.data, length);
  return length;
}

/* this function does not touch the buffer
 * and returns data from it (peek mode!)
 */
int
_gnutls_handshake_buffer_get_ptr (gnutls_session_t session,
				  opaque ** data_ptr, size_t * length)
{
  if (length != NULL)
    *length = session->internals.handshake_hash_buffer.length;

  _gnutls_buffers_log ("BUF[HSK]: Peeked %d bytes of Data\n", *length);

  if (data_ptr != NULL)
    *data_ptr = session->internals.handshake_hash_buffer.data;

  return 0;
}

/* Does not free the buffer
 */
int
_gnutls_handshake_buffer_empty (gnutls_session_t session)
{

  _gnutls_buffers_log ("BUF[HSK]: Emptied buffer\n");

  session->internals.handshake_hash_buffer.length = 0;

  return 0;
}


int
_gnutls_handshake_buffer_clear (gnutls_session_t session)
{

  _gnutls_buffers_log ("BUF[HSK]: Cleared Data from buffer\n");

  _gnutls_buffer_clear (&session->internals.handshake_hash_buffer);

  return 0;
}
