/*
 This file is part of telegram-purple
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 
 Copyright Matthias Jentsch 2014-2015
 */

#include <assert.h>
#include <tgl.h>
#include <glib.h>
#include <errno.h>

#include "telegram-purple.h"
#include "tgp-structs.h"
#include "tgp-msg.h"
#include "tgp-ft.h"
#include "tgp-2prpl.h"
#include "tgp-chat.h"
#include "tgp-utils.h"
#include "tgp-chat.h"
#include "msglog.h"

static void tgp_msg_err_out (struct tgl_state *TLS, const char *error, tgl_peer_id_t to);

static char *format_service_msg (struct tgl_state *TLS, struct tgl_message *M) {
  assert (M && M->service);
  
  char *txt_user = NULL;
  char *txt_action = NULL;
  char *txt = NULL;
  
  tgl_peer_t *peer = tgl_peer_get (TLS, M->from_id);
  if (! peer) {
    return NULL;
  }
  txt_user = p2tgl_strdup_alias (peer);
  
  switch (M->action.type) {
    case tgl_message_action_chat_create:
      txt_action = g_strdup_printf ("created chat %s", M->action.title);
      break;
    case tgl_message_action_chat_edit_title:
      txt_action = g_strdup_printf ("changed title to %s", M->action.new_title);
      break;
    case tgl_message_action_chat_edit_photo:
      txt_action = g_strdup ("changed photo");
      break;
    case tgl_message_action_chat_delete_photo:
      txt_action = g_strdup ("deleted photo");
      break;
    case tgl_message_action_chat_add_user:
    {
      tgl_peer_t *peer = tgl_peer_get (TLS, TGL_MK_USER (M->action.user));
      if (peer) {
        char *alias = p2tgl_strdup_alias (peer);
        txt_action = g_strdup_printf ("added user %s", alias);
        g_free (alias);
      }
      break;
    }
    case tgl_message_action_chat_delete_user:
    {
      tgl_peer_t *peer = tgl_peer_get (TLS, TGL_MK_USER (M->action.user));
      if (peer) {
        char *alias = p2tgl_strdup_alias (peer);
        txt_action = g_strdup_printf ("deleted user %s", alias);
        g_free (alias);
      }
      break;
    }
    case tgl_message_action_set_message_ttl:
      txt_action = g_strdup_printf ("set ttl to %d seconds", M->action.ttl);
      break;
    case tgl_message_action_read_messages:
      txt_action = g_strdup_printf ("%d messages marked read", M->action.read_cnt);
      break;
    case tgl_message_action_delete_messages:
      txt_action = g_strdup_printf ("%d messages deleted", M->action.delete_cnt);
      break;
    case tgl_message_action_screenshot_messages:
      txt_action = g_strdup_printf ("%d messages screenshoted", M->action.screenshot_cnt);
      break;
    case tgl_message_action_notify_layer:
      txt_action = g_strdup_printf ("updated layer to %d", M->action.layer);
      break;
    case tgl_message_action_request_key:
      txt_action = g_strdup_printf ("Request rekey #%016llx", M->action.exchange_id);
      break;
    case tgl_message_action_accept_key:
      txt_action = g_strdup_printf ("Accept rekey #%016llx", M->action.exchange_id);
      break;
    case tgl_message_action_commit_key:
      txt_action = g_strdup_printf ("Commit rekey #%016llx", M->action.exchange_id);
      break;
    case tgl_message_action_abort_key:
      txt_action = g_strdup_printf ("Abort rekey #%016llx", M->action.exchange_id);
      break;
    default:
      txt_action = NULL;
      break;
  }
  if (txt_action) {
    debug ("SERVICE MESSAGE: %s", txt_action);
    txt = g_strdup_printf ("%s %s.", txt_user, txt_action);
    g_free (txt_action);
  }
  g_free (txt_user);
  return txt;
}

static char *format_document_desc (char *type, char *caption, gint64 size) {
  char *s = tgp_g_format_size (size);
  char *msg = g_strdup_printf ("[%s] %s %s", type, caption, s);
  g_free (s);
  return msg;
}

static char *format_message (struct tgl_message *M) {
  switch (M->media.type) {
    case tgl_message_media_photo_encr:
      return format_document_desc ("ENCRYPTED PHOTO", "(not yet supported)", M->media.encr_photo.size);
      break;
    case tgl_message_media_contact:
      return g_strdup_printf ("<b>%s %s</b><br>%s", M->media.first_name, M->media.last_name, M->media.phone);
      break;
    case tgl_message_media_geo:
      return g_strdup_printf ("<a href=\"http://openstreetmap.org/?lat=%f&lon=%f&zoom=20\">"
                             "http://openstreetmap.org/?lat=%f&lon=%f&zoom=20</a>",
                             M->media.geo.latitude, M->media.geo.longitude,
                             M->media.geo.latitude, M->media.geo.longitude);
      return g_strdup_printf ("<b>%s %s</b><br>%s", M->media.first_name, M->media.last_name, M->media.phone);
      break;
    default:
      if (*M->message != 0) {
        return purple_markup_escape_text (M->message, strlen (M->message));
      }
      return NULL;
      break;
  }
}

