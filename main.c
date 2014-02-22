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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <poll.h>
#include "text-client-protocol.h"
#include "xim.h"

#define SIZEOF(x) (sizeof (x) / sizeof(*x))

enum
  {
    QUERY_INPUT_STYLE,
    LAST_IM_ATTRIBUTE
  };

enum
  {
    INPUT_STYLE,
    FILTER_EVENTS,
    CLIENT_WINDOW,
    FOCUS_WINDOW,
    PREEDIT_ATTRIBUTES,
    STATUS_ATTRIBUTES,
    LAST_IC_ATTRIBUTE
  };

typedef struct xim_wayland_input_context_t xim_wayland_input_context_t;
typedef struct xim_wayland_input_method_t xim_wayland_input_method_t;
typedef struct xim_wayland_t xim_wayland_t;

struct xim_wayland_styling_t
{
  uint32_t index;
  uint32_t length;
  xcb_xim_feedback_t feedback;
  struct wl_list link;
};

typedef struct xim_wayland_styling_t xim_wayland_styling_t;

struct xim_wayland_input_context_t
{
  uint16_t id;
  struct wl_text_input *text_input;
  xim_wayland_input_method_t *input_method;
  struct wl_surface *surface;
  uint32_t serial;

  xcb_xim_attribute_t *attrs[LAST_IC_ATTRIBUTE];

  xim_wayland_t *xw;

  bool preedit_started;

  char *preedit_string;
  uint16_t preedit_length;
  int32_t preedit_caret;
  struct wl_list preedit_styling_list;

  struct wl_list link;
};

struct xim_wayland_input_method_t
{
  xcb_xim_transport_t *transport;
  uint16_t id;
  uint16_t input_context_counter;

  xcb_xim_attribute_spec_t *specs[LAST_IM_ATTRIBUTE];
  xcb_xim_attribute_t *attrs[LAST_IM_ATTRIBUTE];

  xcb_xim_attribute_spec_t *ic_specs[LAST_IC_ATTRIBUTE];

  struct wl_list input_context_list;
  struct wl_list link;
};

struct xim_wayland_t
{
  xcb_connection_t *connection;
  xcb_xim_server_connection_t *xim;
  uint16_t input_method_counter;

  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_seat *seat;
  struct wl_compositor *compositor;
  struct wl_text_input_manager *text_input_manager;

  struct wl_list input_method_list;
};

typedef struct xim_wayland_t xim_wayland_t;

static void
handle_wayland_enter (void *data,
                      struct wl_text_input *wl_text_input,
                      struct wl_surface *surface)
{
  xim_wayland_input_context_t *input_context = data;

  wl_text_input_commit_state (wl_text_input, ++input_context->serial);
}

static void
handle_wayland_leave (void *data,
                      struct wl_text_input *wl_text_input)
{
}

static void
handle_wayland_modifiers_map (void *data,
                              struct wl_text_input *wl_text_input,
                              struct wl_array *map)
{
}

static void
handle_wayland_input_panel_state (void *data,
                                  struct wl_text_input *wl_text_input,
                                  uint32_t state)
{
}

static void
reset_preedit (xim_wayland_input_context_t *input_context)
{
  xim_wayland_styling_t *preedit_styling, *next;

  wl_list_for_each_safe (preedit_styling, next,
                         &input_context->preedit_styling_list, link)
    {
      wl_list_remove (&preedit_styling->link);
      free (preedit_styling);
    }

  free (input_context->preedit_string);
  input_context->preedit_string = NULL;
  input_context->preedit_length = 0;
}

static bool
update_preedit_string (xim_wayland_input_context_t *input_context,
                       const char *text,
                       xcb_generic_error_t **error)
{
  xcb_xim_transport_t *transport = input_context->input_method->transport;

  if (*text == '\0')
    {
      if (!xcb_xim_preedit_draw (input_context->xw->xim,
                                 transport,
                                 input_context->input_method->id,
                                 input_context->id,
                                 strlen (text),
                                 0,
                                 input_context->preedit_length,
                                 0,
                                 strlen (text),
                                 (const uint8_t *) text,
                                 0,
                                 NULL,
                                 error))
        {
          reset_preedit (input_context);
          return false;
        }

      if (!input_context->preedit_started)
        {
          if (!xcb_xim_preedit_done (input_context->xw->xim,
                                     transport,
                                     input_context->input_method->id,
                                     input_context->id,
                                     error))
            {
              reset_preedit (input_context);
              return false;
            }

          input_context->preedit_started = false;
        }

      reset_preedit (input_context);
    }
  else
    {
      xim_wayland_styling_t *preedit_styling;
      xcb_xim_feedback_t *feedbacks;
      size_t length, i;

      if (!input_context->preedit_started)
        {
          if (!xcb_xim_preedit_start (input_context->xw->xim,
                                      transport,
                                      input_context->input_method->id,
                                      input_context->id,
                                      error))
            {
              reset_preedit (input_context);
              return false;
            }

          input_context->preedit_started = true;
        }

      length = strlen (text);

      feedbacks = calloc (sizeof (xcb_xim_feedback_t), length);

      wl_list_for_each (preedit_styling,
                        &input_context->preedit_styling_list, link)
        {
          if (preedit_styling->index + preedit_styling->length > length)
            continue;

          for (i = 0; i < preedit_styling->length; i++)
            feedbacks[i + preedit_styling->index] |= preedit_styling->feedback;
        }

      if (!xcb_xim_preedit_draw (input_context->xw->xim,
                                 transport,
                                 input_context->input_method->id,
                                 input_context->id,
                                 strlen (text),
                                 0,
                                 input_context->preedit_length,
                                 0,
                                 length,
                                 (const uint8_t *) text,
                                 length,
                                 feedbacks,
                                 error))
        {
          reset_preedit (input_context);
          return false;
        }

      free (input_context->preedit_string);
      input_context->preedit_string = strdup (text);
      input_context->preedit_length = length;
    }
  return true;
}

