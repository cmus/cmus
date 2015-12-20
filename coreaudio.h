#include <sys/types.h>
/*
  Adapted from the Jack project.
  Copyright (C) 2000 Paul Davis
  Copyright (C) 2003 Rohan Drape
    
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.
    
  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, write to the Free Software 
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
    
  ISO/POSIX C version of Paul Davis's lock free ringbuffer C++ code.
  This is safe for the case of one read thread and one write thread.
*/

typedef struct  
{
  char  *buf;
  size_t len;
} 
coreaudio_ringbuffer_data_t ;

typedef struct
{
  char		 *buf;
  volatile size_t write_ptr;
  volatile size_t read_ptr;
  size_t	  size;
  size_t	  size_mask;
  int		  mlocked;
} 
coreaudio_ringbuffer_t ;

/**
 * Allocates a ringbuffer data structure of a specified size. The
 * caller must arrange for a call to coreaudio_ringbuffer_free() to release
 * the memory associated with the ringbuffer.
 *
 * @param sz the ringbuffer size in bytes.
 *
 * @return a pointer to a new coreaudio_ringbuffer_t, if successful; NULL
 * otherwise.
 */
inline coreaudio_ringbuffer_t *coreaudio_ringbuffer_create(size_t sz);

/**
 * Frees the ringbuffer data structure allocated by an earlier call to
 * coreaudio_ringbuffer_create().
 *
 * @param rb a pointer to the ringbuffer structure.
 */
inline void coreaudio_ringbuffer_free(coreaudio_ringbuffer_t *rb);

/**
 * Fill a data structure with a description of the current readable
 * data held in the ringbuffer.  This description is returned in a two
 * element array of coreaudio_ringbuffer_data_t.  Two elements are needed
 * because the data to be read may be split across the end of the
 * ringbuffer.
 *
 * The first element will always contain a valid @a len field, which
 * may be zero or greater.  If the @a len field is non-zero, then data
 * can be read in a contiguous fashion using the address given in the
 * corresponding @a buf field.
 *
 * If the second element has a non-zero @a len field, then a second
 * contiguous stretch of data can be read from the address given in
 * its corresponding @a buf field.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param vec a pointer to a 2 element array of coreaudio_ringbuffer_data_t.
 *
 */
inline void coreaudio_ringbuffer_get_read_vector(const coreaudio_ringbuffer_t *rb,
				     coreaudio_ringbuffer_data_t *vec);

/**
 * Fill a data structure with a description of the current writable
 * space in the ringbuffer.  The description is returned in a two
 * element array of coreaudio_ringbuffer_data_t.  Two elements are needed
 * because the space available for writing may be split across the end
 * of the ringbuffer.
 *
 * The first element will always contain a valid @a len field, which
 * may be zero or greater.  If the @a len field is non-zero, then data
 * can be written in a contiguous fashion using the address given in
 * the corresponding @a buf field.
 *
 * If the second element has a non-zero @a len field, then a second
 * contiguous stretch of data can be written to the address given in
 * the corresponding @a buf field.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param vec a pointer to a 2 element array of coreaudio_ringbuffer_data_t.
 */
inline void coreaudio_ringbuffer_get_write_vector(const coreaudio_ringbuffer_t *rb,
				      coreaudio_ringbuffer_data_t *vec);

/**
 * Read data from the ringbuffer.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param dest a pointer to a buffer where data read from the
 * ringbuffer will go.
 * @param cnt the number of bytes to read.
 *
 * @return the number of bytes read, which may range from 0 to cnt.
 */
inline size_t coreaudio_ringbuffer_read(coreaudio_ringbuffer_t *rb, char *dest, size_t cnt);

/**
 * Read data from the ringbuffer. Opposed to coreaudio_ringbuffer_read()
 * this function does not move the read pointer. Thus it's
 * a convenient way to inspect data in the ringbuffer in a
 * continous fashion. The price is that the data is copied
 * into a user provided buffer. For "raw" non-copy inspection
 * of the data in the ringbuffer use coreaudio_ringbuffer_get_read_vector().
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param dest a pointer to a buffer where data read from the
 * ringbuffer will go.
 * @param cnt the number of bytes to read.
 *
 * @return the number of bytes read, which may range from 0 to cnt.
 */
