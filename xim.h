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

#ifndef __XCB_XIM_H__
#define __XCB_XIM_H__

#include <stdbool.h>
#include <xcb/xcb.h>

/* This library defines an XCB binding of XIM server protocol.
   No support for XIM client protocol at the moment.

   Note:

   - All structs that represent wire format
     (e.g. xcb_xim_*_{request,reply,attribute}_t) are in client byte
     order, while non-struct function arguments are in host byte
     order.  To access an integer field of a struct, you may need to
     convert byte order according to ENDIAN member of
     xcb_xim_transport_t.

   - All structs that represent wire format do not have padding at the
     end.  For example, xcb_xim_*_request_t are 4-byte aligned and do
     not have fields that needs larger space (e.g. pointer or other
     structs).

   - All data (including variable length fields) associated with a
     struct is allocated within the same contiguous memory of the
     struct itself and you don't need to free() it separately.

   - As a starting point, see xcb_xim_server_connection_new().  */

typedef struct xcb_xim_server_connection_t xcb_xim_server_connection_t;

#define XCB_XIM_PREEDIT_AREA 0x0001
#define XCB_XIM_PREEDIT_CALLBACKS 0x0002
#define XCB_XIM_PREEDIT_POSITION 0x0004
#define XCB_XIM_PREEDIT_NOTHING 0x0008
#define XCB_XIM_PREEDIT_NONE 0x0010

#define XCB_XIM_STATUS_AREA 0x0100
#define XCB_XIM_STATUS_CALLBACKS 0x0200
#define XCB_XIM_STATUS_NOTHING 0x0400
#define XCB_XIM_STATUS_NONE 0x0800

/* Transport.  */

struct xcb_xim_transport_t
{
  /* We only support "X/" transport.  */
  xcb_window_t client_window;
  xcb_window_t server_window;

  uint8_t endian;      /* 'B' for big endian, 'l' for little endian */
};

typedef struct xcb_xim_transport_t xcb_xim_transport_t;

uint16_t
xcb_xim_card16 (xcb_xim_transport_t *transport, uint16_t value);

uint32_t
xcb_xim_card32 (xcb_xim_transport_t *transport, uint32_t value);

/* Basic types used in requests.  */

struct xcb_xim_str_t
{
  uint8_t length;               /* 1: n */
                                /* n: STRING8 */
};

typedef struct xcb_xim_str_t xcb_xim_str_t;

#define XCB_XIM_TYPE_SEPARATOROFNESTEDLIST 0
#define XCB_XIM_TYPE_CARD8 1
#define XCB_XIM_TYPE_CARD16 2
#define XCB_XIM_TYPE_CARD32 3
#define XCB_XIM_TYPE_STRING8 4
#define XCB_XIM_TYPE_WINDOW 5
#define XCB_XIM_TYPE_XIMSTYLES 10
#define XCB_XIM_TYPE_XRECTANGLE 11
#define XCB_XIM_TYPE_XPOINT 12
#define XCB_XIM_TYPE_XFONTSET 13
#define XCB_XIM_TYPE_XIMOPTIONS 14
#define XCB_XIM_TYPE_XIMHOTKEYTRIGGERS 15
#define XCB_XIM_TYPE_XIMHOTKEYSTATE 16
#define XCB_XIM_TYPE_XIMSTRINGCONVERSION 17
#define XCB_XIM_TYPE_NEST 0x7fff

struct xcb_xim_attribute_spec_t
{
  uint16_t attribute_id;        /* 2: attribute_id */
  uint16_t type;                /* 2: type */
  uint16_t length;              /* 2: n */
                                /* n: name */
                                /* p: pad(2+n) */
};

typedef struct xcb_xim_attribute_spec_t xcb_xim_attribute_spec_t;

xcb_xim_attribute_spec_t *
xcb_xim_attribute_spec_new (xcb_xim_transport_t *transport,
                            uint16_t attribute_id,
                            uint16_t type,
                            uint16_t name_length,
                            const char *name);

struct xcb_xim_triggerkey_t
{
  uint32_t keysym;
  uint32_t modifier;
  uint32_t modifier_mask;
};

typedef struct xcb_xim_triggerkey_t xcb_xim_triggerkey_t;