static void
handle_wayland_preedit_string (void *data,
                               struct wl_text_input *wl_text_input,
                               uint32_t serial,
                               const char *text,
                               const char *commit)
{
  xim_wayland_input_context_t *input_context = data;
  xcb_xim_transport_t *transport = input_context->input_method->transport;
  uint32_t input_style =
    xcb_xim_card32 (transport,
                    *(uint32_t *) (input_context->attrs[INPUT_STYLE] + 1));

  if ((input_style & XCB_XIM_PREEDIT_CALLBACKS) != 0)
    {
      xcb_generic_error_t *error;

      error = NULL;
      if (!update_preedit_string (input_context, text, &error))
        {
          if (error)
            {
              fprintf (stderr, "can't render preedit: %i\n",
                       error->error_code);
              free (error);
            }
          else
            fprintf (stderr, "can't render preedit\n");
        }
    }
  else
    fprintf (stderr, "preedit callbacks not supported by this client\n");
}

static void
handle_wayland_preedit_styling (void *data,
                                struct wl_text_input *wl_text_input,
                                uint32_t index,
                                uint32_t length,
                                uint32_t style)
{
  xim_wayland_input_context_t *input_context = data;
  xcb_xim_feedback_t feedback;
  xim_wayland_styling_t *styling;

  switch (style)
    {
    case WL_TEXT_INPUT_PREEDIT_STYLE_HIGHLIGHT:
      feedback = XCB_XIM_FEEDBACK_HIGHLIGHT;
      break;

    case WL_TEXT_INPUT_PREEDIT_STYLE_UNDERLINE:
      feedback = XCB_XIM_FEEDBACK_UNDERLINE;
      break;

    case WL_TEXT_INPUT_PREEDIT_STYLE_ACTIVE:
      feedback = XCB_XIM_FEEDBACK_PRIMARY;
      break;

    case WL_TEXT_INPUT_PREEDIT_STYLE_INACTIVE:
      feedback = XCB_XIM_FEEDBACK_SECONDARY;
      break;

    case WL_TEXT_INPUT_PREEDIT_STYLE_SELECTION:
      feedback = XCB_XIM_FEEDBACK_REVERSE;

    default:
      return;
    }

  styling = calloc (1, sizeof (xim_wayland_styling_t));
  if (!styling)
    return;

  styling->index = index;
  styling->length = length;
  styling->feedback = feedback;

  wl_list_insert (&input_context->preedit_styling_list, &styling->link);
}

static void
handle_wayland_preedit_cursor (void *data,
                               struct wl_text_input *wl_text_input,
                               int32_t index)
{
  xim_wayland_input_context_t *input_context = data;
  xcb_xim_transport_t *transport = input_context->input_method->transport;
  xcb_generic_error_t *error;

  error = NULL;
  if (!xcb_xim_preedit_caret (input_context->xw->xim,
                              transport,
                              input_context->input_method->id,
                              input_context->id,
                              index,
                              XCB_XIM_CARET_DIRECTION_ABSOLUTE_POSITION,
                              XCB_XIM_CARET_STYLE_PRIMARY,
                              &error))
    {
      if (error)
        {
          fprintf (stderr, "can't set caret position: %i\n",
                   error->error_code);
          free (error);
        }
      else
        fprintf (stderr, "can't set caret position\n");
    }
}

static void
handle_wayland_commit_string (void *data,
                              struct wl_text_input *wl_text_input,
                              uint32_t serial,
                              const char *text)
{
  xim_wayland_input_context_t *input_context = data;
  xcb_generic_error_t *error;

  error = NULL;
  if (!update_preedit_string (input_context, "", &error))
    {
      if (error)
        {
          fprintf (stderr, "can't clear preedit: %i\n",
                   error->error_code);
          free (error);
        }
      else
        fprintf (stderr, "can't clear preedit\n");
    }

  error = NULL;
  if (!xcb_xim_commit (input_context->xw->xim,
                       input_context->input_method->transport,
                       input_context->input_method->id,
                       input_context->id,
                       XCB_XIM_COMMIT_FLAG_KEYSYM | XCB_XIM_COMMIT_FLAG_STRING,
                       0xffffff,
                       strlen (text),
                       (const uint8_t *) text,
                       &error))
    {
      if (error)
        {
          fprintf (stderr, "can't commit: %i\n",
                   error->error_code);
          free (error);
        }
      else
        fprintf (stderr, "can't commit\n");
    }
}

static void
handle_wayland_cursor_position (void *data,
                                struct wl_text_input *wl_text_input,
                                int32_t index,
                                int32_t anchor)
{
}

static void
handle_wayland_delete_surrounding_text (void *data,
                                        struct wl_text_input *wl_text_input,
                                        int32_t index,
                                        uint32_t length)
{
}

