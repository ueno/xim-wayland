/*
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Daiki Ueno
 */

#include "config.h"

#include <endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include "xim.h"

#define DEBUG 1

#define XCB_XIM_CONNECT 1
#define XCB_XIM_CONNECT_REPLY 2
#define XCB_XIM_DISCONNECT 3
#define XCB_XIM_DISCONNECT_REPLY 4

#define XCB_XIM_OPEN_REPLY 31
#define XCB_XIM_CLOSE_REPLY 33
#define XCB_XIM_REGISTER_TRIGGERKEYS 34
#define XCB_XIM_TRIGGER_NOTIFY_REPLY 36

#define XCB_XIM_SET_EVENT_MASK 37
#define XCB_XIM_ENCODING_NEGOTIATION_REPLY 39
#define XCB_XIM_QUERY_EXTENSION_REPLY 41
#define XCB_XIM_SET_IM_VALUES_REPLY 43
#define XCB_XIM_GET_IM_VALUES_REPLY 45

#define XCB_XIM_CREATE_IC_REPLY 51
#define XCB_XIM_DESTROY_IC_REPLY 53
#define XCB_XIM_SET_IC_VALUES_REPLY 55
#define XCB_XIM_GET_IC_VALUES_REPLY 57
#define XCB_XIM_SYNC_REPLY 62
#define XCB_XIM_COMMIT 63
#define XCB_XIM_RESET_IC_REPLY 65

#define XCB_XIM_GEOMETRY 70
#define XCB_XIM_STR_CONVERSION 71
#define XCB_XIM_PREEDIT_START 73
#define XCB_XIM_PREEDIT_DRAW 75
#define XCB_XIM_PREEDIT_CARET 76
#define XCB_XIM_PREEDIT_DONE 78
#define XCB_XIM_STATUS_START 79
#define XCB_XIM_STATUS_DRAW 80
#define XCB_XIM_STATUS_DONE 81
#define XCB_XIM_PREEDITSTATE 82

#define PAD(n) ((4 - ((n) % 4)) % 4)

#undef MIN
#define MIN(x,y) ((x) > (y) ? (y) : (x))

#define NO16(t,n)                               \
  ((t)->endian == 'B'                           \
   ? htobe16 ((n))                              \
   : htole16 ((n)))

#define NO32(t,n)                               \
  ((t)->endian == 'B'                           \
   ? htobe32 ((n))                              \
   : htole32 ((n)))

#define HO16(t,n)                               \
  ((t)->endian == 'B'                           \
   ? be16toh ((n))                              \
   : le16toh ((n)))

#define HO32(t,n)                               \
  ((t)->endian == 'B'                           \
   ? be32toh ((n))                              \
   : le32toh ((n)))

#define PACK8(t,d,n)                            \
  *(uint8_t *) (d) = (n);                       \
  (d)++;

#define PACK16(t,d,n)                           \
  *(uint16_t *) (d) = NO16((t),(n));            \
  (d) += 2;

#define PACK32(t,d,n)                           \
  *(uint32_t *) (d) = NO32((t),(n));            \
  (d) += 4;

#define UNPACK8(t,d,n)                          \
  *(n) = *(uint8_t *) (d);                      \
  (d) += 1;

#define UNPACK16(t,d,n)                         \
  *(n) = HO16((t),*(uint16_t *) (d));           \
  (d) += 2;

#define UNPACK32(t,d,n)                         \
  *(n) = HO32((t),*(uint32_t *) (d));           \
  (d) += 2;

#define SIZEOF(x) (sizeof (x) / sizeof(*x))

#ifdef __GNUC__
#define xcb_xim_container_of(ptr, sample, member)                       \
        (__typeof__(sample))((char *)(ptr)      -                       \
                 ((char *)&(sample)->member - (char *)(sample)))
#else
#define xcb_xim_container_of(ptr, sample, member)                       \
        (void *)((char *)(ptr)  -                                       \
                 ((char *)&(sample)->member - (char *)(sample)))
#endif

#if DEBUG
static void
hexdump (const char *prompt, const unsigned char *output, size_t outlen)
{
  int i;

  fprintf (stderr, "%s", prompt);

  for (i = 0; i < outlen; i++)
    fprintf (stderr, "%02X ", output[i]);

  fputc ('\n', stderr);
}
#else
#define hexdump(p,s,l)
#endif

enum
  {
    XIM_SERVERS,
    _XIM_XCONNECT,
    _XIM_MOREDATA,
    _XIM_PROTOCOL,
    LOCALES,
    TRANSPORT,
    LAST_ATOM
  };

static const char *atom_names[] =
  {
    "XIM_SERVERS",
    "_XIM_XCONNECT",
    "_XIM_MOREDATA",
    "_XIM_PROTOCOL",
    "LOCALES",
    "TRANSPORT",
  };

struct xcb_xim_list_t
{
  void *data;
  struct xcb_xim_list_t *next;
};

struct xcb_xim_server_connection_t
{
  xcb_connection_t *connection;
  char *locale;
  xcb_screen_t *screen;
  xcb_atom_t atoms[LAST_ATOM];

  xcb_window_t accept_window;

  xcb_xim_transport_t *clients;
  size_t nclients;
  size_t maxclients;

  struct xcb_xim_list_t *requests;
  struct xcb_xim_list_t *requests_tail;
};

static bool
init_atoms (xcb_connection_t *connection,
            xcb_atom_t *atoms,
            xcb_generic_error_t **error)
{
  xcb_intern_atom_cookie_t intern_atom_cookies[LAST_ATOM];
  xcb_intern_atom_reply_t *intern_atom_reply;
  int i;

  for (i = 0; i < SIZEOF (atom_names); i++)
    intern_atom_cookies[i] =
      xcb_intern_atom (connection,
                       0,
                       strlen (atom_names[i]),
                       atom_names[i]);

  for (i = 0; i < SIZEOF (atom_names); i++)
    {
      intern_atom_reply = xcb_intern_atom_reply (connection,
                                                 intern_atom_cookies[i],
                                                 error);
      if (!intern_atom_reply)
        return false;

      atoms[i] = intern_atom_reply->atom;
      free (intern_atom_reply);
    }

  return true;
}