static void tgp_msg_send_done (struct tgl_state *TLS, void *callback_extra, int success, struct tgl_message *M) {
  if (! success) {
    const char *err = "Sending message failed. Maybe you don't have the permission "
    "to send to this peer, or the peer does no longer exist.";
    warning (err);
    tgp_msg_err_out (TLS, err, M->to_id);
  }
}

static int tgp_msg_send_split (struct tgl_state *TLS, const char *message, tgl_peer_id_t to) {
  int max = TGP_DEFAULT_MAX_MSG_SPLIT_COUNT;
  if (max < 1) {
    max = 1;
  }
  
  int size = (int)g_utf8_strlen(message, -1);
  if (size > TGP_MAX_MSG_SIZE * max) {
    return -E2BIG;
  }
  
  int start = 0;
  while (size > start) {
    int e = start + (int)TGP_MAX_MSG_SIZE;
    gchar *chunk = g_utf8_substring (message, start, e);
    tgl_do_send_message (TLS, to, chunk, (int)strlen (chunk), tgp_msg_send_done, NULL);
    g_free (chunk);
    start = e;
  }
  return 1;
}

static void tgp_msg_err_out (struct tgl_state *TLS, const char *error, tgl_peer_id_t to) {
  int flags = PURPLE_MESSAGE_ERROR | PURPLE_MESSAGE_SYSTEM;
  time_t now;
  time (&now);
  switch (tgl_get_peer_type (to)) {
      case TGL_PEER_CHAT:
        p2tgl_got_chat_in (TLS, to, to, error, flags, now);
        break;
      case TGL_PEER_USER:
      case TGL_PEER_ENCR_CHAT:
        p2tgl_got_im (TLS, to, error, flags, now);
        break;
  }
}

int tgp_msg_send (struct tgl_state *TLS, const char *message, tgl_peer_id_t to) {
  
  // search for outgoing embedded image tags and send them
  gchar *img = NULL;
  gchar *stripped = NULL;
  if ((img = g_strrstr (message, "<IMG")) || (img = g_strrstr (message, "<img"))) {
    debug ("img found: %s", img);
    
    gchar *id;
    if ((id = g_strrstr (img, "ID=\"")) || (id = g_strrstr (img, "id=\""))) {
      id += 4;
      debug ("id found: %s", id);
      int imgid = atoi (id);
      if (imgid > 0) {
        PurpleStoredImage *psi = purple_imgstore_find_by_id (imgid);
        gchar *tmp = g_build_filename(g_get_tmp_dir(), purple_imgstore_get_filename (psi), NULL) ;
  
        GError *err = NULL;
        gconstpointer data = purple_imgstore_get_data (psi);
        g_file_set_contents (tmp, data, purple_imgstore_get_size (psi), &err);
        if (! err) {
          tgl_do_send_document (TLS, -2, to, tmp, tgp_msg_send_done, NULL);
        } else {
          failure ("Cannot store image, temp directory not available: %s\n", err->message);
          g_error_free (err);
        }
      }
    }
    
    // send remaining text as additional plaintext message
    stripped = purple_markup_strip_html (message);
    int ret = tgp_msg_send_split (TLS, stripped, to);
    g_free (stripped);
    return ret;
  }
  
#ifndef __ADIUM_
  /*
    Adium won't escape any HTML markup and just pass any user-input through,
    while Pidgin will replace special chars with the escape chars and also add 
    additional markup for RTL languages and such.

    First, we remove any HTML markup added by Pidgin, since Telegram won't handle it properly.
    User-entered HTML is still escaped and therefore won't be harmed.
   */
  stripped = purple_markup_strip_html (message);
  
  /* 
    now unescape the markup, so that html special chars will still show
    up properly in Telegram
   */
  gchar *unescaped = purple_unescape_text (stripped);
  int ret = tgp_msg_send_split (TLS, stripped, to);
  
  g_free (unescaped);
  g_free (stripped);
  return ret;
#endif
  
  return tgp_msg_send_split (TLS, message, to);
}