static void
handle_wayland_keysym (void *data,
                       struct wl_text_input *wl_text_input,
                       uint32_t serial,
                       uint32_t time,
                       uint32_t sym,
                       uint32_t state,
                       uint32_t modifiers)
{
}

static void
handle_wayland_language (void *data,
                         struct wl_text_input *wl_text_input,
                         uint32_t serial,
                         const char *language)
{
}

static void
handle_wayland_text_direction (void *data,
                               struct wl_text_input *wl_text_input,
                               uint32_t serial,
                               uint32_t direction)
{
}

static const struct wl_text_input_listener
text_input_listener =
  {
    handle_wayland_enter,
    handle_wayland_leave,
    handle_wayland_modifiers_map,
    handle_wayland_input_panel_state,
    handle_wayland_preedit_string,
    handle_wayland_preedit_styling,
    handle_wayland_preedit_cursor,
    handle_wayland_commit_string,
    handle_wayland_cursor_position,
    handle_wayland_delete_surrounding_text,
    handle_wayland_keysym,
    handle_wayland_language,
    handle_wayland_text_direction,
  };

static void
init_im_attributes (xim_wayland_input_method_t *input_method)
{
  uint32_t value[] =
    {
      XCB_XIM_PREEDIT_CALLBACKS | XCB_XIM_STATUS_CALLBACKS,
      XCB_XIM_PREEDIT_CALLBACKS | XCB_XIM_STATUS_NOTHING,
      XCB_XIM_PREEDIT_NOTHING | XCB_XIM_STATUS_NOTHING
    };

  input_method->specs[QUERY_INPUT_STYLE] =
    xcb_xim_attribute_spec_new (input_method->transport,
                                QUERY_INPUT_STYLE,
                                XCB_XIM_TYPE_XIMSTYLES,
                                strlen ("queryInputStyle"),
                                "queryInputStyle");

  input_method->attrs[QUERY_INPUT_STYLE] =
    xcb_xim_attribute_styles_new (input_method->transport,
                                  QUERY_INPUT_STYLE,
                                  SIZEOF (value),
                                  value);

  input_method->ic_specs[INPUT_STYLE] =
    xcb_xim_attribute_spec_new (input_method->transport,
                                INPUT_STYLE,
                                XCB_XIM_TYPE_CARD32,
                                strlen ("inputStyle"),
                                "inputStyle");

  input_method->ic_specs[FILTER_EVENTS] =
    xcb_xim_attribute_spec_new (input_method->transport,
                                FILTER_EVENTS,
                                XCB_XIM_TYPE_CARD32,
                                strlen ("filterEvents"),
                                "filterEvents");

  input_method->ic_specs[CLIENT_WINDOW] =
    xcb_xim_attribute_spec_new (input_method->transport,
                                CLIENT_WINDOW,
                                XCB_XIM_TYPE_WINDOW,
                                strlen ("clientWindow"),
                                "clientWindow");

  input_method->ic_specs[FOCUS_WINDOW] =
    xcb_xim_attribute_spec_new (input_method->transport,
                                FOCUS_WINDOW,
                                XCB_XIM_TYPE_WINDOW,
                                strlen ("focusWindow"),
                                "focusWindow");

  input_method->ic_specs[PREEDIT_ATTRIBUTES] =
    xcb_xim_attribute_spec_new (input_method->transport,
                                PREEDIT_ATTRIBUTES,
                                XCB_XIM_TYPE_NEST,
                                strlen ("preeditAttributes"),
                                "preeditAttributes");

  input_method->ic_specs[STATUS_ATTRIBUTES] =
    xcb_xim_attribute_spec_new (input_method->transport,
                                STATUS_ATTRIBUTES,
                                XCB_XIM_TYPE_NEST,
                                strlen ("statusAttributes"),
                                "statusAttributes");
}

static void
init_ic_attributes (xim_wayland_input_context_t *input_context)
{
  xcb_xim_transport_t *transport = input_context->input_method->transport;

  input_context->attrs[INPUT_STYLE] =
    xcb_xim_attribute_card32_new (transport,
                                  INPUT_STYLE,
                                  XCB_XIM_PREEDIT_CALLBACKS
                                  | XCB_XIM_STATUS_CALLBACKS);

  input_context->attrs[FILTER_EVENTS] =
    xcb_xim_attribute_card32_new (transport,
                                  FILTER_EVENTS,
                                  0);

  input_context->attrs[CLIENT_WINDOW] =
    xcb_xim_attribute_card32_new (transport,
                                  CLIENT_WINDOW,
                                  0);

  input_context->attrs[FOCUS_WINDOW] =
    xcb_xim_attribute_card32_new (transport,
                                  FOCUS_WINDOW,
                                  0);
}

static xim_wayland_input_context_t *
xim_wayland_input_context_new (xim_wayland_t *xw,
                               xim_wayland_input_method_t *input_method,
                               uint16_t id)
{
  xim_wayland_input_context_t *input_context;

  input_context = calloc (1, sizeof (xim_wayland_input_context_t));
  if (!input_context)
    return NULL;

  input_context->xw = xw;
  input_context->text_input =
    wl_text_input_manager_create_text_input (xw->text_input_manager);
  if (!input_context->text_input)
    {
      free (input_context);
      return NULL;
    }

  wl_text_input_add_listener (input_context->text_input,
                              &text_input_listener, input_context);

  input_context->surface =
    wl_compositor_create_surface (xw->compositor);
  if (!input_context->surface)
    {
      wl_text_input_destroy (input_context->text_input);
      free (input_context);
      return NULL;
    }

  input_context->id = id;
  input_context->input_method = input_method;
  wl_list_init (&input_context->preedit_styling_list);

  init_ic_attributes (input_context);

  return input_context;
}