struct xcb_xim_encoding_info_t
{
  uint16_t byte_length;         /* 2: n */
                                /* n: STRING8 */
                                /* p: pad(2+n) */
};

typedef struct xcb_xim_encoding_info_t xcb_xim_encoding_info_t;

struct xcb_xim_extension_t
{
  uint8_t major_opcode;         /* 1: major_opcode */
  uint8_t minor_opcode;         /* 1: minor_opcode */
  uint16_t name_length;         /* 2: n */
                                /* n: STRING8 */
                                /* p: pad(n) */
};

typedef struct xcb_xim_extension_t xcb_xim_extension_t;

xcb_xim_extension_t *
xcb_xim_extension_new (xcb_xim_transport_t *transport,
                       uint8_t major_opcode,
                       uint8_t minor_opcode,
                       uint16_t name_length,
                       const char *name);

struct xcb_xim_attribute_t
{
  uint16_t attribute_id;        /* 2: attribute_id */
  uint16_t value_byte_length;   /* 2: n */
                                /* n: value */
                                /* p: pad(n) */
};

typedef struct xcb_xim_attribute_t xcb_xim_attribute_t;

struct xcb_xim_str_conv_text_t
{
  uint16_t type;                /* 2: type */
  uint16_t string_byte_length;  /* 2: n */
                                /* n: STRING8 */
                                /* p: pad(n) */
                                /* 2: m */
                                /* 2: padding */
                                /* m: LISTofXIMFEEDBACK (unused) */
};

typedef struct xcb_xim_str_conv_text_t xcb_xim_str_conv_text_t;

typedef enum
  {
    XCB_XIM_CARET_DIRECTION_FORWARD_CHAR,
    XCB_XIM_CARET_DIRECTION_BACKWARD_CHAR,
    XCB_XIM_CARET_DIRECTION_FORWARD_WORD,
    XCB_XIM_CARET_DIRECTION_BACKWARD_WORD,
    XCB_XIM_CARET_DIRECTION_CARET_UP,
    XCB_XIM_CARET_DIRECTION_CARET_DOWN,
    XCB_XIM_CARET_DIRECTION_NEXT_LINE,
    XCB_XIM_CARET_DIRECTION_PREVIOUS_LINE,
    XCB_XIM_CARET_DIRECTION_LINE_START,
    XCB_XIM_CARET_DIRECTION_LINE_END,
    XCB_XIM_CARET_DIRECTION_ABSOLUTE_POSITION,
    XCB_XIM_CARET_DIRECTION_DONT_CHANGE
  } xcb_xim_caret_direction_t;

typedef enum
  {
    XCB_XIM_CARET_STYLE_INVISIBLE,
    XCB_XIM_CARET_STYLE_PRIMARY,
    XCB_XIM_CARET_STYLE_SECONDARY
  } xcb_xim_caret_style_t;

typedef enum
  {
    XCB_XIM_FEEDBACK_REVERSE = 0x1,
    XCB_XIM_FEEDBACK_UNDERLINE = 0x2,
    XCB_XIM_FEEDBACK_HIGHLIGHT = 0x4,
    XCB_XIM_FEEDBACK_PRIMARY = 0x8,
    XCB_XIM_FEEDBACK_SECONDARY = 0x10,
    XCB_XIM_FEEDBACK_TERTIARY = 0x20,
    XCB_XIM_FEEDBACK_VISIBLE_TO_FORWARD = 0x40,
    XCB_XIM_FEEDBACK_VISIBLE_TO_BACKWARD = 0x80,
    XCB_XIM_FEEDBACK_VISIBLE_TO_CENTER = 0x100
  } xcb_xim_feedback_t;

typedef enum
  {
    XCB_XIM_HOTKEY_STATE_ON = 0x1,
    XCB_XIM_HOTKEY_STATE_OFF = 0x2
  } xcb_xim_hotkey_state_t;

typedef enum
  {
    XCB_XIM_PREEDIT_STATE_ENABLE = 0x1,
    XCB_XIM_PREEDIT_STATE_DISABLE = 0x2
  } xcb_xim_preedit_state_t;

typedef enum
  {
    XCB_XIM_RESET_STATE_INITIAL = 0x1,
    XCB_XIM_RESET_STATE_PRESERVE = 0x2,
  } xcb_xim_reset_state_t;