inline size_t coreaudio_ringbuffer_peek(coreaudio_ringbuffer_t *rb, char *dest, size_t cnt);

/**
 * Advance the read pointer.
 *
 * After data have been read from the ringbuffer using the pointers
 * returned by coreaudio_ringbuffer_get_read_vector(), use this function to
 * advance the buffer pointers, making that space available for future
 * write operations.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param cnt the number of bytes read.
 */
inline void coreaudio_ringbuffer_read_advance(coreaudio_ringbuffer_t *rb, size_t cnt);

/**
 * Return the number of bytes available for reading.
 *
 * @param rb a pointer to the ringbuffer structure.
 *
 * @return the number of bytes available to read.
 */
inline size_t coreaudio_ringbuffer_read_space(const coreaudio_ringbuffer_t *rb);

/**
 * Lock a ringbuffer data block into memory.
 *
 * Uses the mlock() system call.  This is not a realtime operation.
 *
 * @param rb a pointer to the ringbuffer structure.
 */
inline int coreaudio_ringbuffer_mlock(coreaudio_ringbuffer_t *rb);

/**
 * Reset the read and write pointers, making an empty buffer.
 *
 * This is not thread safe.
 *
 * @param rb a pointer to the ringbuffer structure.
 */
inline void coreaudio_ringbuffer_reset(coreaudio_ringbuffer_t *rb);

/**
 * Write data into the ringbuffer.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param src a pointer to the data to be written to the ringbuffer.
 * @param cnt the number of bytes to write.
 *
 * @return the number of bytes write, which may range from 0 to cnt
 */
inline size_t coreaudio_ringbuffer_write(coreaudio_ringbuffer_t *rb, const char *src,
			     size_t cnt);

/**
 * Advance the write pointer.
 *
 * After data have been written the ringbuffer using the pointers
 * returned by coreaudio_ringbuffer_get_write_vector(), use this function
 * to advance the buffer pointer, making the data available for future
 * read operations.
 *
 * @param rb a pointer to the ringbuffer structure.
 * @param cnt the number of bytes written.
 */
inline void coreaudio_ringbuffer_write_advance(coreaudio_ringbuffer_t *rb, size_t cnt);

/**
 * Return the number of bytes available for writing.
 *
 * @param rb a pointer to the ringbuffer structure.
 *
 * @return the amount of free space (in bytes) available for writing.
 */
inline size_t coreaudio_ringbuffer_write_space(const coreaudio_ringbuffer_t *rb);



#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* Create a new ringbuffer to hold at least `sz' bytes of data. The
   actual buffer size is rounded up to the next power of two.  */

inline coreaudio_ringbuffer_t *
coreaudio_ringbuffer_create (size_t sz)
{
	int power_of_two;
	coreaudio_ringbuffer_t *rb;
	
	if ((rb = malloc (sizeof (coreaudio_ringbuffer_t))) == NULL) {
		return NULL;
	}
	
	for (power_of_two = 1; 1 << power_of_two < sz; power_of_two++);
	
	rb->size = 1 << power_of_two;
	rb->size_mask = rb->size;
	rb->size_mask -= 1;
	rb->write_ptr = 0;
	rb->read_ptr = 0;
	if ((rb->buf = malloc (rb->size)) == NULL) {
		free (rb);
		return NULL;
	}
	rb->mlocked = 0;
	
	return rb;
}

/* Free all data associated with the ringbuffer `rb'. */

inline void
coreaudio_ringbuffer_free (coreaudio_ringbuffer_t * rb)
{
	if (rb->mlocked) {
		munlock (rb->buf, rb->size);
	}
	free (rb->buf);
	free (rb);
}

/* Lock the data block of `rb' using the system call 'mlock'.  */