static void
xim_wayland_input_context_free (xim_wayland_input_context_t *input_context)
{
  int i;

  for (i = 0; i < SIZEOF (input_context->attrs); i++)
    free (input_context->attrs[i]);

  wl_text_input_destroy (input_context->text_input);
  wl_surface_destroy (input_context->surface);

  free (input_context);
}

static xim_wayland_input_method_t *
xim_wayland_input_method_new (xcb_xim_transport_t *transport,
                              uint16_t id)
{
  xim_wayland_input_method_t *input_method;

  input_method = calloc (1, sizeof (xim_wayland_input_method_t));
  if (!input_method)
    return NULL;

  input_method->transport = transport;
  input_method->id = id;

  init_im_attributes (input_method);

  wl_list_init (&input_method->input_context_list);

  return input_method;
}

static void
xim_wayland_input_method_free (xim_wayland_input_method_t *input_method)
{
  xim_wayland_input_context_t *input_context, *next;
  int i;

  wl_list_for_each_safe (input_context, next,
                         &input_method->input_context_list, link)
    {
      wl_list_remove (&input_context->link);
      xim_wayland_input_context_free (input_context);
    }

  for (i = 0; i < SIZEOF (input_method->attrs); i++)
    free (input_method->attrs[i]);

  for (i = 0; i < SIZEOF (input_method->specs); i++)
    free (input_method->specs[i]);

  for (i = 0; i < SIZEOF (input_method->ic_specs); i++)
    free (input_method->ic_specs[i]);

  free (input_method);
}

static xim_wayland_input_context_t *
find_input_context (xim_wayland_input_method_t *input_method, uint16_t id)
{
  xim_wayland_input_context_t *input_context;

  wl_list_for_each (input_context, &input_method->input_context_list, link)
    {
      if (input_context->id == id)
        return input_context;
    }

  return NULL;
}

static xim_wayland_input_method_t *
find_input_method (xim_wayland_t *xw,
                   xcb_xim_transport_t *transport,
                   uint16_t id)
{
  xim_wayland_input_method_t *input_method;

  wl_list_for_each (input_method, &xw->input_method_list, link)
    {
      if (input_method->transport == transport && input_method->id == id)
        return input_method;
    }

  return NULL;
}

static bool
handle_xim_open_request (xim_wayland_t *xw,
                         xcb_xim_generic_request_t *request,
                         xcb_xim_transport_t *requestor,
                         xcb_generic_error_t **error)
{
  xim_wayland_input_method_t *input_method;
  bool success;

  input_method = xim_wayland_input_method_new (requestor,
                                               ++xw->input_method_counter);
  if (!input_method)
    return false;

  success = xcb_xim_open_reply (xw->xim,
                                requestor,
                                input_method->id,
                                LAST_IM_ATTRIBUTE,
                                input_method->specs,
                                LAST_IC_ATTRIBUTE,
                                input_method->ic_specs,
                                error);

  if (!success)
    {
      xim_wayland_input_method_free (input_method);
      return false;
    }

  wl_list_insert (&xw->input_method_list, &input_method->link);
  return success;
}

static bool
handle_xim_close_request (xim_wayland_t *xw,
                          xcb_xim_generic_request_t *request,
                          xcb_xim_transport_t *requestor,
                          xcb_generic_error_t **error)
{
  xcb_xim_close_request_t *_close = (xcb_xim_close_request_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _close->input_method_id);
  xim_wayland_input_method_t *input_method;

  input_method = find_input_method (xw, requestor, input_method_id);
  if (!input_method)
    return false;

  wl_list_remove (&input_method->link);
  xim_wayland_input_method_free (input_method);

  return xcb_xim_close_reply (xw->xim,
                              requestor,
                              input_method_id,
                              error);
}

static bool
handle_xim_query_extension_request (xim_wayland_t *xw,
                                    xcb_xim_generic_request_t *request,
                                    xcb_xim_transport_t *requestor,
                                    xcb_generic_error_t **error)
{
  xcb_xim_query_extension_request_t *_query_extension =
    (xcb_xim_query_extension_request_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _query_extension->input_method_id);

  return xcb_xim_query_extension_reply (xw->xim,
                                        requestor,
                                        input_method_id,
                                        0,
                                        NULL,
                                        error);
}

static bool
handle_xim_encoding_negotiation_request (xim_wayland_t *xw,
                                         xcb_xim_generic_request_t *request,
                                         xcb_xim_transport_t *requestor,
                                         xcb_generic_error_t **error)
{
  xcb_xim_str_iterator_t iterator;
  xcb_xim_encoding_negotiation_request_t *_encoding_negotiation =
    (xcb_xim_encoding_negotiation_request_t *) request;
  uint16_t input_method_id =
    xcb_xim_card16 (requestor,
                    _encoding_negotiation->input_method_id);

  /* FIXME: only support UTF-8 at the moment */
  iterator =
    xcb_xim_encoding_negotiation_request_encoding_iterator (_encoding_negotiation);
  while (xcb_xim_str_iterator_has_data (&iterator))
    {
      xcb_xim_str_t *str = iterator.data;

      if (strncmp ((const char *) (str + 1), "UTF-8", str->length) == 0)
        break;
      xcb_xim_str_iterator_next (&iterator);
    }
  if (!xcb_xim_str_iterator_has_data (&iterator))
    return false;

  return xcb_xim_encoding_negotiation_reply (xw->xim,
                                             requestor,
                                             input_method_id,
                                             0,
                                             iterator.index,
                                             error);
}