xcb_xim_str_conv_text_t *
xcb_xim_str_conv_text_new (xcb_xim_transport_t *transport,
                           uint16_t type,
                           uint16_t string_byte_length,
                           const uint8_t *string,
                           uint16_t feedbacks_length,
                           const xcb_xim_feedback_t *feedbacks);

xcb_xim_attribute_t *
xcb_xim_attribute_card8_new (xcb_xim_transport_t *transport,
                             uint16_t attribute_id,
                             uint8_t value);

xcb_xim_attribute_t *
xcb_xim_attribute_card16_new (xcb_xim_transport_t *transport,
                              uint16_t attribute_id,
                              uint16_t value);

xcb_xim_attribute_t *
xcb_xim_attribute_card32_new (xcb_xim_transport_t *transport,
                              uint16_t attribute_id,
                              uint32_t value);

xcb_xim_attribute_t *
xcb_xim_attribute_string8_new (xcb_xim_transport_t *transport,
                               uint16_t attribute_id,
                               uint8_t value_length,
                               const char *value);

xcb_xim_attribute_t *
xcb_xim_attribute_styles_new (xcb_xim_transport_t *transport,
                              uint16_t attribute_id,
                              uint16_t length,
                              const uint32_t *value);

xcb_xim_attribute_t *
xcb_xim_attribute_rectangle_new (xcb_xim_transport_t *transport,
                                 uint16_t attribute_id,
                                 const xcb_rectangle_t *value);

xcb_xim_attribute_t *
xcb_xim_attribute_point_new (xcb_xim_transport_t *transport,
                             uint16_t attribute_id,
                             const xcb_point_t *value);

xcb_xim_attribute_t *
xcb_xim_attribute_font_set_new (xcb_xim_transport_t *transport,
                                uint16_t attribute_id,
                                uint8_t value_length,
                                const char *value);

xcb_xim_attribute_t *
xcb_xim_attribute_hotkey_triggers_new (xcb_xim_transport_t *transport,
                                       uint16_t attribute_id,
                                       uint32_t value_length,
                                       const xcb_xim_triggerkey_t **triggers,
                                       const xcb_xim_hotkey_state_t *states);

xcb_xim_attribute_t *
xcb_xim_attribute_packed_new (xcb_xim_transport_t *transport,
                              uint16_t attribute_id,
                              uint16_t value_byte_length,
                              const void *value);

xcb_xim_attribute_t *
xcb_xim_attribute_nested_list_new (xcb_xim_transport_t *transport,
                                   uint16_t attribute_id,
                                   uint16_t value_length,
                                   const xcb_xim_attribute_t **value);

/* Iterators.  */

struct xcb_xim_attribute_id_iterator_t
{
  const xcb_xim_transport_t *transport;
  uint16_t *data;
  uint16_t index;
  uint16_t remainder;
};

typedef struct xcb_xim_attribute_id_iterator_t xcb_xim_attribute_id_iterator_t;

bool
xcb_xim_attribute_id_iterator_has_data (xcb_xim_attribute_id_iterator_t *i);

void
xcb_xim_attribute_id_iterator_next (xcb_xim_attribute_id_iterator_t *i);

struct xcb_xim_attribute_iterator_t
{
  const xcb_xim_transport_t *transport;
  xcb_xim_attribute_t *data;
  uint16_t index;
  uint16_t remainder;
};

typedef struct xcb_xim_attribute_iterator_t xcb_xim_attribute_iterator_t;

bool
xcb_xim_attribute_iterator_has_data (xcb_xim_attribute_iterator_t *i);

void
xcb_xim_attribute_iterator_next (xcb_xim_attribute_iterator_t *i);

struct xcb_xim_str_iterator_t
{
  xcb_xim_str_t *data;
  uint16_t index;
  uint16_t remainder;
};

typedef struct xcb_xim_str_iterator_t xcb_xim_str_iterator_t;

bool
xcb_xim_str_iterator_has_data (xcb_xim_str_iterator_t *i);

void
xcb_xim_str_iterator_next (xcb_xim_str_iterator_t *i);

/* Requests.  */

struct xcb_xim_generic_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;
};

