/*
 * nghttp2 - HTTP/2.0 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "nghttp2_helper.h"

#include <assert.h>
#include <string.h>

#include "nghttp2_net.h"

void nghttp2_put_uint16be(uint8_t *buf, uint16_t n)
{
  uint16_t x = htons(n);
  memcpy(buf, &x, sizeof(uint16_t));
}

void nghttp2_put_uint32be(uint8_t *buf, uint32_t n)
{
  uint32_t x = htonl(n);
  memcpy(buf, &x, sizeof(uint32_t));
}

uint16_t nghttp2_get_uint16(const uint8_t *data)
{
  uint16_t n;
  memcpy(&n, data, sizeof(uint16_t));
  return ntohs(n);
}

uint32_t nghttp2_get_uint32(const uint8_t *data)
{
  uint32_t n;
  memcpy(&n, data, sizeof(uint32_t));
  return ntohl(n);
}

int nghttp2_reserve_buffer(uint8_t **buf_ptr, size_t *buflen_ptr,
                           size_t min_length)
{
  if(min_length > *buflen_ptr) {
    uint8_t *temp;
    min_length = (min_length+4095)/4096*4096;
    temp = realloc(*buf_ptr, min_length);
    if(temp == NULL) {
      return NGHTTP2_ERR_NOMEM;
    } else {
      *buf_ptr = temp;
      *buflen_ptr = min_length;
    }
  }
  return 0;
}

void* nghttp2_memdup(const void* src, size_t n)
{
  void* dest = malloc(n);
  if(dest == NULL) {
    return NULL;
  }
  memcpy(dest, src, n);
  return dest;
}

void nghttp2_downcase(uint8_t *s, size_t len)
{
  size_t i;
  for(i = 0; i < len; ++i) {
    if('A' <= s[i] && s[i] <= 'Z') {
      s[i] += 'a'-'A';
    }
  }
}

int nghttp2_adjust_local_window_size(int32_t *local_window_size_ptr,
                                     int32_t *recv_window_size_ptr,
                                     int32_t *recv_reduction_ptr,
                                     int32_t *delta_ptr)
{
  if(*delta_ptr > 0) {
    int32_t new_recv_window_size =
      nghttp2_max(0, *recv_window_size_ptr) - *delta_ptr;
    if(new_recv_window_size < 0) {
      /* The delta size is strictly more than received bytes. Increase
         local_window_size by that difference. */
      int32_t recv_reduction_diff;
      if(*local_window_size_ptr >
         NGHTTP2_MAX_WINDOW_SIZE + new_recv_window_size) {
        return NGHTTP2_ERR_FLOW_CONTROL;
      }
      *local_window_size_ptr -= new_recv_window_size;
      /* If there is recv_reduction due to earlier window_size
         reduction, we have to adjust it too. */
      recv_reduction_diff = nghttp2_min(*recv_reduction_ptr,
                                        -new_recv_window_size);
      *recv_reduction_ptr -= recv_reduction_diff;
      if(*recv_window_size_ptr < 0) {
        *recv_window_size_ptr += recv_reduction_diff;
      } else {
        /* If *recv_window_size_ptr > 0, then those bytes are
           considered to be backed to the remote peer (by
           WINDOW_UPDATE with the adjusted *delta_ptr), so it is
           effectively 0 now. */
        *recv_window_size_ptr = recv_reduction_diff;
      }
      /* recv_reduction_diff must be paied from *delta_ptr, since it
         was added in window size reduction (see below). */
      *delta_ptr -= recv_reduction_diff;
    } else {
      *recv_window_size_ptr = new_recv_window_size;
    }
    return 0;
  } else {
    if(*local_window_size_ptr + *delta_ptr < 0 ||
       *recv_window_size_ptr < INT32_MIN - *delta_ptr ||
       *recv_reduction_ptr > INT32_MAX + *delta_ptr) {
      return NGHTTP2_ERR_FLOW_CONTROL;
    }
    /* Decreasing local window size. Note that we achieve this without
       noticing to the remote peer. To do this, we cut
       recv_window_size by -delta. This means that we don't send
       WINDOW_UPDATE for -delta bytes. */
    *local_window_size_ptr += *delta_ptr;
    *recv_window_size_ptr += *delta_ptr;
    *recv_reduction_ptr -= *delta_ptr;
    *delta_ptr = 0;
  }
  return 0;
}

int nghttp2_should_send_window_update(int32_t local_window_size,
                                      int32_t recv_window_size)
{
  return recv_window_size >= local_window_size / 2;
}