static void
set_values (xcb_xim_transport_t *transport,
            xcb_xim_attribute_t **attributes,
            uint16_t max_attribute_id,
            xcb_xim_attribute_iterator_t iterator)
{
  for (; xcb_xim_attribute_iterator_has_data (&iterator);
       xcb_xim_attribute_iterator_next (&iterator))
    {
      xcb_xim_attribute_t *attribute = iterator.data;
      xcb_xim_attribute_t *attribute_copy;
      uint16_t attribute_id = xcb_xim_card16 (transport,
                                              attribute->attribute_id);
      uint16_t attribute_byte_length =
        4 + xcb_xim_card16 (transport,
                            attribute->value_byte_length);

      if (attribute_id >= max_attribute_id)
        continue;

      attribute_copy = malloc (attribute_byte_length);
      if (!attribute_copy)
        continue;

      memcpy (attribute_copy, attribute, attribute_byte_length);

      free (attributes[attribute_id]);
      attributes[attribute_id] = attribute_copy;
    }
}

static bool
handle_xim_set_im_values_request (xim_wayland_t *xw,
                                  xcb_xim_generic_request_t *request,
                                  xcb_xim_transport_t *requestor,
                                  xcb_generic_error_t **error)
{
  xcb_xim_set_im_values_request_t *_set_im_values =
    (xcb_xim_set_im_values_request_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _set_im_values->input_method_id);
  xim_wayland_input_method_t *input_method;
  xcb_xim_attribute_iterator_t iterator;

  input_method = find_input_method (xw, requestor, input_method_id);
  if (!input_method)
    return false;

  iterator = xcb_xim_set_im_values_request_attribute_iterator (_set_im_values);
  set_values (requestor,
              input_method->attrs,
              LAST_IM_ATTRIBUTE,
              iterator);

  return xcb_xim_set_im_values_reply (xw->xim,
                                      requestor,
                                      input_method_id,
                                      error);
}

static bool
handle_xim_get_im_values_request (xim_wayland_t *xw,
                                  xcb_xim_generic_request_t *request,
                                  xcb_xim_transport_t *requestor,
                                  xcb_generic_error_t **error)
{
  xcb_xim_get_im_values_request_t *_get_im_values =
    (xcb_xim_get_im_values_request_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _get_im_values->input_method_id);
  xim_wayland_input_method_t *input_method;
  xcb_xim_attribute_id_iterator_t iterator;
  uint16_t attributes_length;
  uint16_t max_attributes_length;
  xcb_xim_attribute_t **attributes;
  bool success;

  input_method = find_input_method (xw, requestor, input_method_id);
  if (!input_method)
    return false;

  max_attributes_length = 0;
  attributes_length = 0;
  attributes = NULL;

  for (iterator =
         xcb_xim_get_im_values_request_attribute_id_iterator (_get_im_values);
       xcb_xim_attribute_id_iterator_has_data (&iterator);
       xcb_xim_attribute_id_iterator_next (&iterator))
    {
      uint16_t attribute_id = xcb_xim_card16 (requestor,
                                              *iterator.data);

      if (attribute_id >= LAST_IM_ATTRIBUTE)
        continue;

      if (attributes_length == max_attributes_length)
        {
          max_attributes_length = max_attributes_length * 2 + 10;
          attributes =
            realloc (attributes,
                     sizeof (xcb_xim_attribute_t *) * max_attributes_length);
        }

      attributes[attributes_length++] = input_method->attrs[attribute_id];
    }

  success = xcb_xim_get_im_values_reply (xw->xim,
                                         requestor,
                                         input_method_id,
                                         attributes_length,
                                         attributes,
                                         error);
  free (attributes);
  return success;
}

static bool
handle_xim_create_ic_request (xim_wayland_t *xw,
                              xcb_xim_generic_request_t *request,
                              xcb_xim_transport_t *requestor,
                              xcb_generic_error_t **error)
{
  xcb_xim_create_ic_request_t *_create_ic =
    (xcb_xim_create_ic_request_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _create_ic->input_method_id);
  xim_wayland_input_method_t *input_method;
  xim_wayland_input_context_t *input_context;
  xcb_xim_attribute_iterator_t iterator;
  bool success;

  input_method = find_input_method (xw, requestor, input_method_id);
  if (!input_method)
    return false;

  input_context =
    xim_wayland_input_context_new (xw,
                                   input_method,
                                   ++input_method->input_context_counter);
  if (!input_context)
    return false;

  iterator = xcb_xim_create_ic_request_attribute_iterator (_create_ic);
  set_values (requestor,
              input_context->attrs,
              LAST_IC_ATTRIBUTE,
              iterator);

  success = xcb_xim_create_ic_reply (xw->xim,
                                     requestor,
                                     input_method_id,
                                     input_context->id,
                                     error);
  if (!success)
    {
      xim_wayland_input_context_free (input_context);
      return false;
    }

  wl_list_insert (&input_method->input_context_list, &input_context->link);
  return success;
}