typedef struct xcb_xim_generic_request_t xcb_xim_generic_request_t;

typedef enum
  {
    XCB_XIM_ERROR_BAD_ALLOC = 1,
    XCB_XIM_ERROR_BAD_STYLE = 2,
    XCB_XIM_ERROR_BAD_CLIENT_WINDOW = 3,
    XCB_XIM_ERROR_BAD_FOCUS_WINDOW = 4,
    XCB_XIM_ERROR_BAD_AREA = 5,
    XCB_XIM_ERROR_BAD_SPOT_LOCATION = 6,
    XCB_XIM_ERROR_BAD_COLORMAP = 7,
    XCB_XIM_ERROR_BAD_ATOM = 8,
    XCB_XIM_ERROR_BAD_PIXEL = 9,
    XCB_XIM_ERROR_BAD_PIXMAP = 10,
    XCB_XIM_ERROR_BAD_NAME = 11,
    XCB_XIM_ERROR_BAD_CURSOR = 12,
    XCB_XIM_ERROR_BAD_PROTOCOL = 13,
    XCB_XIM_ERROR_BAD_FOREGROUND = 14,
    XCB_XIM_ERROR_BAD_BACKGROUND = 15,
    XCB_XIM_ERROR_LOCALE_NOT_SUPPORTED = 16,
    XCB_XIM_ERROR_BAD_SOMETHING = 999
  } xcb_xim_error_code_t;

typedef enum
  {
    XCB_XIM_ERROR_FLAG_NONE = 0,
    XCB_XIM_ERROR_FLAG_INPUT_METHOD = 1,
    XCB_XIM_ERROR_FLAG_INPUT_CONTEXT = 2
  } xcb_xim_error_flag_t;

struct xcb_xim_error_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;     /* 2: input_method_id */
  uint16_t input_context_id;    /* 2: input_context_id */
  uint16_t flag;                /* 2: flag */
  uint16_t error_code;          /* 2: error_code */
  uint16_t detail_length;       /* 2: n */
  uint16_t detail_type;         /* 2: detail_type */
                                /* n: error detail */
                                /* p: pad(n) */
};

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
               xcb_generic_error_t **error);

#define XCB_XIM_ERROR 20

/* XIM_OPEN */

struct xcb_xim_open_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint8_t locale_length;               /* 1: n */
};

typedef struct xcb_xim_open_request_t xcb_xim_open_request_t;

bool
xcb_xim_open_reply (xcb_xim_server_connection_t *xim,
                    xcb_xim_transport_t *transport,
                    uint16_t input_method_id,
                    uint16_t im_attrs_length,
                    xcb_xim_attribute_spec_t **im_attrs,
                    uint16_t ic_attrs_length,
                    xcb_xim_attribute_spec_t **ic_attrs,
                    xcb_generic_error_t **error);

#define XCB_XIM_OPEN 30

/* XIM_CLOSE */

struct xcb_xim_close_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;
};

typedef struct xcb_xim_close_request_t xcb_xim_close_request_t;

bool
xcb_xim_close_reply (xcb_xim_server_connection_t *xim,
                     xcb_xim_transport_t *transport,
                     uint16_t input_method_id,
                     xcb_generic_error_t **error);

#define XCB_XIM_CLOSE 32

/* XIM_SET_EVENT_MASK */

bool
xcb_xim_set_event_mask (xcb_xim_server_connection_t *xim,
                        xcb_xim_transport_t *transport,
                        uint16_t input_method_id,
                        uint16_t input_context_id,
                        uint32_t forward_event_mask,
                        uint32_t synchronous_event_mask,
                        xcb_generic_error_t **error);

/* XIM_REGISTER_TRIGGERKEYS */

bool
xcb_xim_register_triggerkeys (xcb_xim_server_connection_t *xim,
                              xcb_xim_transport_t *transport,
                              uint16_t input_method_id,
                              uint32_t on_keys_length,
                              const xcb_xim_triggerkey_t **on_keys,
                              uint32_t off_keys_length,
                              const xcb_xim_triggerkey_t **off_keys,
                              xcb_generic_error_t **error);

/* XIM_TRIGGER_NOTIFY */