inline int
coreaudio_ringbuffer_mlock (coreaudio_ringbuffer_t * rb)
{
	if (mlock (rb->buf, rb->size)) {
		return -1;
	}
	rb->mlocked = 1;
	return 0;
}

/* Reset the read and write pointers to zero. This is not thread
   safe. */

inline void
coreaudio_ringbuffer_reset (coreaudio_ringbuffer_t * rb)
{
	rb->read_ptr = 0;
	rb->write_ptr = 0;
}

/* Return the number of bytes available for reading.  This is the
   number of bytes in front of the read pointer and behind the write
   pointer.  */

inline size_t
coreaudio_ringbuffer_read_space (const coreaudio_ringbuffer_t * rb)
{
	size_t w, r;
	
	w = rb->write_ptr;
	r = rb->read_ptr;
	
	if (w > r) {
		return w - r;
	} else {
		return (w - r + rb->size) & rb->size_mask;
	}
}

/* Return the number of bytes available for writing.  This is the
   number of bytes in front of the write pointer and behind the read
   pointer.  */

inline size_t
coreaudio_ringbuffer_write_space (const coreaudio_ringbuffer_t * rb)
{
	size_t w, r;

	w = rb->write_ptr;
	r = rb->read_ptr;

	if (w > r) {
		return ((r - w + rb->size) & rb->size_mask) - 1;
	} else if (w < r) {
		return (r - w) - 1;
	} else {
		return rb->size - 1;
	}
}

/* The copying data reader.  Copy at most `cnt' bytes from `rb' to
   `dest'.  Returns the actual number of bytes copied. */

inline size_t
coreaudio_ringbuffer_read (coreaudio_ringbuffer_t * rb, char *dest, size_t cnt)
{
	size_t free_cnt;
	size_t cnt2;
	size_t to_read;
	size_t n1, n2;

	if ((free_cnt = coreaudio_ringbuffer_read_space (rb)) == 0) {
		return 0;
	}

	to_read = cnt > free_cnt ? free_cnt : cnt;

	cnt2 = rb->read_ptr + to_read;

	if (cnt2 > rb->size) {
		n1 = rb->size - rb->read_ptr;
		n2 = cnt2 & rb->size_mask;
	} else {
		n1 = to_read;
		n2 = 0;
	}

	memcpy (dest, &(rb->buf[rb->read_ptr]), n1);
	rb->read_ptr = (rb->read_ptr + n1) & rb->size_mask;

	if (n2) {
		memcpy (dest + n1, &(rb->buf[rb->read_ptr]), n2);
		rb->read_ptr = (rb->read_ptr + n2) & rb->size_mask;
	}

	return to_read;
}

/* The copying data reader w/o read pointer advance.  Copy at most 
   `cnt' bytes from `rb' to `dest'.  Returns the actual number of bytes 
   copied. */

inline size_t
coreaudio_ringbuffer_peek (coreaudio_ringbuffer_t * rb, char *dest, size_t cnt)
{
	size_t free_cnt;
	size_t cnt2;
	size_t to_read;
	size_t n1, n2;
	size_t tmp_read_ptr;

	tmp_read_ptr = rb->read_ptr;

	if ((free_cnt = coreaudio_ringbuffer_read_space (rb)) == 0) {
		return 0;
	}

	to_read = cnt > free_cnt ? free_cnt : cnt;

	cnt2 = tmp_read_ptr + to_read;

	if (cnt2 > rb->size) {
		n1 = rb->size - tmp_read_ptr;
		n2 = cnt2 & rb->size_mask;
	} else {
		n1 = to_read;
		n2 = 0;
	}

	memcpy (dest, &(rb->buf[tmp_read_ptr]), n1);
	tmp_read_ptr = (tmp_read_ptr + n1) & rb->size_mask;

	if (n2) {
		memcpy (dest + n1, &(rb->buf[tmp_read_ptr]), n2);
	}

	return to_read;
}


/* The copying data writer.  Copy at most `cnt' bytes to `rb' from
   `src'.  Returns the actual number of bytes copied. */