bool
init_transport (xcb_xim_server_connection_t *xim,
                const char *name,
                xcb_generic_error_t **error)
{
  const xcb_setup_t *setup;
  xcb_screen_iterator_t iter;
  xcb_intern_atom_cookie_t intern_atom_cookie;
  xcb_intern_atom_reply_t *intern_atom_reply;
  xcb_get_property_cookie_t get_property_cookie;
  xcb_get_property_reply_t *get_property_reply;
  xcb_atom_t *data = NULL;
  int nitems;
  char *atom_name;
  xcb_atom_t atom;
  int i;

  /* Create a window that accepts incoming connections.  */
  setup = xcb_get_setup (xim->connection);
  iter = xcb_setup_roots_iterator (setup);
  xim->screen = iter.data;

  xim->accept_window = xcb_generate_id (xim->connection);
  xcb_create_window (xim->connection,
                     XCB_COPY_FROM_PARENT,
                     xim->accept_window,
                     xim->screen->root,
                     0, 0,
                     1, 1,
                     1,
                     XCB_WINDOW_CLASS_INPUT_OUTPUT,
                     xim->screen->root_visual,
                     0,
                     NULL);

  /* Advertise server name through window property.  */
  if (asprintf (&atom_name, "@server=%s", name) < 1)
    return false;

  intern_atom_cookie = xcb_intern_atom (xim->connection,
                                        false,
                                        strlen (atom_name),
                                        atom_name);
  free (atom_name);

  intern_atom_reply = xcb_intern_atom_reply (xim->connection,
                                             intern_atom_cookie,
                                             error);
  if (!intern_atom_reply)
    return false;

  atom = intern_atom_reply->atom;
  free (intern_atom_reply);

  /* Register server through the window property.  */
  get_property_cookie = xcb_get_property (xim->connection,
                                          0,
                                          xim->screen->root,
                                          xim->atoms[XIM_SERVERS],
                                          XCB_ATOM_ATOM,
                                          0,
                                          UINT_MAX);
  get_property_reply = xcb_get_property_reply (xim->connection,
                                               get_property_cookie,
                                               error);

  if (get_property_reply->type != XCB_NONE
      && (get_property_reply->type != XCB_ATOM_ATOM
          || get_property_reply->format != 32))
    {
      free (get_property_reply);
      return false;
    }

  data = xcb_get_property_value (get_property_reply);
  nitems = xcb_get_property_value_length (get_property_reply);
  for (i = 0; i < nitems && data[i] != atom; i++)
    ;

  if (i != nitems)
    {
      xcb_get_selection_owner_cookie_t get_selection_owner_cookie;
      xcb_get_selection_owner_reply_t *get_selection_owner_reply;

      get_selection_owner_cookie =
        xcb_get_selection_owner (xim->connection, atom);

      get_selection_owner_reply =
        xcb_get_selection_owner_reply (xim->connection,
                                       get_selection_owner_cookie, error);
      if (get_selection_owner_reply->owner != XCB_WINDOW_NONE
          && get_selection_owner_reply->owner != xim->accept_window)
        {
          free (get_selection_owner_reply);
          return false;
        }

      free (get_selection_owner_reply);

      xcb_set_selection_owner (xim->connection,
                               xim->accept_window,
                               atom,
                               XCB_CURRENT_TIME);

      xcb_change_property (xim->connection,
                           XCB_PROP_MODE_PREPEND,
                           xim->screen->root,
                           xim->atoms[XIM_SERVERS],
                           XCB_ATOM_ATOM,
                           32,
                           0,
                           (const void *) data);
    }
  else
    {
      xcb_set_selection_owner (xim->connection,
                               xim->accept_window,
                               atom,
                               XCB_CURRENT_TIME);

      xcb_change_property (xim->connection,
                           XCB_PROP_MODE_PREPEND,
                           xim->screen->root,
                           xim->atoms[XIM_SERVERS],
                           XCB_ATOM_ATOM,
                           32,
                           1,
                           (const void *) &atom);
    }

  xcb_flush (xim->connection);

  return true;
}

xcb_xim_server_connection_t *
xcb_xim_server_connection_new (xcb_connection_t *connection,
                               const char *name,
                               const char *locale,
                               xcb_generic_error_t **error)
{
  xcb_xim_server_connection_t *xim;

  xim = malloc (sizeof (xcb_xim_server_connection_t));
  if (!xim)
    return NULL;

  memset (xim, 0, sizeof (xcb_xim_server_connection_t));

  xim->locale = strdup (locale);
  if (!xim->locale)
    {
      free (xim);
      return NULL;
    }
  xim->connection = connection;

  if (!init_atoms (connection, xim->atoms, error))
    {
      free (xim);
      return NULL;
    }

  if (!init_transport (xim, name, error))
    {
      free (xim);
      return NULL;
    }

  return xim;
}

void
xcb_xim_server_connection_free (xcb_xim_server_connection_t *xim)
{
  struct xcb_xim_list_t *requests;

  free (xim->locale);
  free (xim->clients);

  requests = xim->requests;
  while (requests)
    {
      struct xcb_xim_list_t *next = requests->next;
      free (requests->data);
      requests = next;
    }

  free (xim);
}

static bool
accept_connection (xcb_xim_server_connection_t *xim,
                   xcb_client_message_event_t *request,
                   xcb_generic_error_t **error)
{
  xcb_client_message_event_t reply;
  xcb_xim_transport_t *client;

  if (xim->nclients == xim->maxclients)
    {
      xim->maxclients = xim->maxclients * 2 + 10;
      xim->clients = realloc (xim->clients,
                              sizeof (xcb_xim_transport_t) * xim->maxclients);
      if (!xim->clients)
        return false;
    }

  client = &xim->clients[xim->nclients++];
  client->client_window = request->data.data32[0];
  client->server_window = xcb_generate_id (xim->connection);
  xcb_create_window (xim->connection,
                     XCB_COPY_FROM_PARENT,
                     client->server_window,
                     xim->screen->root,
                     0, 0,
                     1, 1,
                     1,
                     XCB_WINDOW_CLASS_INPUT_OUTPUT,
                     xim->screen->root_visual,
                     0,
                     NULL);

  memset (&reply, 0, sizeof (reply));
  reply.response_type = XCB_CLIENT_MESSAGE;
  reply.window = client->client_window;
  reply.type = xim->atoms[_XIM_XCONNECT];
  reply.format = 32;
  reply.data.data32[0] = client->server_window;
  reply.data.data32[1] = 0;
  reply.data.data32[2] = 0;
  reply.data.data32[3] = 20;

  xcb_send_event (xim->connection,
                  false,
                  client->client_window,
                  XCB_EVENT_MASK_NO_EVENT,
                  (const char *) &reply);

  xcb_flush (xim->connection);

  return true;
}

static xcb_xim_transport_t *
find_transport (xcb_xim_server_connection_t *xim, xcb_window_t server_window)
{
  int i;

  for (i = xim->nclients - 1; i >= 0; i--)
    if (xim->clients[i].server_window == server_window)
      return &xim->clients[i];

  return NULL;
}

static uint8_t *
read_data (xcb_xim_server_connection_t *xim,
           xcb_xim_transport_t *client,
           xcb_client_message_event_t *event,
           size_t *length,
           xcb_generic_error_t **error)
{
  if (event->format == 32)
    {
      xcb_atom_t atom = event->data.data32[1];
      uint8_t *data;
      uint32_t value_length = event->data.data32[0];
      int actual_value_length;
      int request_length;
      uint16_t nitems;
      uint8_t *value;

      xcb_get_property_cookie_t get_property_cookie;
      xcb_get_property_reply_t *get_property_reply;

      get_property_cookie = xcb_get_property (xim->connection,
                                              true,
                                              client->server_window,
                                              atom,
                                              XCB_ATOM_STRING,
                                              0,
                                              UINT_MAX);
      get_property_reply = xcb_get_property_reply (xim->connection,
                                                   get_property_cookie,
                                                   error);
      if (!get_property_reply)
        return NULL;

      actual_value_length =
        xcb_get_property_value_length (get_property_reply);
      if (value_length > actual_value_length
          || value_length < 4)
        {
          free (get_property_reply);
          return NULL;
        }

      value = xcb_get_property_value (get_property_reply);
      nitems = *(uint16_t *) &value[2];
      request_length = nitems * 4 + 4;
      if (request_length > value_length)
        {
          free (get_property_reply);
          return NULL;
        }

      data = malloc (request_length);
      if (!data)
        {
          free (get_property_reply);
          return NULL;
        }

      memcpy (data, value, request_length);
      *length = request_length;

      free (get_property_reply);

      hexdump ("> ", data, *length);
      return data;
    }
  else
    {
      uint16_t nitems;
      int request_length;
      uint8_t *data;

      nitems = *(uint16_t *) &event->data.data8[2];
      request_length = nitems * 4 + 4;
      if (request_length > sizeof (event->data.data8))
        return NULL;

      data = malloc (request_length);
      if (!data)
        return NULL;

      memcpy (data, event->data.data8, request_length);
      *length = request_length;

      hexdump ("> ", data, *length);
      return data;
    }

  return NULL;
}