static bool
handle_xim_destroy_ic_request (xim_wayland_t *xw,
                               xcb_xim_generic_request_t *request,
                               xcb_xim_transport_t *requestor,
                               xcb_generic_error_t **error)
{
  xcb_xim_destroy_ic_request_t *_destroy_ic =
    (xcb_xim_destroy_ic_request_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _destroy_ic->input_method_id);
  uint16_t input_context_id = xcb_xim_card16 (requestor,
                                              _destroy_ic->input_context_id);
  xim_wayland_input_method_t *input_method;
  xim_wayland_input_context_t *input_context;

  input_method = find_input_method (xw, requestor, input_method_id);
  if (!input_method)
    return false;

  input_context = find_input_context (input_method, input_context_id);
  if (!input_context)
    return false;

  wl_list_remove (&input_context->link);
  xim_wayland_input_context_free (input_context);

  return xcb_xim_destroy_ic_reply (xw->xim,
                                   requestor,
                                   input_method_id,
                                   input_context_id,
                                   error);
}

static bool
handle_xim_set_ic_values_request (xim_wayland_t *xw,
                                  xcb_xim_generic_request_t *request,
                                  xcb_xim_transport_t *requestor,
                                  xcb_generic_error_t **error)
{
  xcb_xim_set_ic_values_request_t *_set_ic_values =
    (xcb_xim_set_ic_values_request_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _set_ic_values->input_method_id);
  uint16_t input_context_id = xcb_xim_card16 (requestor,
                                              _set_ic_values->input_context_id);
  xim_wayland_input_method_t *input_method;
  xim_wayland_input_context_t *input_context;
  xcb_xim_attribute_iterator_t iterator;

  input_method = find_input_method (xw, requestor, input_method_id);
  if (!input_method)
    return false;

  input_context = find_input_context (input_method, input_context_id);
  if (!input_context)
    return false;

  iterator = xcb_xim_set_ic_values_request_attribute_iterator (_set_ic_values);
  set_values (requestor,
              input_context->attrs,
              LAST_IC_ATTRIBUTE,
              iterator);

  return xcb_xim_set_ic_values_reply (xw->xim,
                                      requestor,
                                      input_method_id,
                                      input_context_id,
                                      error);
}

static bool
handle_xim_get_ic_values_request (xim_wayland_t *xw,
                                  xcb_xim_generic_request_t *request,
                                  xcb_xim_transport_t *requestor,
                                  xcb_generic_error_t **error)
{
  xcb_xim_get_ic_values_request_t *_get_ic_values =
    (xcb_xim_get_ic_values_request_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _get_ic_values->input_method_id);
  uint16_t input_context_id = xcb_xim_card16 (requestor,
                                              _get_ic_values->input_context_id);
  xim_wayland_input_method_t *input_method;
  xim_wayland_input_context_t *input_context;
  xcb_xim_attribute_id_iterator_t iterator;
  uint16_t attributes_length;
  uint16_t max_attributes_length;
  xcb_xim_attribute_t **attributes;
  bool success;

  input_method = find_input_method (xw, requestor, input_method_id);
  if (!input_method)
    return false;

  input_context = find_input_context (input_method, input_context_id);
  if (!input_context)
    return false;

  max_attributes_length = 0;
  attributes_length = 0;
  attributes = NULL;

  for (iterator =
         xcb_xim_get_ic_values_request_attribute_id_iterator (_get_ic_values);
       xcb_xim_attribute_id_iterator_has_data (&iterator);
       xcb_xim_attribute_id_iterator_next (&iterator))
    {
      uint16_t attribute_id = xcb_xim_card16 (requestor,
                                              *iterator.data);

      if (attribute_id >= LAST_IC_ATTRIBUTE)
        continue;

      if (attributes_length == max_attributes_length)
        {
          max_attributes_length = max_attributes_length * 2 + 10;
          attributes =
            realloc (attributes,
                     sizeof (xcb_xim_attribute_t *) * max_attributes_length);
        }

      attributes[attributes_length++] = input_context->attrs[attribute_id];
    }

  success = xcb_xim_get_ic_values_reply (xw->xim,
                                         requestor,
                                         input_method_id,
                                         input_context_id,
                                         attributes_length,
                                         attributes,
                                         error);
  free (attributes);
  return success;
}

static bool
handle_xim_set_ic_focus_request (xim_wayland_t *xw,
                                 xcb_xim_generic_request_t *request,
                                 xcb_xim_transport_t *requestor,
                                 xcb_generic_error_t **error)
{
  xcb_xim_set_ic_focus_request_t *_set_ic_focus =
    (xcb_xim_set_ic_focus_request_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _set_ic_focus->input_method_id);
  uint16_t input_context_id = xcb_xim_card16 (requestor,
                                              _set_ic_focus->input_context_id);
  xim_wayland_input_method_t *input_method;
  xim_wayland_input_context_t *input_context;

  input_method = find_input_method (xw, requestor, input_method_id);
  if (!input_method)
    return false;

  input_context = find_input_context (input_method, input_context_id);
  if (!input_context)
    return false;

  if (!input_context->text_input)
    return false;

  wl_text_input_show_input_panel (input_context->text_input);
  wl_text_input_activate (input_context->text_input,
                          xw->seat,
                          input_context->surface);
  wl_display_flush (xw->display);

  return true;
}