inline size_t
coreaudio_ringbuffer_write (coreaudio_ringbuffer_t * rb, const char *src, size_t cnt)
{
	size_t free_cnt;
	size_t cnt2;
	size_t to_write;
	size_t n1, n2;

	if ((free_cnt = coreaudio_ringbuffer_write_space (rb)) == 0) {
		return 0;
	}

	to_write = cnt > free_cnt ? free_cnt : cnt;

	cnt2 = rb->write_ptr + to_write;

	if (cnt2 > rb->size) {
		n1 = rb->size - rb->write_ptr;
		n2 = cnt2 & rb->size_mask;
	} else {
		n1 = to_write;
		n2 = 0;
	}

	memcpy (&(rb->buf[rb->write_ptr]), src, n1);
	rb->write_ptr = (rb->write_ptr + n1) & rb->size_mask;

	if (n2) {
		memcpy (&(rb->buf[rb->write_ptr]), src + n1, n2);
		rb->write_ptr = (rb->write_ptr + n2) & rb->size_mask;
	}

	return to_write;
}

/* Advance the read pointer `cnt' places. */

inline void
coreaudio_ringbuffer_read_advance (coreaudio_ringbuffer_t * rb, size_t cnt)
{
	size_t tmp = (rb->read_ptr + cnt) & rb->size_mask;
	rb->read_ptr = tmp;
}

/* Advance the write pointer `cnt' places. */

inline void
coreaudio_ringbuffer_write_advance (coreaudio_ringbuffer_t * rb, size_t cnt)
{
	size_t tmp = (rb->write_ptr + cnt) & rb->size_mask;
	rb->write_ptr = tmp;
}

/* The non-copying data reader.  `vec' is an array of two places.  Set
   the values at `vec' to hold the current readable data at `rb'.  If
   the readable data is in one segment the second segment has zero
   length.  */

inline void
coreaudio_ringbuffer_get_read_vector (const coreaudio_ringbuffer_t * rb,
				 coreaudio_ringbuffer_data_t * vec)
{
	size_t free_cnt;
	size_t cnt2;
	size_t w, r;

	w = rb->write_ptr;
	r = rb->read_ptr;

	if (w > r) {
		free_cnt = w - r;
	} else {
		free_cnt = (w - r + rb->size) & rb->size_mask;
	}

	cnt2 = r + free_cnt;

	if (cnt2 > rb->size) {

		/* Two part vector: the rest of the buffer after the current write
		   ptr, plus some from the start of the buffer. */

		vec[0].buf = &(rb->buf[r]);
		vec[0].len = rb->size - r;
		vec[1].buf = rb->buf;
		vec[1].len = cnt2 & rb->size_mask;

	} else {

		/* Single part vector: just the rest of the buffer */

		vec[0].buf = &(rb->buf[r]);
		vec[0].len = free_cnt;
		vec[1].len = 0;
	}
}

/* The non-copying data writer.  `vec' is an array of two places.  Set
   the values at `vec' to hold the current writeable data at `rb'.  If
   the writeable data is in one segment the second segment has zero
   length.  */

inline void
coreaudio_ringbuffer_get_write_vector (const coreaudio_ringbuffer_t * rb,
				  coreaudio_ringbuffer_data_t * vec)
{
	size_t free_cnt;
	size_t cnt2;
	size_t w, r;

	w = rb->write_ptr;
	r = rb->read_ptr;

	if (w > r) {
		free_cnt = ((r - w + rb->size) & rb->size_mask) - 1;
	} else if (w < r) {
		free_cnt = (r - w) - 1;
	} else {
		free_cnt = rb->size - 1;
	}

	cnt2 = w + free_cnt;

	if (cnt2 > rb->size) {

		/* Two part vector: the rest of the buffer after the current write
		   ptr, plus some from the start of the buffer. */

		vec[0].buf = &(rb->buf[w]);
		vec[0].len = rb->size - w;
		vec[1].buf = rb->buf;
		vec[1].len = cnt2 & rb->size_mask;
	} else {
		vec[0].buf = &(rb->buf[w]);
		vec[0].len = free_cnt;
		vec[1].len = 0;
	}
}