static bool
write_data (xcb_xim_server_connection_t *xim,
            xcb_xim_transport_t *client,
            size_t length,
            const uint8_t *data,
            xcb_generic_error_t **error)
{
  xcb_client_message_event_t event;

  memset (&event, 0, sizeof (event));
  event.response_type = XCB_CLIENT_MESSAGE;
  event.window = client->client_window;
  event.type = xim->atoms[_XIM_PROTOCOL];

  if (length > 20)
    {
      char buffer[16];
      static uint8_t serial;
      xcb_intern_atom_cookie_t intern_atom_cookie;
      xcb_intern_atom_reply_t *intern_atom_reply;
      xcb_get_property_cookie_t get_property_cookie;
      xcb_get_property_reply_t *get_property_reply;
      xcb_atom_t atom;

      event.format = 32;

      snprintf (buffer, sizeof (buffer), "server%u", serial++);
      intern_atom_cookie = xcb_intern_atom (xim->connection,
                                            false,
                                            strlen (buffer),
                                            buffer);
      intern_atom_reply = xcb_intern_atom_reply (xim->connection,
                                                 intern_atom_cookie,
                                                 error);
      if (!intern_atom_reply)
        return false;
      atom = intern_atom_reply->atom;
      free (intern_atom_reply);

      get_property_cookie = xcb_get_property (xim->connection,
                                              true,
                                              client->client_window,
                                              atom,
                                              XCB_ATOM_STRING,
                                              0,
                                              UINT_MAX);
      get_property_reply = xcb_get_property_reply (xim->connection,
                                                   get_property_cookie,
                                                   error);
      if (!get_property_reply)
        return false;
      free (get_property_reply);

      xcb_change_property (xim->connection,
                           XCB_PROP_MODE_APPEND,
                           client->client_window,
                           atom,
                           XCB_ATOM_STRING,
                           8,
                           length,
                           data);

      event.data.data32[0] = length;
      event.data.data32[1] = atom;
    }
  else
    {
      event.format = 8;
      memcpy (event.data.data8, data, length);
    }

  xcb_send_event (xim->connection,
                  false,
                  client->client_window,
                  XCB_EVENT_MASK_NO_EVENT,
                  (const char *) &event);
  xcb_flush (xim->connection);

  hexdump (" <", data, length);

  return true;
}

uint16_t
xcb_xim_uint16 (xcb_xim_transport_t *transport, uint16_t value)
{
  return HO16 (transport, value);
}

uint32_t
xcb_xim_uint32 (xcb_xim_transport_t *transport, uint32_t value)
{
  return HO32 (transport, value);
}

bool
xcb_xim_str_iterator_has_data (xcb_xim_str_iterator_t *i)
{
  return i->remainder >= 1 + i->data->length;
}

void
xcb_xim_str_iterator_next (xcb_xim_str_iterator_t *i)
{
  uint8_t length = 1 + i->data->length;

  i->data = (xcb_xim_str_t *) ((uint8_t *) i->data + length);
  i->index++;
  i->remainder -= length;
}

xcb_xim_extension_t *
xcb_xim_extension_new (xcb_xim_transport_t *transport,
                       uint8_t major_opcode,
                       uint8_t minor_opcode,
                       uint16_t name_length,
                       const char *name)
{
  xcb_xim_extension_t *extension;
  size_t length;

  length = 4 + name_length + PAD (name_length);
  extension = malloc (length);
  if (!extension)
    return NULL;

  memset (extension, 0, length);
  extension->major_opcode = major_opcode;
  extension->minor_opcode = minor_opcode;
  extension->name_length = NO16 (transport, name_length);
  memcpy ((uint8_t *) (extension + 1), name, name_length);

  return extension;
}

xcb_xim_attribute_spec_t *
xcb_xim_attribute_spec_new (xcb_xim_transport_t *transport,
                            uint16_t attribute_id,
                            uint16_t type,
                            uint16_t name_length,
                            const char *name)
{
  xcb_xim_attribute_spec_t *spec;
  size_t length;

  length = 6 + name_length + PAD (2 + name_length);

  spec = malloc (length);
  if (!spec)
    return NULL;

  memset (spec, 0, length);
  spec->attribute_id = HO16 (transport, attribute_id);
  spec->type = NO16 (transport, type);
  spec->length = NO16 (transport, name_length);
  memcpy ((uint8_t *) (spec + 1), name, name_length);

  return spec;
}

xcb_xim_str_conv_text_t *
xcb_xim_str_conv_text_new (xcb_xim_transport_t *transport,
                           uint16_t type,
                           uint16_t string_byte_length,
                           const uint8_t *string,
                           uint16_t feedbacks_length,
                           const xcb_xim_feedback_t *feedbacks)
{
  xcb_xim_str_conv_text_t *conv;
  size_t length;
  uint8_t *p;
  uint16_t i;

  length = 4 + string_byte_length + PAD (string_byte_length)
    + 4 + 4 * feedbacks_length;

  conv = malloc (length);
  if (!conv)
    return NULL;

  memset (conv, 0, length);
  conv->type = NO16 (transport, type);
  conv->string_byte_length = NO16 (transport, string_byte_length);

  p = (uint8_t *) (conv + 1);
  memcpy (p, string, string_byte_length);
  p += string_byte_length + PAD (string_byte_length);
  PACK16 (transport, p, feedbacks_length);
  PACK16 (transport, p, 0);
  for (i = 0; i < feedbacks_length; i++)
    PACK16 (transport, p, feedbacks[i]);

  return conv;
}

xcb_xim_attribute_t *
xcb_xim_attribute_card8_new (xcb_xim_transport_t *transport,
                             uint16_t attribute_id,
                             uint8_t value)
{
  xcb_xim_attribute_t *attribute;
  size_t length;
  uint8_t *p;

  length = 4 + 4;

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);

  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, 1);
  p = (uint8_t *) (attribute + 1);
  PACK8 (transport, p, value);

  return attribute;
}

xcb_xim_attribute_t *
xcb_xim_attribute_card16_new (xcb_xim_transport_t *transport,
                              uint16_t attribute_id,
                              uint16_t value)
{
  xcb_xim_attribute_t *attribute;
  size_t length;
  uint8_t *p;

  length = 4 + 4;

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);

  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, 2);
  p = (uint8_t *) (attribute + 1);
  PACK16 (transport, p, value);

  return attribute;
}

xcb_xim_attribute_t *
xcb_xim_attribute_card32_new (xcb_xim_transport_t *transport,
                              uint16_t attribute_id,
                              uint32_t value)
{
  xcb_xim_attribute_t *attribute;
  size_t length;
  uint8_t *p;

  length = 4 + 4;

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);

  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, 4);
  p = (uint8_t *) (attribute + 1);
  PACK32 (transport, p, value);

  return attribute;
}

xcb_xim_attribute_t *
xcb_xim_attribute_string8_new (xcb_xim_transport_t *transport,
                               uint16_t attribute_id,
                               uint8_t value_length,
                               const char *value)
{
  xcb_xim_attribute_t *attribute;
  size_t length;

  length = 4 + value_length + PAD (value_length);

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);

  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, value_length);
  memcpy (attribute + 1, value, value_length);

  return attribute;
}

xcb_xim_attribute_t *
xcb_xim_attribute_styles_new (xcb_xim_transport_t *transport,
                              uint16_t attribute_id,
                              uint16_t value_length,
                              const uint32_t *value)
{
  xcb_xim_attribute_t *attribute;
  size_t length;
  uint16_t i;
  uint8_t *p;

  length = 4 + 4 + 4 * value_length;

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);

  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, 4 + 4 * value_length);

  p = (uint8_t *) (attribute + 1);
  PACK16 (transport, p, value_length);
  PACK16 (transport, p, 0);
  for (i = 0; i < value_length; i++)
    PACK32 (transport, p, value[i]);

  return attribute;
}