static void tgp_msg_display (struct tgl_state *TLS, struct tgp_msg_loading *C) {
  connection_data *conn = TLS->ev_base;
  struct tgl_message *M = C->msg;
  char *text = NULL;
  int flags = 0;
  
  // Filter message updates and deletes, are not created and
  // all messages in general that were already displayed, or shouldn't be displayed
  if ((M->flags & (FLAG_MESSAGE_EMPTY | FLAG_DELETED)) ||
      !(M->flags & FLAG_CREATED) ||
      !M->message ||
      our_msg (TLS, M) ||
      !tgl_get_peer_type (M->to_id)) {
    return;
  }
  
  
  if (M->service) {
    text = format_service_msg (TLS, M);
    flags |= PURPLE_MESSAGE_SYSTEM;
  }
  else if (M->media.type == tgl_message_media_document) {
    char *who = p2tgl_strdup_id (M->from_id);
    if (! out_msg(TLS, M)) {
      tgprpl_recv_file (conn->gc, who, &M->media.document);
    }
    g_free (who);
    return;
  }
  else if (M->media.type == tgl_message_media_document_encr) {
    char *who = p2tgl_strdup_id (M->from_id);
    if (! out_msg(TLS, M)) {
      tgprpl_recv_encr_file (conn->gc, who, &M->media.encr_document);
    }
    g_free (who);
  }
  else if (M->media.type == tgl_message_media_photo) {
    char *filename = C->data;
    int imgStoreId = p2tgl_imgstore_add_with_id (filename);
    if (imgStoreId <= 0) {
      failure ("Cannot display picture message, adding to imgstore failed.");
      return;
    }
    used_images_add (conn, imgStoreId);
    text = format_img_full (imgStoreId);
    flags |= PURPLE_MESSAGE_IMAGES;
  }
  else {
    text = format_message (M);
    flags |= PURPLE_MESSAGE_RECV;
  }
  
  
  if (! text || ! *text) {
    warning ("No text to display");
    return;
  }
  switch (tgl_get_peer_type (M->to_id)) {
    case TGL_PEER_CHAT: {
      if (chat_show (conn->gc, tgl_get_peer_id (M->to_id))) {
        p2tgl_got_chat_in (TLS, M->to_id, M->from_id, text, flags, M->date);
      }
      pending_reads_add (conn->pending_reads, M->to_id);
      break;
    }
    case TGL_PEER_ENCR_CHAT: {
      p2tgl_got_im (TLS, M->to_id, text, flags, M->date);
      pending_reads_add (conn->pending_reads, M->to_id);
      break;
    }
    case TGL_PEER_USER: {
      if (out_msg (TLS, M)) {
        flags |= PURPLE_MESSAGE_SEND;
        flags &= ~PURPLE_MESSAGE_RECV;
        p2tgl_got_im_combo (TLS, M->to_id, text, flags, M->date);
      } else {
        p2tgl_got_im (TLS, M->from_id, text, flags, M->date);
        pending_reads_add (conn->pending_reads, M->from_id);
      }
      break;
    }
  }
  
  
  if (p2tgl_status_is_present (purple_account_get_active_status (conn->pa))) {
    pending_reads_send_all (conn->pending_reads, conn->TLS);
  }

  
  g_free (text);
}

static time_t tgp_msg_oldest_relevant_ts (struct tgl_state *TLS) {
  connection_data *conn = TLS->ev_base;
  int days = purple_account_get_int (conn->pa, TGP_KEY_HISTORY_RETRIEVAL_THRESHOLD,
                                     TGP_DEFAULT_HISTORY_RETRIEVAL_THRESHOLD);
  return days > 0 ? tgp_time_n_days_ago (days) : 0;
}

static void tgp_msg_process_ready (struct tgl_state *TLS)
{
  connection_data *conn = TLS->ev_base;
  struct tgp_msg_loading *C;
  
  while ((C = g_queue_peek_head (conn->new_messages))) {
    if (! C->done) {
      break;
    }
    g_queue_pop_head (conn->new_messages);
    tgp_msg_display (TLS, C);
    tgp_msg_loading_free (C);
  }
}

static void tgp_msg_on_loaded_photo (struct tgl_state *TLS, void *extra, int success, char *filename) {
  struct tgp_msg_loading *C = extra;
  C->data = filename;
  C->done = TRUE;
  tgp_msg_process_ready (TLS);
}

void tgp_msg_recv (struct tgl_state *TLS, struct tgl_message *M)
{
  connection_data *conn = TLS->ev_base;
  struct tgp_msg_loading *C = tgp_msg_loading_init (TRUE, M);
  
  if (M->date != 0 && M->date < tgp_msg_oldest_relevant_ts (TLS)) {
    debug ("Message from %d on %d too old, ignored.", tgl_get_peer_id (M->from_id), M->date);
    return;
  }
  
  if (M->media.type == tgl_message_media_photo) {
    C->done = FALSE;
    tgl_do_load_photo (TLS, &M->media.photo, tgp_msg_on_loaded_photo, C);
  }
  
  if (M->media.type == tgl_message_media_geo) {
    // TODO: load geo thumbnail
  }
  
  g_queue_push_tail (conn->new_messages, C);
  tgp_msg_process_ready (TLS);
}