static int VALID_HD_NAME_CHARS[] = {
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 1 /* ! */,
 -1,
 1 /* # */, 1 /* $ */, 1 /* % */, 1 /* & */, 1 /* ' */,
 -1, -1,
 1 /* * */, 1 /* + */,
 -1,
 1 /* - */, 1 /* . */,
 -1,
 1 /* 0 */, 1 /* 1 */, 1 /* 2 */, 1 /* 3 */, 1 /* 4 */, 1 /* 5 */,
 1 /* 6 */, 1 /* 7 */, 1 /* 8 */, 1 /* 9 */,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 1 /* ^ */, 1 /* _ */, 1 /* ` */, 1 /* a */, 1 /* b */, 1 /* c */, 1 /* d */,
 1 /* e */, 1 /* f */, 1 /* g */, 1 /* h */, 1 /* i */, 1 /* j */, 1 /* k */,
 1 /* l */, 1 /* m */, 1 /* n */, 1 /* o */, 1 /* p */, 1 /* q */, 1 /* r */,
 1 /* s */, 1 /* t */, 1 /* u */, 1 /* v */, 1 /* w */, 1 /* x */, 1 /* y */,
 1 /* z */,
 -1,
 1 /* | */,
 -1,
 1 /* ~ */,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static int check_header_name(const uint8_t *name, size_t len, int nocase)
{
  const uint8_t *last;
  if(len == 0) {
    return 0;
  }
  if(*name == ':') {
    if(len == 1) {
      return 0;
    }
    ++name;
    --len;
  }
  for(last = name + len; name != last; ++name) {
    if(nocase && 'A' <= *name && *name <= 'Z') continue;
    if(VALID_HD_NAME_CHARS[*name] == -1) {
      return 0;
    }
  }
  return 1;
}

int nghttp2_check_header_name(const uint8_t *name, size_t len)
{
  return check_header_name(name, len, 0);
}

int nghttp2_check_header_name_nocase(const uint8_t *name, size_t len)
{
  return check_header_name(name, len, 1);
}

int nghttp2_check_header_value(const uint8_t* value, size_t len)
{
  size_t i;
  for(i = 0; i < len; ++i) {
    /* Only allow NUL or ASCII range [0x20, 0x7e], inclusive, to match
       HTTP/1 sematics */
    if(value[i] != '\0' && (0x20u > value[i] || value[i] > 0x7eu)) {
      return 0;
    }
  }
  return 1;
}

const char* nghttp2_strerror(int error_code)
{
  switch(error_code) {
  case 0:
    return "Success";
  case NGHTTP2_ERR_INVALID_ARGUMENT:
    return "Invalid argument";
  case NGHTTP2_ERR_UNSUPPORTED_VERSION:
    return "Unsupported SPDY version";
  case NGHTTP2_ERR_WOULDBLOCK:
    return "Operation would block";
  case NGHTTP2_ERR_PROTO:
    return "Protocol error";
  case NGHTTP2_ERR_INVALID_FRAME:
    return "Invalid frame octets";
  case NGHTTP2_ERR_EOF:
    return "EOF";
  case NGHTTP2_ERR_DEFERRED:
    return "Data transfer deferred";
  case NGHTTP2_ERR_STREAM_ID_NOT_AVAILABLE:
    return "No more Stream ID available";
  case NGHTTP2_ERR_STREAM_CLOSED:
    return "Stream was already closed or invalid";
  case NGHTTP2_ERR_STREAM_CLOSING:
    return "Stream is closing";
  case NGHTTP2_ERR_STREAM_SHUT_WR:
    return "The transmission is not allowed for this stream";
  case NGHTTP2_ERR_INVALID_STREAM_ID:
    return "Stream ID is invalid";
  case NGHTTP2_ERR_INVALID_STREAM_STATE:
    return "Invalid stream state";
  case NGHTTP2_ERR_DEFERRED_DATA_EXIST:
    return "Another DATA frame has already been deferred";
  case NGHTTP2_ERR_START_STREAM_NOT_ALLOWED:
    return "request HEADERS is not allowed";
  case NGHTTP2_ERR_GOAWAY_ALREADY_SENT:
    return "GOAWAY has already been sent";
  case NGHTTP2_ERR_INVALID_HEADER_BLOCK:
    return "Invalid header block";
  case NGHTTP2_ERR_INVALID_STATE:
    return "Invalid state";
  case NGHTTP2_ERR_GZIP:
    return "Gzip error";
  case NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE:
    return "The user callback function failed due to the temporal error";
  case NGHTTP2_ERR_FRAME_SIZE_ERROR:
    return "The length of the frame is invalid";
  case NGHTTP2_ERR_HEADER_COMP:
    return "Header compression/decompression error";
  case NGHTTP2_ERR_NOMEM:
    return "Out of memory";
  case NGHTTP2_ERR_CALLBACK_FAILURE:
    return "The user callback function failed";
  default:
    return "Unknown error code";
  }
}

void nghttp2_free(void *ptr)
{
  free(ptr);
}