xcb_xim_attribute_t *
xcb_xim_attribute_rectangle_new (xcb_xim_transport_t *transport,
                                 uint16_t attribute_id,
                                 const xcb_rectangle_t *value)
{
  xcb_xim_attribute_t *attribute;
  size_t length;
  uint8_t *p;

  length = 4 + 8;

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);

  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, 8);

  p = (uint8_t *) (attribute + 1);
  PACK16 (transport, p, value->x);
  PACK16 (transport, p, value->y);
  PACK16 (transport, p, value->width);
  PACK16 (transport, p, value->height);

  return attribute;
}

xcb_xim_attribute_t *
xcb_xim_attribute_point_new (xcb_xim_transport_t *transport,
                             uint16_t attribute_id,
                             const xcb_point_t *value)
{
  xcb_xim_attribute_t *attribute;
  size_t length;
  uint8_t *p;

  length = 4 + 4;

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);

  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, 4);

  p = (uint8_t *) (attribute + 1);
  PACK16 (transport, p, value->x);
  PACK16 (transport, p, value->y);

  return attribute;
}

xcb_xim_attribute_t *
xcb_xim_attribute_font_set_new (xcb_xim_transport_t *transport,
                                uint16_t attribute_id,
                                uint8_t value_length,
                                const char *value)
{
  xcb_xim_attribute_t *attribute;
  size_t value_byte_length;
  size_t length;
  uint8_t *p;

  value_byte_length = 2 + value_length + PAD (2 + value_length);
  length = 4 + value_byte_length;

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);

  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, value_byte_length);

  p = (uint8_t *) (attribute + 1);
  PACK16 (transport, p, value_length);
  memcpy (p, value, value_length);

  return attribute;
}

xcb_xim_attribute_t *
xcb_xim_attribute_hotkey_triggers_new (xcb_xim_transport_t *transport,
                                       uint16_t attribute_id,
                                       uint32_t value_length,
                                       const xcb_xim_triggerkey_t **triggers,
                                       const xcb_xim_hotkey_state_t *states)
{
  xcb_xim_attribute_t *attribute;
  size_t value_byte_length;
  size_t length;
  uint32_t i;
  uint8_t *p;

  value_byte_length = 4 + (12 + 4) * value_length;
  length = 4 + value_byte_length;

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);

  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, value_byte_length);

  p = (uint8_t *) (attribute + 1);
  PACK32 (transport, p, value_length);
  for (i = 0; i < value_length; i++)
    {
      memcpy (p, triggers[i], 12);
      p += 12;
    }
  for (i = 0; i < value_length; i++)
    PACK32 (transport, p, states[i]);

  return attribute;
}

xcb_xim_attribute_t *
xcb_xim_attribute_packed_new (xcb_xim_transport_t *transport,
                              uint16_t attribute_id,
                              uint16_t value_byte_length,
                              const void *value)
{
  xcb_xim_attribute_t *attribute;
  size_t length;
  uint8_t *p;

  length = 4 + value_byte_length;

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);

  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, value_byte_length);

  p = (uint8_t *) (attribute + 1);
  memcpy (p, value, value_byte_length);

  return attribute;
}

xcb_xim_attribute_t *
xcb_xim_attribute_nested_list_new (xcb_xim_transport_t *transport,
                                   uint16_t attribute_id,
                                   uint16_t value_length,
                                   const xcb_xim_attribute_t **value)
{
  xcb_xim_attribute_t *attribute;
  size_t length;
  uint16_t i;
  uint8_t *p;

  length = 4;
  for (i = 0; i < value_length; i++)
    {
      size_t value_byte_length =
        HO16 (transport, value[i]->value_byte_length);

      length += 4 + value_byte_length + PAD (value_byte_length);
    }

  attribute = malloc (length);
  if (!attribute)
    return NULL;

  memset (attribute, 0, length);
  attribute->attribute_id = NO16 (transport, attribute_id);
  attribute->value_byte_length = NO16 (transport, length - 4);

  p = (uint8_t *) (attribute + 1);
  for (i = 0; i < value_length; i++)
    {
      size_t value_byte_length =
        HO16 (transport, value[i]->value_byte_length);
      size_t attribute_byte_length = 4
        + value_byte_length + PAD (value_byte_length);

      memcpy (p, value[i], attribute_byte_length);
      p += attribute_byte_length;
    }

  return attribute;
}

xcb_xim_attribute_iterator_t
xcb_xim_attribute_nested_list_attribute_iterator (xcb_xim_generic_request_t *r,
                                                  xcb_xim_attribute_t *a)
{
  xcb_xim_attribute_iterator_t i;
  xcb_xim_request_container_t *container = NULL;
  uint16_t value_byte_length;

  container = xcb_xim_container_of (r, container, request);

  i.transport = container->requestor;
  i.data = (xcb_xim_attribute_t *) (a + 1);
  i.index = 0;

  value_byte_length = HO16 (container->requestor, a->value_byte_length);

  i.remainder = value_byte_length;

  return i;
}

static bool
xcb_xim_connect_reply (xcb_xim_server_connection_t *xim,
                       xcb_xim_transport_t *transport,
                       uint16_t major,
                       uint16_t minor,
                       xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_CONNECT_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, major);
  PACK16 (transport, p, minor);

  return write_data (xim, transport, sizeof (data), data, error);
}