static bool
handle_xim_unset_ic_focus_request (xim_wayland_t *xw,
                                   xcb_xim_generic_request_t *request,
                                   xcb_xim_transport_t *requestor,
                                   xcb_generic_error_t **error)
{
  xcb_xim_unset_ic_focus_request_t *_unset_ic_focus =
    (xcb_xim_unset_ic_focus_request_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _unset_ic_focus->input_method_id);
  uint16_t input_context_id = xcb_xim_card16 (requestor,
                                              _unset_ic_focus->input_context_id);
  xim_wayland_input_method_t *input_method;
  xim_wayland_input_context_t *input_context;

  input_method = find_input_method (xw, requestor, input_method_id);
  if (!input_method)
    return false;

  input_context = find_input_context (input_method, input_context_id);
  if (!input_context)
    return false;

  if (!input_context->text_input)
    return false;

  wl_text_input_deactivate (input_context->text_input,
                            xw->seat);
  return true;
}

static bool
handle_xim_preedit_caret_reply (xim_wayland_t *xw,
                                xcb_xim_generic_request_t *request,
                                xcb_xim_transport_t *requestor,
                                xcb_generic_error_t **error)
{
  xcb_xim_preedit_caret_reply_t *_preedit_caret =
    (xcb_xim_preedit_caret_reply_t *) request;
  uint16_t input_method_id = xcb_xim_card16 (requestor,
                                             _preedit_caret->input_method_id);
  uint16_t input_context_id = xcb_xim_card16 (requestor,
                                              _preedit_caret->input_context_id);
  uint32_t position = xcb_xim_card32 (requestor,
                                      _preedit_caret->position);
  xim_wayland_input_method_t *input_method;
  xim_wayland_input_context_t *input_context;

  input_method = find_input_method (xw, requestor, input_method_id);
  if (!input_method)
    return false;

  input_context = find_input_context (input_method, input_context_id);
  if (!input_context)
    return false;

  if (!input_context->text_input)
    return false;

  if (position > input_context->preedit_length)
    input_context->preedit_caret = position;

  return true;
}

typedef bool (* xim_wayland_xim_request_handler_t) (
  xim_wayland_t *xw,
  xcb_xim_generic_request_t *request,
  xcb_xim_transport_t *requestor,
  xcb_generic_error_t **error);

static const struct
{
  uint8_t major_opcode;
  xim_wayland_xim_request_handler_t handler;
} xim_request_handlers[] =
  {
    { XCB_XIM_OPEN, handle_xim_open_request },
    { XCB_XIM_CLOSE, handle_xim_close_request },
    { XCB_XIM_QUERY_EXTENSION, handle_xim_query_extension_request },
    { XCB_XIM_ENCODING_NEGOTIATION, handle_xim_encoding_negotiation_request },
    { XCB_XIM_SET_IM_VALUES, handle_xim_set_im_values_request },
    { XCB_XIM_GET_IM_VALUES, handle_xim_get_im_values_request },
    { XCB_XIM_CREATE_IC, handle_xim_create_ic_request },
    { XCB_XIM_DESTROY_IC, handle_xim_destroy_ic_request },
    { XCB_XIM_SET_IC_VALUES, handle_xim_set_ic_values_request },
    { XCB_XIM_GET_IC_VALUES, handle_xim_get_ic_values_request },
    { XCB_XIM_SET_IC_FOCUS, handle_xim_set_ic_focus_request },
    { XCB_XIM_UNSET_IC_FOCUS, handle_xim_unset_ic_focus_request },
    { XCB_XIM_PREEDIT_CARET_REPLY, handle_xim_preedit_caret_reply }
    /* Note that we intentionally ignore XIM_FORWARD_EVENT request,
       because key press/release events are directly delivered to
       input method under Wayland.  */
  };

static bool
handle_xim_request (xim_wayland_t *xw,
                    xcb_xim_generic_request_t *request,
                    xcb_xim_transport_t *requestor,
                    xcb_generic_error_t **error)
{
  int i;

  for (i = 0; i < SIZEOF (xim_request_handlers); i++)
    {
      if (xim_request_handlers[i].major_opcode == request->major_opcode)
        return xim_request_handlers[i].handler (xw, request, requestor, error);
    }

  return true;
}

static bool
handle_wayland_events (xim_wayland_t *xw)
{
  int ret;

  ret = wl_display_dispatch (xw->display);
  if (ret < 0)
    return false;

  return true;
}

static bool
handle_x_events (xim_wayland_t *xw)
{
  xcb_generic_event_t *event;

  while ((event = xcb_poll_for_event (xw->connection)) != NULL)
    {
      xcb_xim_dispatch_result_t result;
      xcb_xim_request_container_t *container;
      xcb_generic_error_t *error;

      error = NULL;
      result = xcb_xim_server_connection_dispatch (xw->xim, event, &error);

      switch (result)
        {
        case XCB_XIM_DISPATCH_ERROR:     /* Error in dispatching.  */
          if (error)
            {
              fprintf (stderr, "can't dispatch XIM message: %i\n",
                       error->error_code);
              free (error);
            }
          else
            fprintf (stderr, "can't dispatch XIM message\n");
          free (event);
          return false;

        case XCB_XIM_DISPATCH_CONTINUE:
          /* Ignore unrelated events.  */

        case XCB_XIM_DISPATCH_REMOVE:
          break;
        }

      while ((container = xcb_xim_server_connection_poll_request (xw->xim))
             != NULL)
        {
          uint8_t major_opcode = container->request.major_opcode;
          bool success;

          error = NULL;
          success = handle_xim_request (xw,
                                        &container->request,
                                        container->requestor,
                                        &error);
          free (container);

          if (!success)
            {
              if (error)
                {
                  fprintf (stderr, "can't handle XIM request %i: %i\n",
                           major_opcode,
                           error->error_code);
                  free (error);
                }
              else
                fprintf (stderr, "can't handle XIM request %i\n",
                         major_opcode);
              free (event);
              return false;
            }
        }
      free (event);
    }
  return true;
}