struct xcb_xim_trigger_notify_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;        /* 2: input_method_id */
  uint16_t input_context_id;       /* 2: input_context_id */
  uint32_t flag;                   /* 4: flag */
  uint32_t keys;                   /* 4: keys */
  uint32_t mask;                   /* 4: event mask */
};

bool
xcb_xim_trigger_notify_reply (xcb_xim_server_connection_t *xim,
                              xcb_xim_transport_t *transport,
                              uint16_t input_method_id,
                              uint16_t input_context_id,
                              xcb_generic_error_t **error);

#define XCB_XIM_TRIGGER_NOTIFY 35

/* XIM_QUERY_EXTENSION */

struct xcb_xim_query_extension_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;     /* 2: input_method_id */
  uint16_t extensions_byte_length; /* 2: n */
                                /* n: LISTofSTR */
                                /* p: pad(n) */
};

typedef struct xcb_xim_query_extension_request_t
  xcb_xim_query_extension_request_t;

xcb_xim_str_iterator_t
xcb_xim_query_extension_request_extension_iterator (
  const xcb_xim_query_extension_request_t *r);

bool
xcb_xim_query_extension_reply (xcb_xim_server_connection_t *xim,
                               xcb_xim_transport_t *transport,
                               uint16_t input_method_id,
                               uint16_t extensions_length,
                               xcb_xim_extension_t **extensions,
                               xcb_generic_error_t **error);

#define XCB_XIM_QUERY_EXTENSION 40

/* XIM_ENCODING_NEGOTIATION */

struct xcb_xim_encoding_negotiation_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;     /* 2: input_method_id */
  uint16_t encodings_byte_length; /* 2: n */
                                /* n: LISTofSTR */
                                /* p: pad(n) */
                                /* 2: m */
                                /* 2: pad(m) */
                                /* m: LISTofENCODINGINFO */
};

typedef struct xcb_xim_encoding_negotiation_request_t
  xcb_xim_encoding_negotiation_request_t;

xcb_xim_str_iterator_t
xcb_xim_encoding_negotiation_request_encoding_iterator (
  const xcb_xim_encoding_negotiation_request_t *r);

bool
xcb_xim_encoding_negotiation_reply (xcb_xim_server_connection_t *xim,
                                    xcb_xim_transport_t *transport,
                                    uint16_t input_method_id,
                                    uint16_t category,
                                    int16_t index,
                                    xcb_generic_error_t **error);

#define XCB_XIM_ENCODING_NEGOTIATION 38

/* XIM_SET_IM_VALUES */

struct xcb_xim_set_im_values_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;     /* 2: input_method_id */
  uint16_t attributes_byte_length; /* 2: n */
                                /* n: LISTofXIMATTRIBUTE */
};

typedef struct xcb_xim_set_im_values_request_t xcb_xim_set_im_values_request_t;

xcb_xim_attribute_iterator_t
xcb_xim_set_im_values_request_attribute_iterator (
  const xcb_xim_set_im_values_request_t *r);

xcb_xim_attribute_iterator_t
xcb_xim_attribute_nested_list_attribute_iterator (xcb_xim_generic_request_t *r,
                                                  xcb_xim_attribute_t *a);

bool
xcb_xim_set_im_values_reply (xcb_xim_server_connection_t *xim,
                             xcb_xim_transport_t *transport,
                             uint16_t input_method_id,
                             xcb_generic_error_t **error);

#define XCB_XIM_SET_IM_VALUES 42

/* XIM_GET_IM_VALUES */

struct xcb_xim_get_im_values_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;     /* 2: input_method_id */
  uint16_t attributes_byte_length; /* 2: n */
                                /* n: LISTofCARD16 */
                                /* p: pad(n) */
};

typedef struct xcb_xim_get_im_values_request_t xcb_xim_get_im_values_request_t;

xcb_xim_attribute_id_iterator_t
xcb_xim_get_im_values_request_attribute_id_iterator (
  const xcb_xim_get_im_values_request_t *r);

bool
xcb_xim_get_im_values_reply (xcb_xim_server_connection_t *xim,
                             xcb_xim_transport_t *transport,
                             uint16_t input_method_id,
                             uint16_t attributes_length,
                             xcb_xim_attribute_t **attributes,
                             xcb_generic_error_t **error);