static bool
xcb_xim_disconnect_reply (xcb_xim_server_connection_t *xim,
                          xcb_xim_transport_t *transport,
                          xcb_generic_error_t **error)
{
  uint8_t data[4], *p = data;

  PACK8 (transport, p, XCB_XIM_DISCONNECT_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_error (xcb_xim_server_connection_t *xim,
               xcb_xim_transport_t *transport,
               uint16_t input_method_id,
               uint16_t input_context_id,
               xcb_xim_error_flag_t error_flag,
               xcb_xim_error_code_t error_code,
               uint16_t detail_type,
               uint16_t detail_length,
               const uint8_t *detail,
               xcb_generic_error_t **error)
{
  uint8_t *data, *p;
  size_t length;
  bool success;

  length = 4 + 12 + detail_length + PAD (detail_length);

  p = data = malloc (length);
  if (!data)
    return false;

  memset (data, 0, length);

  PACK8 (transport, p, XCB_XIM_ERROR);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (length - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK16 (transport, p, error_flag);
  PACK16 (transport, p, error_code);
  PACK16 (transport, p, detail_length);
  PACK16 (transport, p, detail_type);
  memcpy (p, detail, detail_length);

  success = write_data (xim, transport, p - data, data, error);
  free (data);

  return success;
}

bool
xcb_xim_open_reply (xcb_xim_server_connection_t *xim,
                    xcb_xim_transport_t *transport,
                    uint16_t input_method_id,
                    uint16_t im_attrs_length,
                    xcb_xim_attribute_spec_t **im_attrs,
                    uint16_t ic_attrs_length,
                    xcb_xim_attribute_spec_t **ic_attrs,
                    xcb_generic_error_t **error)
{
  uint8_t *data, *p;
  size_t length;
  uint16_t i;
  uint16_t im_attrs_byte_length;
  uint16_t ic_attrs_byte_length;
  bool success;

  im_attrs_byte_length = 0;
  for (i = 0; i < im_attrs_length; i++)
    {
      size_t name_length = HO16 (transport, im_attrs[i]->length);

      im_attrs_byte_length += 6 + name_length + PAD (2 + name_length);
    }

  ic_attrs_byte_length = 0;
  for (i = 0; i < ic_attrs_length; i++)
    {
      size_t name_length = HO16 (transport, ic_attrs[i]->length);

      ic_attrs_byte_length += 6 + name_length + PAD (2 + name_length);
    }

  length = 4 + 4 + im_attrs_byte_length + 4 + ic_attrs_byte_length;

  p = data = malloc (length);
  if (!data)
    return false;

  memset (data, 0, length);

  PACK8 (transport, p, XCB_XIM_OPEN_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (length - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, im_attrs_byte_length);

  for (i = 0; i < im_attrs_length; i++)
    {
      size_t name_length = HO16 (transport, im_attrs[i]->length);
      size_t spec_length = 6 + name_length + PAD (2 + name_length);

      memcpy (p, im_attrs[i], spec_length);
      p += spec_length;
    }

  PACK16 (transport, p, ic_attrs_byte_length);
  PACK16 (transport, p, 0);

  for (i = 0; i < ic_attrs_length; i++)
    {
      size_t name_length = HO16 (transport, ic_attrs[i]->length);
      size_t spec_length = 6 + name_length + PAD (2 + name_length);

      memcpy (p, ic_attrs[i], spec_length);
      p += spec_length;
    }

  success = write_data (xim, transport, length, data, error);
  free (data);

  return success;
}

bool
xcb_xim_close_reply (xcb_xim_server_connection_t *xim,
                     xcb_xim_transport_t *transport,
                     uint16_t input_method_id,
                     xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_CLOSE_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, 0);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_register_triggerkeys (xcb_xim_server_connection_t *xim,
                              xcb_xim_transport_t *transport,
                              uint16_t input_method_id,
                              uint32_t on_keys_length,
                              const xcb_xim_triggerkey_t **on_keys,
                              uint32_t off_keys_length,
                              const xcb_xim_triggerkey_t **off_keys,
                              xcb_generic_error_t **error)
{
  uint8_t *data, *p;
  size_t length;
  uint32_t i;
  bool success;

  length = 4 + 12 + 12 * on_keys_length + 12 * off_keys_length;

  p = data = malloc (length);
  if (!data)
    return false;

  memset (data, 0, length);

  PACK8 (transport, p, XCB_XIM_REGISTER_TRIGGERKEYS);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (length - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, 0);
  PACK32 (transport, p, 12 * on_keys_length);
  for (i = 0; i < on_keys_length; i++)
    {
      memcpy (p, on_keys[i], 12);
      p += 12;
    }
  PACK32 (transport, p, 12 * off_keys_length);
  for (i = 0; i < off_keys_length; i++)
    {
      memcpy (p, off_keys[i], 12);
      p += 12;
    }

  success = write_data (xim, transport, p - data, data, error);
  free (data);

  return success;
}

bool
xcb_xim_trigger_notify_reply (xcb_xim_server_connection_t *xim,
                              xcb_xim_transport_t *transport,
                              uint16_t input_method_id,
                              uint16_t input_context_id,
                              xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_TRIGGER_NOTIFY_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_set_event_mask (xcb_xim_server_connection_t *xim,
                        xcb_xim_transport_t *transport,
                        uint16_t input_method_id,
                        uint16_t input_context_id,
                        uint32_t forward_event_mask,
                        uint32_t synchronous_event_mask,
                        xcb_generic_error_t **error)
{
  uint8_t data[16], *p = data;

  PACK8 (transport, p, XCB_XIM_SET_EVENT_MASK);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK32 (transport, p, forward_event_mask);
  PACK32 (transport, p, synchronous_event_mask);

  return write_data (xim, transport, sizeof (data), data, error);
}

xcb_xim_str_iterator_t
xcb_xim_query_extension_request_extension_iterator (
  const xcb_xim_query_extension_request_t *r)
{
  xcb_xim_str_iterator_t i;
  xcb_xim_request_container_t *container = NULL;
  uint16_t request_byte_length;
  uint16_t extensions_byte_length;

  container = xcb_xim_container_of (r, container, request);

  i.data = (xcb_xim_str_t *) (r + 1);
  i.index = 0;

  request_byte_length = HO16 (container->requestor, r->length) * 4;
  extensions_byte_length = HO16 (container->requestor,
                                 r->extensions_byte_length);

  i.remainder = MIN (request_byte_length, extensions_byte_length);

  return i;
}

bool
xcb_xim_query_extension_reply (xcb_xim_server_connection_t *xim,
                               xcb_xim_transport_t *transport,
                               uint16_t input_method_id,
                               uint16_t extensions_length,
                               xcb_xim_extension_t **extensions,
                               xcb_generic_error_t **error)
{
  uint8_t *data, *p;
  size_t length;
  uint16_t extensions_byte_length;
  uint16_t i;
  bool success;

  extensions_byte_length = 0;
  for (i = 0; i < extensions_length; i++)
    {
      size_t name_length = HO16 (transport, extensions[i]->name_length);
      extensions_byte_length += 4 + name_length + PAD (name_length);
    }

  length = 4 + 4 + extensions_byte_length;

  p = data = malloc (length);
  if (!data)
    return false;

  memset (data, 0, length);

  PACK8 (transport, p, XCB_XIM_QUERY_EXTENSION_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (length - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, extensions_byte_length);

  for (i = 0; i < extensions_length; i++)
    {
      size_t name_length = HO16 (transport, extensions[i]->name_length);
      size_t extension_byte_length = 4 + name_length + PAD (name_length);
      memcpy (p, extensions[i], extension_byte_length);
      p += extension_byte_length;
    }

  success = write_data (xim, transport, p - data, data, error);
  free (data);

  return success;
}

xcb_xim_str_iterator_t
xcb_xim_encoding_negotiation_request_encoding_iterator (
  const xcb_xim_encoding_negotiation_request_t *r)
{
  xcb_xim_str_iterator_t i;
  xcb_xim_request_container_t *container = NULL;
  uint16_t request_byte_length;
  uint16_t encodings_byte_length;

  container = xcb_xim_container_of (r, container, request);

  i.data = (xcb_xim_str_t *) (r + 1);
  i.index = 0;

  request_byte_length = HO16 (container->requestor, r->length) * 4;
  encodings_byte_length = HO16 (container->requestor, r->encodings_byte_length);

  i.remainder = MIN (request_byte_length, encodings_byte_length);

  return i;
}

bool
xcb_xim_encoding_negotiation_reply (xcb_xim_server_connection_t *xim,
                                    xcb_xim_transport_t *transport,
                                    uint16_t input_method_id,
                                    uint16_t category,
                                    int16_t index,
                                    xcb_generic_error_t **error)
{
  uint8_t data[12], *p = data;

  PACK8 (transport, p, XCB_XIM_ENCODING_NEGOTIATION_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, category);
  PACK16 (transport, p, index);
  PACK16 (transport, p, 0);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_attribute_id_iterator_has_data (xcb_xim_attribute_id_iterator_t *i)
{
  return i->remainder >= 2;
}

void
xcb_xim_attribute_id_iterator_next (xcb_xim_attribute_id_iterator_t *i)
{
  i->data++;
  i->index++;
  i->remainder -= 2;
}

bool
xcb_xim_attribute_iterator_has_data (xcb_xim_attribute_iterator_t *i)
{
  uint16_t value_byte_length;

  if (i->remainder < 4)
    return false;

  value_byte_length = HO16 (i->transport, i->data->value_byte_length);

  return i->remainder >= 4 + value_byte_length + PAD (value_byte_length);
}

void
xcb_xim_attribute_iterator_next (xcb_xim_attribute_iterator_t *i)
{
  uint16_t value_byte_length = HO16 (i->transport, i->data->value_byte_length);
  size_t length = 4 + value_byte_length + PAD (value_byte_length);

  i->data = (xcb_xim_attribute_t *) ((uint8_t *) i->data + length);
  i->index++;
  i->remainder -= length;
}

xcb_xim_attribute_iterator_t
xcb_xim_set_im_values_request_attribute_iterator (
  const xcb_xim_set_im_values_request_t *r)
{
  xcb_xim_attribute_iterator_t i;
  xcb_xim_request_container_t *container = NULL;
  uint16_t request_byte_length;
  uint16_t attributes_byte_length;

  container = xcb_xim_container_of (r, container, request);

  i.transport = container->requestor;
  i.data = (xcb_xim_attribute_t *) (r + 1);
  i.index = 0;

  request_byte_length = HO16 (container->requestor, r->length) * 4;
  attributes_byte_length = HO16 (container->requestor,
                                 r->attributes_byte_length);

  i.remainder = MIN (request_byte_length, attributes_byte_length);

  return i;
}

bool
xcb_xim_set_im_values_reply (xcb_xim_server_connection_t *xim,
                             xcb_xim_transport_t *transport,
                             uint16_t input_method_id,
                             xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_SET_IM_VALUES_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, 0);

  return write_data (xim, transport, sizeof (data), data, error);
}

xcb_xim_attribute_id_iterator_t
xcb_xim_get_im_values_request_attribute_id_iterator (
  const xcb_xim_get_im_values_request_t *r)
{
  xcb_xim_attribute_id_iterator_t i;
  xcb_xim_request_container_t *container = NULL;
  uint16_t request_byte_length;
  uint16_t attributes_byte_length;

  container = xcb_xim_container_of (r, container, request);

  i.transport = container->requestor;
  i.data = (uint16_t *) (r + 1);
  i.index = 0;

  request_byte_length = HO16 (container->requestor, r->length) * 4;
  attributes_byte_length = HO16 (container->requestor,
                                 r->attributes_byte_length);

  i.remainder = MIN (request_byte_length, attributes_byte_length);

  return i;
}

bool
xcb_xim_get_im_values_reply (xcb_xim_server_connection_t *xim,
                             xcb_xim_transport_t *transport,
                             uint16_t input_method_id,
                             uint16_t attributes_length,
                             xcb_xim_attribute_t **attributes,
                             xcb_generic_error_t **error)
{
  uint8_t *data, *p;
  size_t length;
  uint16_t attributes_byte_length;
  uint16_t i;
  bool success;

  attributes_byte_length = 0;
  for (i = 0; i < attributes_length; i++)
    {
      size_t value_byte_length =
        HO16 (transport, attributes[i]->value_byte_length);

      attributes_byte_length += 4 + value_byte_length + PAD (value_byte_length);
    }

  length = 4 + 4 + attributes_byte_length;

  p = data = malloc (length);
  if (!data)
    return false;

  memset (data, 0, length);

  PACK8 (transport, p, XCB_XIM_GET_IM_VALUES_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (length - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, attributes_byte_length);

  for (i = 0; i < attributes_length; i++)
    {
      size_t value_byte_length =
        HO16 (transport, attributes[i]->value_byte_length);
      size_t attribute_byte_length =
        4 + value_byte_length + PAD (value_byte_length);

      memcpy (p, attributes[i], attribute_byte_length);
      p += attribute_byte_length;
    }

  success = write_data (xim, transport, p - data, data, error);
  free (data);

  return success;
}

xcb_xim_attribute_iterator_t
xcb_xim_create_ic_request_attribute_iterator (
  const xcb_xim_create_ic_request_t *r)
{
  xcb_xim_attribute_iterator_t i;
  xcb_xim_request_container_t *container = NULL;
  uint16_t request_byte_length;
  uint16_t attributes_byte_length;

  container = xcb_xim_container_of (r, container, request);

  i.transport = container->requestor;
  i.data = (xcb_xim_attribute_t *) (r + 1);
  i.index = 0;

  request_byte_length = HO16 (container->requestor, r->length) * 4;
  attributes_byte_length = HO16 (container->requestor,
                                 r->attributes_byte_length);

  i.remainder = MIN (request_byte_length, attributes_byte_length);

  return i;
}

bool
xcb_xim_create_ic_reply (xcb_xim_server_connection_t *xim,
                         xcb_xim_transport_t *transport,
                         uint16_t input_method_id,
                         uint16_t input_context_id,
                         xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_CREATE_IC_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_destroy_ic_reply (xcb_xim_server_connection_t *xim,
                          xcb_xim_transport_t *transport,
                          uint16_t input_method_id,
                          uint16_t input_context_id,
                          xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_DESTROY_IC_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);

  return write_data (xim, transport, sizeof (data), data, error);
}

xcb_xim_attribute_iterator_t
xcb_xim_set_ic_values_request_attribute_iterator (
  const xcb_xim_set_ic_values_request_t *r)
{
  xcb_xim_attribute_iterator_t i;
  xcb_xim_request_container_t *container = NULL;
  uint16_t request_byte_length;
  uint16_t attributes_byte_length;

  container = xcb_xim_container_of (r, container, request);

  i.transport = container->requestor;
  i.data = (xcb_xim_attribute_t *) (r + 1);
  i.index = 0;

  request_byte_length = HO16 (container->requestor, r->length) * 4;
  attributes_byte_length = HO16 (container->requestor,
                                 r->attributes_byte_length);

  i.remainder = MIN (request_byte_length, attributes_byte_length);

  return i;
}

bool
xcb_xim_set_ic_values_reply (xcb_xim_server_connection_t *xim,
                             xcb_xim_transport_t *transport,
                             uint16_t input_method_id,
                             uint16_t input_context_id,
                             xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_SET_IC_VALUES_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);

  return write_data (xim, transport, sizeof (data), data, error);
}

xcb_xim_attribute_id_iterator_t
xcb_xim_get_ic_values_request_attribute_id_iterator (
  const xcb_xim_get_ic_values_request_t *r)
{
  xcb_xim_attribute_id_iterator_t i;
  xcb_xim_request_container_t *container = NULL;
  uint16_t request_byte_length;
  uint16_t attributes_byte_length;

  container = xcb_xim_container_of (r, container, request);

  i.transport = container->requestor;

  /* Need to calculate the end offset manually, since
     xcb_xim_get_ic_values_request_t has no padding at the end.  */
  i.data = (uint16_t *) ((uint8_t *) r + 10);
  i.index = 0;

  request_byte_length = HO16 (container->requestor, r->length) * 4;
  attributes_byte_length = HO16 (container->requestor,
                                 r->attributes_byte_length);

  i.remainder = MIN (request_byte_length, attributes_byte_length);

  return i;
}

bool
xcb_xim_get_ic_values_reply (xcb_xim_server_connection_t *xim,
                             xcb_xim_transport_t *transport,
                             uint16_t input_method_id,
                             uint16_t input_context_id,
                             uint16_t attributes_length,
                             xcb_xim_attribute_t **attributes,
                             xcb_generic_error_t **error)
{
  uint8_t *data, *p;
  size_t length;
  uint16_t attributes_byte_length;
  uint16_t i;
  bool success;

  attributes_byte_length = 0;
  for (i = 0; i < attributes_length; i++)
    {
      size_t value_byte_length =
        HO16 (transport, attributes[i]->value_byte_length);

      attributes_byte_length += 4 + value_byte_length + PAD (value_byte_length);
    }

  length = 4 + 4 + attributes_byte_length;

  p = data = malloc (length);
  if (!data)
    return false;

  memset (data, 0, length);

  PACK8 (transport, p, XCB_XIM_GET_IC_VALUES_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (length - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK16 (transport, p, attributes_byte_length);
  PACK16 (transport, p, 0);

  for (i = 0; i < attributes_length; i++)
    {
      size_t value_byte_length =
        HO16 (transport, attributes[i]->value_byte_length);
      size_t attribute_byte_length =
        4 + value_byte_length + PAD (value_byte_length);

      memcpy (p, attributes[i], attribute_byte_length);
      p += attribute_byte_length;
    }

  success = write_data (xim, transport, p - data, data, error);
  free (data);

  return success;
}

bool
xcb_xim_forward_event (xcb_xim_server_connection_t *xim,
                       xcb_xim_transport_t *transport,
                       uint16_t input_method_id,
                       uint16_t input_context_id,
                       uint16_t flag,
                       uint16_t serial,
                       xcb_generic_event_t *event,
                       xcb_generic_error_t **error)
{
  uint8_t data[44], *p = data;

  PACK8 (transport, p, XCB_XIM_FORWARD_EVENT);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK16 (transport, p, flag);
  PACK16 (transport, p, serial);
  memcpy (p, event, 32);

  return write_data (xim, transport, sizeof (data), data, error);
}

xcb_generic_event_t *
xcb_xim_forward_event_get_event (xcb_xim_forward_event_request_t *request)
{
  return (xcb_generic_event_t *) (request + 1);
}

uint16_t
xcb_xim_forward_event_get_serial (xcb_xim_forward_event_request_t *request)
{
  xcb_xim_request_container_t *container = NULL;

  container = xcb_xim_container_of (request, container, request);

  return HO16 (container->requestor, request->serial);
}

bool
xcb_xim_sync_reply (xcb_xim_server_connection_t *xim,
                    xcb_xim_transport_t *transport,
                    uint16_t input_method_id,
                    uint16_t input_context_id,
                    xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_SYNC_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_commit (xcb_xim_server_connection_t *xim,
                xcb_xim_transport_t *transport,
                uint16_t input_method_id,
                uint16_t input_context_id,
                uint16_t flag,
                uint32_t keysym,
                uint16_t string_length,
                const uint8_t *string,
                xcb_generic_error_t **error)
{
  uint8_t *data, *p;
  size_t length;
  bool success;

  length = 10;

  if ((flag & XCB_XIM_COMMIT_FLAG_KEYSYM) != 0)
    length += 6;

  if ((flag & XCB_XIM_COMMIT_FLAG_STRING) != 0)
    length += 2 + string_length;

  length += PAD (length);

  p = data = malloc (length);
  if (!data)
    return false;

  memset (data, 0, length);

  PACK8 (transport, p, XCB_XIM_COMMIT);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (length - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK16 (transport, p, flag);

  if ((flag & XCB_XIM_COMMIT_FLAG_KEYSYM) != 0)
    {
      PACK16 (transport, p, 0);
      PACK32 (transport, p, keysym);
    }

  if ((flag & XCB_XIM_COMMIT_FLAG_STRING) != 0)
    {
      PACK16 (transport, p, string_length);
      memcpy (p, string, string_length);
      p += string_length;
    }

  success = write_data (xim, transport, length, data, error);
  free (data);

  return success;
}

bool
xcb_xim_reset_ic_reply (xcb_xim_server_connection_t *xim,
                        xcb_xim_transport_t *transport,
                        uint16_t input_method_id,
                        uint16_t input_context_id,
                        uint16_t preedit_length,
                        const uint8_t *preedit,
                        xcb_generic_error_t **error)
{
  uint8_t *data, *p;
  size_t length;
  bool success;

  length = 4 + 6 + preedit_length + PAD (2 + preedit_length);

  p = data = malloc (length);
  if (!data)
    return false;

  memset (data, 0, length);

  PACK8 (transport, p, XCB_XIM_RESET_IC_REPLY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (length - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK16 (transport, p, preedit_length);
  memcpy (p, preedit, preedit_length);
  p += preedit_length + PAD (2 + preedit_length);

  success = write_data (xim, transport, p - data, data, error);
  free (data);

  return success;
}

bool
xcb_xim_geometry (xcb_xim_server_connection_t *xim,
                  xcb_xim_transport_t *transport,
                  uint16_t input_method_id,
                  uint16_t input_context_id,
                  xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_GEOMETRY);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_str_conversion (xcb_xim_server_connection_t *xim,
                        xcb_xim_transport_t *transport,
                        uint16_t input_method_id,
                        uint16_t input_context_id,
                        uint16_t position,
                        uint32_t direction,
                        uint16_t factor,
                        uint16_t operation,
                        int16_t byte_length,
                        xcb_generic_error_t **error)
{
  uint8_t data[26], *p = data;

  PACK8 (transport, p, XCB_XIM_STR_CONVERSION);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK16 (transport, p, position);
  PACK32 (transport, p, direction);
  PACK16 (transport, p, factor);
  PACK16 (transport, p, operation);
  PACK16 (transport, p, byte_length);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_preedit_start (xcb_xim_server_connection_t *xim,
                       xcb_xim_transport_t *transport,
                       uint16_t input_method_id,
                       uint16_t input_context_id,
                       xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_PREEDIT_START);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_preedit_draw (xcb_xim_server_connection_t *xim,
                      xcb_xim_transport_t *transport,
                      uint16_t input_method_id,
                      uint16_t input_context_id,
                      int32_t caret,
                      int32_t change_first,
                      int32_t change_length,
                      uint32_t status,
                      uint16_t preedit_length,
                      const uint8_t *preedit,
                      uint16_t feedbacks_length,
                      const xcb_xim_feedback_t *feedbacks,
                      xcb_generic_error_t **error)
{
  uint8_t *data, *p;
  size_t length;
  bool success;
  uint16_t i;

  length = 4 + 26 + preedit_length + PAD (2 + preedit_length)
    + 4 * feedbacks_length;

  p = data = malloc (length);
  if (!data)
    return false;

  memset (data, 0, length);

  PACK8 (transport, p, XCB_XIM_PREEDIT_DRAW);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (length - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK32 (transport, p, caret);
  PACK32 (transport, p, change_first);
  PACK32 (transport, p, change_length);
  PACK32 (transport, p, status);
  PACK16 (transport, p, preedit_length);
  memcpy (p, preedit, preedit_length);
  p += preedit_length + PAD (2 + preedit_length);
  PACK16 (transport, p, feedbacks_length);
  PACK16 (transport, p, 0);
  for (i = 0; i < feedbacks_length; i++)
    PACK32 (transport, p, feedbacks[i]);

  success = write_data (xim, transport, p - data, data, error);
  free (data);

  return success;
}

bool
xcb_xim_preedit_caret (xcb_xim_server_connection_t *xim,
                       xcb_xim_transport_t *transport,
                       uint16_t input_method_id,
                       uint16_t input_context_id,
                       int32_t position,
                       uint32_t direction,
                       uint32_t style,
                       xcb_generic_error_t **error)
{
  uint8_t data[20], *p = data;

  PACK8 (transport, p, XCB_XIM_PREEDIT_CARET);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK32 (transport, p, position);
  PACK32 (transport, p, direction);
  PACK32 (transport, p, style);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_preedit_done (xcb_xim_server_connection_t *xim,
                      xcb_xim_transport_t *transport,
                      uint16_t input_method_id,
                      uint16_t input_context_id,
                      xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_PREEDIT_DONE);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_preeditstate (xcb_xim_server_connection_t *xim,
                      xcb_xim_transport_t *transport,
                      uint16_t input_method_id,
                      uint16_t input_context_id,
                      uint32_t state,
                      xcb_generic_error_t **error)
{
  uint8_t data[12], *p = data;

  PACK8 (transport, p, XCB_XIM_PREEDITSTATE);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK32 (transport, p, state);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_status_start (xcb_xim_server_connection_t *xim,
                      xcb_xim_transport_t *transport,
                      uint16_t input_method_id,
                      uint16_t input_context_id,
                      xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_STATUS_START);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);

  return write_data (xim, transport, sizeof (data), data, error);
}

bool
xcb_xim_status_draw (xcb_xim_server_connection_t *xim,
                     xcb_xim_transport_t *transport,
                     uint16_t input_method_id,
                     uint16_t input_context_id,
                     uint32_t type,
                     uint32_t flag,
                     uint16_t status_length,
                     const uint8_t *status,
                     uint16_t feedbacks_length,
                     const xcb_xim_feedback_t *feedbacks,
                     uint32_t pixmap,
                     xcb_generic_error_t **error)
{
  uint8_t *data, *p;
  size_t length;
  uint16_t i;
  bool success;

  length = 4 + 8;
  switch (type)
    {
    case 0:                     /* text */
      length += 10
        + status_length + PAD (2 + status_length)
        + 4 * feedbacks_length;
      break;

    case 1:                     /* pixmap */
      length += 4;
      break;

    default:
      return false;
    }

  p = data = malloc (length);
  if (!data)
    return false;

  memset (data, 0, length);

  PACK8 (transport, p, XCB_XIM_STATUS_DRAW);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (length - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);
  PACK32 (transport, p, type);

  switch (type)
    {
    case 0:                     /* text */
      PACK32 (transport, p, flag);
      PACK16 (transport, p, status_length);
      memcpy (p, status, status_length);
      p += status_length + PAD (2 + status_length);
      PACK16 (transport, p, 4 * feedbacks_length);
      PACK16 (transport, p, 0);
      for (i = 0; i < feedbacks_length; i++)
        PACK32 (transport, p, feedbacks[i]);
      break;

    case 1:                     /* pixmap */
      PACK32 (transport, p, pixmap);
      break;

    default:
      break;
    }

  success = write_data (xim, transport, p - data, data, error);
  free (data);

  return success;
}

bool
xcb_xim_status_done (xcb_xim_server_connection_t *xim,
                     xcb_xim_transport_t *transport,
                     uint16_t input_method_id,
                     uint16_t input_context_id,
                     xcb_generic_error_t **error)
{
  uint8_t data[8], *p = data;

  PACK8 (transport, p, XCB_XIM_STATUS_DONE);
  PACK8 (transport, p, 0);
  PACK16 (transport, p, (sizeof (data) - 4) / 4);

  PACK16 (transport, p, input_method_id);
  PACK16 (transport, p, input_context_id);

  return write_data (xim, transport, sizeof (data), data, error);
}

static bool
queue_request (xcb_xim_server_connection_t *xim,
               xcb_xim_request_container_t *container)
{
  struct xcb_xim_list_t *list;

  list = malloc (sizeof (*list));
  if (!list)
    return false;

  list->data = container;
  list->next = NULL;

  if (!xim->requests)
    xim->requests = xim->requests_tail = list;
  else
    {
      xim->requests_tail->next = list;
      xim->requests_tail = list;
    }

  return true;
}

xcb_xim_request_container_t *
xcb_xim_server_connection_poll_request (xcb_xim_server_connection_t *xim)
{
  xcb_xim_request_container_t *container;
  struct xcb_xim_list_t *list;

  if (!xim->requests)
    return NULL;

  list = xim->requests;
  xim->requests = xim->requests->next;
  if (!xim->requests)
    xim->requests_tail = NULL;

  container = list->data;
  free (list);

  return container;
}

static xcb_xim_dispatch_result_t
do_selection_request (xcb_xim_server_connection_t *xim,
                      xcb_selection_request_event_t *event,
                      xcb_generic_error_t **error)
{
  xcb_selection_notify_event_t reply;
  char *buffer;

  memset (&reply, 0, sizeof (reply));

  reply.response_type = XCB_SELECTION_NOTIFY;
  reply.time = event->time;
  reply.requestor = event->requestor;
  reply.selection = event->selection;
  reply.target = event->target;
  reply.property = event->property;

  buffer = NULL;
  if (event->target == xim->atoms[LOCALES])
    {
      if (asprintf (&buffer, "@locale=%s", xim->locale) < 1)
        return XCB_XIM_DISPATCH_ERROR;
    }
  else if (event->target == xim->atoms[TRANSPORT])
    {
      buffer = strdup ("@transport=X/");
      if (!buffer)
        return XCB_XIM_DISPATCH_ERROR;
    }

  xcb_change_property (xim->connection,
                       XCB_PROP_MODE_REPLACE,
                       event->requestor,
                       event->target,
                       event->target,
                       8,
                       strlen (buffer),
                       (unsigned char *) buffer);
  free (buffer);

  xcb_send_event (xim->connection,
                  false,
                  event->requestor,
                  XCB_EVENT_MASK_NO_EVENT,
                  (const char *) &reply);
  xcb_flush (xim->connection);

  return XCB_XIM_DISPATCH_REMOVE;
}

static xcb_xim_dispatch_result_t
do_client_message (xcb_xim_server_connection_t *xim,
                   xcb_client_message_event_t *event,
                   xcb_generic_error_t **error)
{
  if (event->type == xim->atoms[_XIM_XCONNECT])
    {
      if (!accept_connection (xim, event, error))
        return XCB_XIM_DISPATCH_ERROR;
    }
  else if (event->type == xim->atoms[_XIM_PROTOCOL])
    {
      xcb_xim_transport_t *transport;
      xcb_xim_request_container_t *container;
      uint8_t *data;
      size_t length;

      transport = find_transport (xim, event->window);
      if (!transport)
        return XCB_XIM_DISPATCH_ERROR;

      data = read_data (xim, transport, event, &length, error);
      if (!data)
        return XCB_XIM_DISPATCH_ERROR;

      container = malloc (sizeof (xcb_xim_transport_t *) + length);
      if (!container)
        return XCB_XIM_DISPATCH_ERROR;

      container->requestor = transport;
      memcpy (&container->request, data, length);

      switch (container->request.major_opcode)
        {
        case XCB_XIM_CONNECT:
          if (length < 8)
            goto error;

          transport->endian = data[4];
          if (!xcb_xim_connect_reply (xim, transport, 1, 0, error))
            goto error;
          break;

        case XCB_XIM_DISCONNECT:
          if (!xcb_xim_disconnect_reply (xim, transport, error))
            goto error;
          break;

        default:
          if (!queue_request (xim, container))
            goto error;
          break;
        }

      return XCB_XIM_DISPATCH_REMOVE;

    error:
      free (container);
      return XCB_XIM_DISPATCH_ERROR;
    }

  return XCB_XIM_DISPATCH_CONTINUE;
}

xcb_xim_dispatch_result_t
xcb_xim_server_connection_dispatch (xcb_xim_server_connection_t *xim,
                                    xcb_generic_event_t *event,
                                    xcb_generic_error_t **error)
{
  switch (event->response_type & ~0x80)
    {
    case XCB_SELECTION_REQUEST:
      return do_selection_request (xim,
                                   (xcb_selection_request_event_t *) event,
                                   error);

    case XCB_CLIENT_MESSAGE:
      return do_client_message (xim,
                                (xcb_client_message_event_t *) event,
                                error);

    default:
      return XCB_XIM_DISPATCH_CONTINUE;
    }
}