static bool
main_loop (xim_wayland_t *xw)
{
  struct pollfd fds[2];

  memset (fds, 0, sizeof (fds));

  fds[0].fd = wl_display_get_fd (xw->display);
  fds[0].events = POLLIN | POLLERR | POLLHUP;

  fds[1].fd = xcb_get_file_descriptor (xw->connection);
  fds[1].events = POLLIN | POLLERR | POLLHUP;

  while (true)
    {
      if (poll (fds, SIZEOF (fds), -1) < 0)
        return false;

      if ((fds[0].revents & (POLLERR | POLLHUP)) != 0)
        {
          fprintf (stderr, "lost connection to Wayland display\n");
          return false;
        }

      if ((fds[1].revents & (POLLERR | POLLHUP)) != 0)
        {
          fprintf (stderr, "lost connection to X display\n");
          return false;
        }

      if (fds[0].revents)
        {
          if (!handle_wayland_events (xw))
            return false;
          fds[0].revents = 0;
        }

      if (fds[1].revents)
        {
          if (!handle_x_events (xw))
            return false;
          fds[1].revents = 0;
        }
    }

  return true;
}

static void
registry_handle_global (void *data,
                        struct wl_registry *registry,
                        uint32_t id,
                        const char *interface,
                        uint32_t version)
{
  xim_wayland_t *xw = data;

  if (strcmp (interface, "wl_text_input_manager") == 0)
    xw->text_input_manager =
      wl_registry_bind (registry, id, &wl_text_input_manager_interface, 1);
  else if (strcmp (interface, "wl_seat") == 0)
    xw->seat =
      wl_registry_bind (registry, id, &wl_seat_interface, 1);
  else if (strcmp (interface, "wl_compositor") == 0)
    xw->compositor =
      wl_registry_bind (registry, id, &wl_compositor_interface, 1);
}

static void
registry_handle_global_remove (void *data,
                               struct wl_registry *registry,
                               uint32_t name)
{
}

static const struct wl_registry_listener
registry_listener =
  {
    registry_handle_global,
    registry_handle_global_remove
  };

static void
print_usage (FILE *stream)
{
  fprintf (stream,
           "Usage: xim-wayland OPTIONS...\n"
           "where OPTIONS are:\n"
           "  --locale, -l=LOCALE  Specify locale (default: C,en)\n"
           "  --help, -h           Show this help\n");
}

#define LOCALES "C,en"

int
main (int argc, char **argv)
{
  int c;
  char *opt_locale;
  xim_wayland_t xw;
  xim_wayland_input_method_t *input_method, *next;
  xcb_generic_error_t *error;
  bool success;

  opt_locale = NULL;
  success = true;

  while (true)
    {
      int option_index;
      static struct option long_options[] =
        {
          { "locale", required_argument, 0, 'l' },
          { "help", no_argument, 0, 'h' },
          { NULL, 0, 0, 0 }
        };

      c = getopt_long (argc, argv, "hl:", long_options, &option_index);
      if (c == -1)
        break;

      switch (c)
        {
        case 'h':
          print_usage (stdout);
          goto out;
          break;

        case 'l':
          opt_locale = strdup (optarg);
          break;

        default:
          success = false;
          print_usage (stderr);
          goto out;
          break;
        }
    }

  if (!opt_locale)
    opt_locale = strdup (LOCALES);

  memset (&xw, 0, sizeof (xw));
  wl_list_init (&xw.input_method_list);

  xw.display = wl_display_connect (NULL);
  if (!xw.display)
    {
      success = false;
      fprintf (stderr, "cannot open Wayland display\n");
      goto out;
    }

  xw.registry = wl_display_get_registry (xw.display);

  wl_registry_add_listener (xw.registry, &registry_listener, &xw);
  wl_display_dispatch (xw.display);

  xw.connection = xcb_connect (NULL, NULL);
  if (!xw.connection)
    {
      success = false;
      fprintf (stderr, "cannot open X display\n");
      goto out;
    }

  error = NULL;
  xw.xim = xcb_xim_server_connection_new (xw.connection,
                                          "wayland",
                                          opt_locale,
                                          &error);
  if (!xw.xim)
    {
      success = false;
      if (error)
        {
          fprintf (stderr, "can't create XIM server: %i\n",
                   error->error_code);
          free (error);
        }
      else
        fprintf (stderr, "can't create XIM server\n");

      goto out;
    }

  success = main_loop (&xw);

 out:
  wl_registry_destroy (xw.registry);

  wl_list_for_each_safe (input_method, next,
                         &xw.input_method_list, link)
    {
      wl_list_remove (&input_method->link);
      xim_wayland_input_method_free (input_method);
    }

  if (xw.display)
    wl_display_disconnect (xw.display);

  if (xw.xim)
    xcb_xim_server_connection_free (xw.xim);

  if (xw.connection)
    xcb_disconnect (xw.connection);

  free (opt_locale);

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