#define XCB_XIM_GET_IM_VALUES 44

/* XIM_CREATE_IC */

struct xcb_xim_create_ic_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;     /* 2: input_method_id */
  uint16_t attributes_byte_length; /* 2: n */
                                /* n: LISTofXICATTRIBUTE */
};

typedef struct xcb_xim_create_ic_request_t xcb_xim_create_ic_request_t;

xcb_xim_attribute_iterator_t
xcb_xim_create_ic_request_attribute_iterator (
  const xcb_xim_create_ic_request_t *r);

bool
xcb_xim_create_ic_reply (xcb_xim_server_connection_t *xim,
                         xcb_xim_transport_t *transport,
                         uint16_t input_method_id,
                         uint16_t input_context_id,
                         xcb_generic_error_t **error);

#define XCB_XIM_CREATE_IC 50

/* XIM_DESTROY_IC */

struct xcb_xim_destroy_ic_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;        /* 2: input_method_id */
  uint16_t input_context_id;       /* 2: input_context_id */
};

typedef struct xcb_xim_destroy_ic_request_t xcb_xim_destroy_ic_request_t;

bool
xcb_xim_destroy_ic_reply (xcb_xim_server_connection_t *xim,
                          xcb_xim_transport_t *transport,
                          uint16_t input_method_id,
                          uint16_t input_context_id,
                          xcb_generic_error_t **error);

#define XCB_XIM_DESTROY_IC 53

/* XIM_GET_IC_VALUES */

struct xcb_xim_set_ic_values_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;        /* 2: input_method_id */
  uint16_t input_context_id;       /* 2: input_context_id */
  uint16_t attributes_byte_length; /* 2: n */
  uint16_t pad;                    /* 2: padding */
                                /* n: LISTofXICATTRIBUTE */
};

typedef struct xcb_xim_set_ic_values_request_t xcb_xim_set_ic_values_request_t;

xcb_xim_attribute_iterator_t
xcb_xim_set_ic_values_request_attribute_iterator (
  const xcb_xim_set_ic_values_request_t *r);

bool
xcb_xim_set_ic_values_reply (xcb_xim_server_connection_t *xim,
                             xcb_xim_transport_t *transport,
                             uint16_t input_method_id,
                             uint16_t input_context_id,
                             xcb_generic_error_t **error);

#define XCB_XIM_SET_IC_VALUES 54

/* XIM_GET_IC_VALUES */

struct xcb_xim_get_ic_values_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;        /* 2: input_method_id */
  uint16_t input_context_id;       /* 2: input_context_id */
  uint16_t attributes_byte_length; /* 2: n */
                                /* Oops, why no padding here...  */
                                /* n: LISTofCARD16 */
                                /* p: pad(2+n) */
};

typedef struct xcb_xim_get_ic_values_request_t xcb_xim_get_ic_values_request_t;

xcb_xim_attribute_id_iterator_t
xcb_xim_get_ic_values_request_attribute_id_iterator (
  const xcb_xim_get_ic_values_request_t *r);

bool
xcb_xim_get_ic_values_reply (xcb_xim_server_connection_t *xim,
                             xcb_xim_transport_t *transport,
                             uint16_t input_method_id,
                             uint16_t input_context_id,
                             uint16_t attributes_length,
                             xcb_xim_attribute_t **attributes,
                             xcb_generic_error_t **error);

#define XCB_XIM_GET_IC_VALUES 56

/* XIM_SET_IC_FOCUS */

struct xcb_xim_set_ic_focus_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;        /* 2: input_method_id */
  uint16_t input_context_id;       /* 2: input_context_id */
};

typedef struct xcb_xim_set_ic_focus_request_t xcb_xim_set_ic_focus_request_t;

#define XCB_XIM_SET_IC_FOCUS 58

/* XIM_UNSET_IC_FOCUS */

struct xcb_xim_unset_ic_focus_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;        /* 2: input_method_id */
  uint16_t input_context_id;       /* 2: input_context_id */
};

typedef struct xcb_xim_unset_ic_focus_request_t
  xcb_xim_unset_ic_focus_request_t;

#define XCB_XIM_UNSET_IC_FOCUS 59

/* XIM_FORWARD_EVENT */

struct xcb_xim_forward_event_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;     /* 2: input_method_id */
  uint16_t input_context_id;    /* 2: input_context_id */
  uint16_t flag;                /* 2: flag */
  uint16_t serial;              /* 2: serial */
                                /* x: XEVENT */
};

typedef struct xcb_xim_forward_event_request_t xcb_xim_forward_event_request_t;

typedef enum
  {
    XCB_XIM_FORWARD_EVENT_FLAG_SYNCHRONOUS = 0x1,
    XCB_XIM_FORWARD_EVENT_FLAG_FILTER = 0x2,
    XCB_XIM_FORWARD_EVENT_FLAG_LOOKUP = 0x4,
  } xcb_xim_forward_event_flag_t;

bool
xcb_xim_forward_event (xcb_xim_server_connection_t *xim,
                       xcb_xim_transport_t *transport,
                       uint16_t input_method_id,
                       uint16_t input_context_id,
                       uint16_t flag,
                       uint16_t serial,
                       xcb_generic_event_t *event,
                       xcb_generic_error_t **error);

xcb_generic_event_t *
xcb_xim_forward_event_get_event (xcb_xim_forward_event_request_t *request);

uint16_t
xcb_xim_forward_event_get_serial (xcb_xim_forward_event_request_t *request);

#define XCB_XIM_FORWARD_EVENT 60

/* XIM_SYNC */

struct xcb_xim_sync_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;        /* 2: input_method_id */
  uint16_t input_context_id;       /* 2: input_context_id */
};

typedef struct xcb_xim_sync_request_t xcb_xim_sync_request_t;

bool
xcb_xim_sync_reply (xcb_xim_server_connection_t *xim,
                    xcb_xim_transport_t *transport,
                    uint16_t input_method_id,
                    uint16_t input_context_id,
                    xcb_generic_error_t **error);

#define XCB_XIM_SYNC 61

/* XIM_COMMIT */

typedef enum
  {
    XCB_XIM_COMMIT_FLAG_SYNCHRONOUS = 0x1,
    XCB_XIM_COMMIT_FLAG_KEYSYM = 0x2,
    XCB_XIM_COMMIT_FLAG_STRING = 0x4,
  } xcb_xim_commit_flag_t;

bool
xcb_xim_commit (xcb_xim_server_connection_t *xim,
                xcb_xim_transport_t *transport,
                uint16_t input_method_id,
                uint16_t input_context_id,
                uint16_t flag,
                uint32_t keysym,
                uint16_t string_length,
                const uint8_t *string,
                xcb_generic_error_t **error);

/* XIM_RESET_IC */

struct xcb_xim_reset_ic_request_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;     /* 2: input_method_id */
  uint16_t input_context_id;    /* 2: input_context_id */
};

typedef struct xcb_xim_reset_ic_request_t xcb_xim_reset_ic_request_t;

bool
xcb_xim_reset_ic_reply (xcb_xim_server_connection_t *xim,
                        xcb_xim_transport_t *transport,
                        uint16_t input_method_id,
                        uint16_t input_context_id,
                        uint16_t preedit_length,
                        const uint8_t *preedit,
                        xcb_generic_error_t **error);

#define XCB_XIM_RESET_IC 64

/* XIM_GEOMETRY */

bool
xcb_xim_geometry (xcb_xim_server_connection_t *xim,
                  xcb_xim_transport_t *transport,
                  uint16_t input_method_id,
                  uint16_t input_context_id,
                  xcb_generic_error_t **error);

/* XIM_STR_CONVERSION */

bool
xcb_xim_str_conversion (xcb_xim_server_connection_t *xim,
                        xcb_xim_transport_t *transport,
                        uint16_t input_method_id,
                        uint16_t input_context_id,
                        uint16_t position,
                        xcb_xim_caret_direction_t direction,
                        uint16_t factor,
                        uint16_t operation,
                        int16_t byte_length,
                        xcb_generic_error_t **error);

#define XCB_XIM_STR_CONVERSION_REPLY 72

/* XIM_PREEDIT_START */

bool
xcb_xim_preedit_start (xcb_xim_server_connection_t *xim,
                       xcb_xim_transport_t *transport,
                       uint16_t input_method_id,
                       uint16_t input_context_id,
                       xcb_generic_error_t **error);

struct xcb_xim_preedit_start_reply_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;     /* 2: input_method_id */
  uint16_t input_context_id;    /* 2: input_context_id */
  int32_t retval;               /* 4: retval */
};

typedef struct xcb_xim_preedit_start_reply_t xcb_xim_preedit_start_reply_t;

#define XCB_XIM_PREEDIT_START_REPLY 74

/* XIM_PREEDIT_DRAW */

bool
xcb_xim_preedit_draw (xcb_xim_server_connection_t *xim,
                      xcb_xim_transport_t *transport,
                      uint16_t input_method_id,
                      uint16_t input_context_id,
                      int32_t caret,
                      int32_t first,
                      int32_t length,
                      uint32_t status,
                      uint16_t preedit_length,
                      const uint8_t *preedit,
                      uint16_t feedbacks_length,
                      const xcb_xim_feedback_t *feedbacks,
                      xcb_generic_error_t **error);

/* XIM_PREEDIT_CARET */

bool
xcb_xim_preedit_caret (xcb_xim_server_connection_t *xim,
                       xcb_xim_transport_t *transport,
                       uint16_t input_method_id,
                       uint16_t input_context_id,
                       int32_t position,
                       xcb_xim_caret_direction_t direction,
                       xcb_xim_caret_style_t style,
                       xcb_generic_error_t **error);

struct xcb_xim_preedit_caret_reply_t
{
  uint8_t major_opcode;
  uint8_t minor_opcode;
  uint16_t length;

  uint16_t input_method_id;     /* 2: input_method_id */
  uint16_t input_context_id;    /* 2: input_context_id */
  uint32_t position;            /* 4: position */
};

typedef struct xcb_xim_preedit_caret_reply_t
  xcb_xim_preedit_caret_reply_t;

#define XCB_XIM_PREEDIT_CARET_REPLY 77

/* XIM_PREEDIT_DONE */

bool
xcb_xim_preedit_done (xcb_xim_server_connection_t *xim,
                      xcb_xim_transport_t *transport,
                      uint16_t input_method_id,
                      uint16_t input_context_id,
                      xcb_generic_error_t **error);

/* XIM_PREEDITSTATE */

bool
xcb_xim_preeditstate (xcb_xim_server_connection_t *xim,
                      xcb_xim_transport_t *transport,
                      uint16_t input_method_id,
                      uint16_t input_context_id,
                      uint32_t state,
                      xcb_generic_error_t **error);

/* XIM_STATUS_START */

bool
xcb_xim_status_start (xcb_xim_server_connection_t *xim,
                      xcb_xim_transport_t *transport,
                      uint16_t input_method_id,
                      uint16_t input_context_id,
                      xcb_generic_error_t **error);

/* XIM_STATUS_DRAW */

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
                     xcb_generic_error_t **error);

/* XIM_STATUS_DONE */

bool
xcb_xim_status_done (xcb_xim_server_connection_t *xim,
                     xcb_xim_transport_t *transport,
                     uint16_t input_method_id,
                     uint16_t input_context_id,
                     xcb_generic_error_t **error);

/* Server connection.  */

struct xcb_xim_request_container_t
{
  xcb_xim_transport_t *requestor;
  xcb_xim_generic_request_t request;
};

typedef struct xcb_xim_request_container_t xcb_xim_request_container_t;

typedef enum
  {
    XCB_XIM_DISPATCH_CONTINUE,
    XCB_XIM_DISPATCH_REMOVE,
    XCB_XIM_DISPATCH_ERROR
  } xcb_xim_dispatch_result_t;

xcb_xim_server_connection_t *
xcb_xim_server_connection_new (xcb_connection_t *connection,
                               const char *name,
                               const char *locale,
                               xcb_generic_error_t **error);

void
xcb_xim_server_connection_free (xcb_xim_server_connection_t *xim);


xcb_xim_dispatch_result_t
xcb_xim_server_connection_dispatch (xcb_xim_server_connection_t *xim,
                                    xcb_generic_event_t *event,
                                    xcb_generic_error_t **error);

xcb_xim_request_container_t *
xcb_xim_server_connection_poll_request (xcb_xim_server_connection_t *xim);

#endif
