/*
 *  Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author: Pierre-Luc Beaudoin <pierre-luc.beaudoin@savoirfairelinux.com>
 *  Author: Emmanuel Milou <emmanuel.milou@savoirfairelinux.com>
 *  Author: Guillaume Carmel-Archambault <guillaume.carmel-archambault@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty f
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Additional permission under GNU GPL version 3 section 7:
 *
 *  If you modify this program, or any covered work, by linking or
 *  combining it with the OpenSSL project's OpenSSL library (or a
 *  modified version of that library), containing parts covered by the
 *  terms of the OpenSSL or SSLeay licenses, Savoir-Faire Linux Inc.
 *  grants you additional permission to convey the resulting work.
 *  Corresponding Source for a non-source form of such a combination
 *  shall include the source code for the parts of OpenSSL used as well
 *  as that of the covered work.
 */
#include <config.h>

#include <calltab.h>
#include <callmanager-glue.h>
#include <configurationmanager-glue.h>
#include <instance-glue.h>
#include <preferencesdialog.h>
#include <accountlistconfigdialog.h>
#include <mainwindow.h>
#include <marshaller.h>
#include <sliders.h>
#include <statusicon.h>
#include <assistant.h>

#include <dbus.h>
#include <actions.h>
#include <string.h>

#include <widget/imwidget.h>

#include <eel-gconf-extensions.h>
#include "videoconf.h"

#define DEFAULT_DBUS_TIMEOUT 30000

static DBusGProxy *callManagerProxy, *configurationManagerProxy, *instanceProxy;

static void
new_call_created_cb (DBusGProxy *proxy UNUSED, const gchar *accountID,
		     const gchar *callID, const gchar *to, void *foo UNUSED)
{
    DEBUG("DBUS: New Call (%s) created to (%s)", callID, to);

    callable_obj_t *c = create_new_call(CALL, CALL_STATE_RINGING, callID, accountID, to, to);

    calllist_add_call(current_calls, c);
    calltree_add_call(current_calls, c, NULL);

    update_actions();
    calltree_display(current_calls);
}

static void
incoming_call_cb (DBusGProxy *proxy UNUSED, const gchar* accountID,
                  const gchar* callID, const gchar* from, void * foo  UNUSED)
{
    // We receive the from field under a formatted way. We want to extract the number and the name of the caller
    gchar *peer_name = call_get_peer_name (from);
    gchar *peer_number = call_get_peer_number (from);

    DEBUG ("DBus: Incoming call (%s) from %s (%s : %s)", callID, from, peer_name, peer_number);

    callable_obj_t *c = create_new_call (CALL, CALL_STATE_INCOMING, callID, accountID, peer_name, peer_number);

    g_free(peer_number);
    g_free(peer_name);

#if GTK_CHECK_VERSION(2,10,0)
    status_tray_icon_blink (TRUE);
    popup_main_window();
#endif

    notify_incoming_call (c);
    sflphone_incoming_call (c);
}

static void
zrtp_negotiation_failed_cb (DBusGProxy *proxy UNUSED, const gchar* callID,
                            const gchar* reason, const gchar* severity, void * foo  UNUSED)
{
    main_window_zrtp_negotiation_failed (callID, reason, severity);
    callable_obj_t *c = calllist_get_call (current_calls, callID);
    if (c)
        notify_zrtp_negotiation_failed (c);
}

static void
volume_changed_cb (DBusGProxy *proxy UNUSED, const gchar* device, const gdouble value,
                   void * foo  UNUSED)
{
    set_slider (device, value);
}

static void
voice_mail_cb (DBusGProxy *proxy UNUSED, const gchar* accountID, const guint nb,
               void * foo  UNUSED)
{
    sflphone_notify_voice_mail (accountID, nb);
}

static void
incoming_message_cb (DBusGProxy *proxy UNUSED, const gchar* callID UNUSED, const gchar *from, const gchar* msg, void * foo  UNUSED)
{
    DEBUG ("DBUS: Message \"%s\" from %s!", msg, from);

    // do not display message if instant messaging is disabled
    if (eel_gconf_key_exists (INSTANT_MESSAGING_ENABLED) && !eel_gconf_get_integer (INSTANT_MESSAGING_ENABLED))
        return;

    IMWidget **widget;
    gchar *id;
    callable_obj_t *call = calllist_get_call (current_calls, callID);
    if (call) {
        widget = (IMWidget **) &call->_im_widget;
        id = call->_callID;
    } else {
        conference_obj_t *conf = conferencelist_get (current_calls, callID);
        if (!conf) {
            ERROR ("Message received, but no recipient found");
            return;
        }
        widget = (IMWidget **) &conf->_im_widget;
        id = conf->_confID;
    }

    if (im_widget_display (widget, msg, id, from))
        im_widget_add_message (*widget, from, msg, 0);
}

static void
call_state_cb (DBusGProxy *proxy UNUSED, const gchar* callID, const gchar* state,
               void * foo  UNUSED)
{
    DEBUG ("DBUS: Call %s state %s",callID, state);
    callable_obj_t *c = calllist_get_call (current_calls, callID);
    if (c) {
        if (g_strcmp0 (state, "HUNGUP") == 0) {
            if (c->_state == CALL_STATE_CURRENT) {
                time(&c->_time_stop);
                calltree_update_call (history, c, NULL);
            }

            stop_notification();
            calltree_update_call (history, c, NULL);
            status_bar_display_account();
            sflphone_hung_up (c);
        } else if (g_strcmp0 (state, "UNHOLD_CURRENT") == 0) {
            sflphone_current (c);
        } else if (g_strcmp0 (state, "UNHOLD_RECORD") == 0) {
            sflphone_record (c);
        } else if (g_strcmp0 (state, "HOLD") == 0) {
            sflphone_hold (c);
        } else if (g_strcmp0 (state, "RINGING") == 0) {
            sflphone_ringing (c);
        } else if (g_strcmp0 (state, "CURRENT") == 0) {
            sflphone_current (c);
        } else if (g_strcmp0 (state, "RECORD") == 0) {
            sflphone_record (c);
        } else if (g_strcmp0 (state, "FAILURE") == 0) {
            sflphone_fail (c);
        } else if (g_strcmp0 (state, "BUSY") == 0) {
            sflphone_busy (c);
        }
    } else {
        ERROR("DBUS: Error: Call is NULL in %s", __func__);

        // The callID is unknow, threat it like a new call
        // If it were an incoming call, we won't be here
        // It means that a new call has been initiated with an other client (cli for instance)
        if ((g_strcmp0 (state, "RINGING")) == 0 ||
            (g_strcmp0 (state, "CURRENT")) == 0 ||
            (g_strcmp0 (state, "RECORD"))) {

            DEBUG ("DBUS: New ringing call! accountID: %s", callID);

            // We fetch the details associated to the specified call
            GHashTable *call_details = dbus_get_call_details (callID);
            callable_obj_t *new_call = create_new_call_from_details (callID, call_details);

            new_call->_history_state = (g_strcasecmp (g_hash_table_lookup (call_details, "CALL_TYPE"), "0") == 0)
                      ? INCOMING : OUTGOING;

            calllist_add_call (current_calls, new_call);
            calltree_add_call (current_calls, new_call, NULL);
            update_actions();
            calltree_display (current_calls);
        }
    }
}

static void toggle_im(conference_obj_t *conf, gboolean activate)
{
    for (GSList *part = conf->participant_list; part; part = g_slist_next (part)) {
        callable_obj_t *call = calllist_get_call (current_calls, part->data);
        if (call && call->_im_widget)
            im_widget_update_state (IM_WIDGET (call->_im_widget), activate);
    }
}

static void
conference_changed_cb (DBusGProxy *proxy UNUSED, const gchar* confID,
                       const gchar* state, void * foo  UNUSED)
{
    DEBUG ("DBUS: Conference state changed: %s\n", state);

    // sflphone_display_transfer_status("Transfer successfull");
    conference_obj_t* changed_conf = conferencelist_get (current_calls, confID);

    if(changed_conf == NULL) {
	ERROR("DBUS: Conference is NULL in conference state changed");
	return;
    }

    // remove old conference from calltree
    calltree_remove_conference (current_calls, changed_conf, NULL);

    // update conference state
    if (g_strcmp0 (state, "ACTIVE_ATACHED") == 0) {
        changed_conf->_state = CONFERENCE_STATE_ACTIVE_ATACHED;
    } else if (g_strcmp0 (state, "ACTIVE_DETACHED") == 0) {
        changed_conf->_state = CONFERENCE_STATE_ACTIVE_DETACHED;
    } else if (g_strcmp0 (state, "ACTIVE_ATTACHED_REC") == 0) {
        changed_conf->_state = CONFERENCE_STATE_ACTIVE_ATTACHED_RECORD;
    } else if (g_strcmp0(state, "ACTIVE_DETACHED_REC") == 0) {
        changed_conf->_state = CONFERENCE_STATE_ACTIVE_DETACHED_RECORD;
    } else if (g_strcmp0 (state, "HOLD") == 0) {
        changed_conf->_state = CONFERENCE_STATE_HOLD;
    } else if (g_strcmp0(state, "HOLD_REC") == 0) {
        changed_conf->_state = CONFERENCE_STATE_HOLD_RECORD;
    } else {
        DEBUG ("Error: conference state not recognized");
    }

    // reactivate instant messaging window for these calls
    toggle_im(changed_conf, TRUE);

    gchar **list = dbus_get_participant_list(changed_conf->_confID);
    conference_participant_list_update(list, changed_conf);
    g_strfreev(list);

    // deactivate instant messaging window for new participants
    toggle_im(changed_conf, FALSE);

    calltree_add_conference (current_calls, changed_conf);
}

static void
conference_created_cb (DBusGProxy *proxy UNUSED, const gchar* confID, void * foo  UNUSED)
{
    DEBUG ("DBUS: Conference %s added", confID);

    conference_obj_t *new_conf = create_new_conference (CONFERENCE_STATE_ACTIVE_ATACHED, confID);

    gchar **participants = dbus_get_participant_list (new_conf->_confID);

    // Update conference list
    conference_participant_list_update (participants, new_conf);

    // Add conference ID in in each calls
    for (gchar **part = participants; *part; part++) {
        callable_obj_t *call = calllist_get_call (current_calls, *part);

        // set when this call have been added to the conference
        time(&call->_time_added);

        // if a text widget is already created, disable it, use conference widget instead
        if (call->_im_widget)
            im_widget_update_state (IM_WIDGET (call->_im_widget), FALSE);

        // if one of these participant is currently recording, the whole conference will be recorded
        if(call->_state == CALL_STATE_RECORD)
            new_conf->_state = CONFERENCE_STATE_ACTIVE_ATTACHED_RECORD;

        call->_confID = g_strdup (confID);
        call->_historyConfID = g_strdup (confID);
    }

    g_strfreev(participants);

    time(&new_conf->_time_start);

    conferencelist_add (current_calls, new_conf);
    conferencelist_add (history, new_conf);
    calltree_add_conference (current_calls, new_conf);
    calltree_add_conference (history, new_conf);
}

static void
conference_removed_cb (DBusGProxy *proxy UNUSED, const gchar* confID, void * foo  UNUSED)
{
    DEBUG ("DBUS: Conference removed %s", confID);

    conference_obj_t * c = conferencelist_get (current_calls, confID);
    calltree_remove_conference (current_calls, c, NULL);

    // deactivate instant messaging window for this conference
    if (c->_im_widget)
        im_widget_update_state (IM_WIDGET (c->_im_widget), FALSE);

    // remove all participant for this conference
    for (GSList *p = c->participant_list; p; p = conference_next_participant (p)) {

        callable_obj_t *call = calllist_get_call (current_calls, p->data);
        if (call) {
            g_free (call->_confID);
            call->_confID = NULL;

            // if an instant messaging was previously disabled, enabled it
            if (call->_im_widget)
                im_widget_update_state (IM_WIDGET (call->_im_widget), TRUE);
        }
    }

    conferencelist_remove (current_calls, c->_confID);
}

static void
record_playback_filepath_cb (DBusGProxy *proxy UNUSED, const gchar *id, const gchar *filepath)
{
    DEBUG("DBUS: Filepath for %s: %s", id, filepath);

    callable_obj_t *call = calllist_get_call(current_calls, id);
    conference_obj_t *conf = conferencelist_get(current_calls, id);

    if (call && conf) {
        ERROR("DBUS: Two objects for this callid");
        return;
    }

    if (!call && !conf) {
        ERROR("DBUS: Could not get object");
        return;
    }

    if(call && call->_recordfile == NULL)
        call->_recordfile = g_strdup(filepath);
    else if(conf && conf->_recordfile == NULL)
        conf->_recordfile = g_strdup(filepath);
}

static void
record_playback_stopped_cb (DBusGProxy *proxy UNUSED, const gchar *filepath)
{
    DEBUG("DBUS: Playback stopped for %s", filepath);

    const gint calllist_size = calllist_get_size(history);
    for (gint i = 0; i < calllist_size; i++) {
        QueueElement *element = calllist_get_nth(history, i);
        if (element == NULL) {
            ERROR("DBUS: ERROR: Could not find %dth call", i);
            break;
        } else if (element->type == HIST_CALL &&
                g_strcmp0(element->elem.call->_recordfile, filepath) == 0)
            element->elem.call->_record_is_playing = FALSE;
    }

    const gint conflist_size = conferencelist_get_size(history);
    for (gint i = 0; i < conflist_size; i++) {
        conference_obj_t *conf = conferencelist_get_nth(history, i);
        if (conf == NULL) {
            ERROR("DBUS: ERROR: Could not find %dth conf", i);
            break;
        } else if (g_strcmp0(conf->_recordfile, filepath) == 0)
            conf->_record_is_playing = FALSE;
    }

    update_actions();
}

static void
accounts_changed_cb (DBusGProxy *proxy UNUSED, void * foo  UNUSED)
{
    DEBUG ("DBUS: Accounts changed");
    sflphone_fill_account_list();
    sflphone_fill_ip2ip_profile();
    account_list_config_dialog_fill();
    status_bar_display_account();
    statusicon_set_tooltip ();
}

static void
transfer_succeded_cb (DBusGProxy *proxy UNUSED, void * foo  UNUSED)
{
    DEBUG ("DBUS: Transfer succeded");
    sflphone_display_transfer_status ("Transfer successfull");
}

static void
transfer_failed_cb (DBusGProxy *proxy UNUSED, void * foo  UNUSED)
{
    DEBUG ("DBUS: Transfer failed");
    sflphone_display_transfer_status ("Transfer failed");
}

static void
secure_sdes_on_cb (DBusGProxy *proxy UNUSED, const gchar *callID, void *foo UNUSED)
{
    DEBUG ("DBUS: SRTP using SDES is on");
    callable_obj_t *c = calllist_get_call (current_calls, callID);
    if (c) {
        sflphone_srtp_sdes_on (c);
        notify_secure_on (c);
    }

}

static void
secure_sdes_off_cb (DBusGProxy *proxy UNUSED, const gchar *callID, void *foo UNUSED)
{
    DEBUG ("DBUS: SRTP using SDES is off");
    callable_obj_t *c = calllist_get_call (current_calls, callID);
    if (c) {
        sflphone_srtp_sdes_off (c);
        notify_secure_off (c);
    }
}

static void
secure_zrtp_on_cb (DBusGProxy *proxy UNUSED, const gchar* callID, const gchar* cipher,
                   void * foo  UNUSED)
{
    DEBUG ("DBUS: SRTP using ZRTP is ON secure_on_cb");
    callable_obj_t *c = calllist_get_call (current_calls, callID);
    if (c) {
        c->_srtp_cipher = g_strdup (cipher);
        sflphone_srtp_zrtp_on (c);
        notify_secure_on (c);
    }
}

static void
secure_zrtp_off_cb (DBusGProxy *proxy UNUSED, const gchar* callID, void * foo  UNUSED)
{
    DEBUG ("DBUS: SRTP using ZRTP is OFF");
    callable_obj_t *c = calllist_get_call (current_calls, callID);
    if (c) {
        sflphone_srtp_zrtp_off (c);
        notify_secure_off (c);
    }
}

static void
show_zrtp_sas_cb (DBusGProxy *proxy UNUSED, const gchar* callID, const gchar* sas,
                  const gboolean verified, void * foo  UNUSED)
{
    DEBUG ("DBUS: Showing SAS");
    callable_obj_t * c = calllist_get_call (current_calls, callID);
    if (c)
        sflphone_srtp_zrtp_show_sas (c, sas, verified);
}

static void
confirm_go_clear_cb (DBusGProxy *proxy UNUSED, const gchar* callID, void * foo  UNUSED)
{
    DEBUG ("DBUS: Confirm Go Clear request");
    callable_obj_t * c = calllist_get_call (current_calls, callID);
    if (c)
        main_window_confirm_go_clear (c);
}

static void
zrtp_not_supported_cb (DBusGProxy *proxy UNUSED, const gchar* callID, void * foo  UNUSED)
{
    DEBUG ("ZRTP not supported on the other end");
    callable_obj_t * c = calllist_get_call (current_calls, callID);
    if (c) {
        main_window_zrtp_not_supported (c);
        notify_zrtp_not_supported (c);
    }
}

static void
sip_call_state_cb (DBusGProxy *proxy UNUSED, const gchar* callID,
                   const gchar* description, const guint code, void * foo  UNUSED)
{
    DEBUG("DBUS: Sip call state changed %s", callID);
    callable_obj_t *c = calllist_get_call (current_calls, callID);
    if (c)
        sflphone_call_state_changed (c, description, code);
}

static void
error_alert (DBusGProxy *proxy UNUSED, int err, void * foo  UNUSED)
{
    const gchar *msg;
    switch (err) {
        case ALSA_PLAYBACK_DEVICE:
            msg = _("ALSA notification\n\nError while opening playback device");
            break;
        case ALSA_CAPTURE_DEVICE:
            msg = _("ALSA notification\n\nError while opening capture device");
            break;
        case PULSEAUDIO_NOT_RUNNING:
            msg = _("Pulseaudio notification\n\nPulseaudio is not running");
            break;
        default:
            return;
    }

    GtkWidget * dialog = gtk_message_dialog_new(
                             GTK_WINDOW (get_main_window()),
                             GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
                             GTK_BUTTONS_CLOSE, "%s", msg);

    gtk_window_set_title (GTK_WINDOW (dialog), _ ("SFLphone Error"));

    g_signal_connect(dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
    gtk_widget_show(dialog);
}

gboolean
dbus_connect (GError **error)
{
    g_type_init();

    DBusGConnection *connection = dbus_g_bus_get (DBUS_BUS_SESSION, error);
    if (connection == NULL)
        return FALSE;

    /* Create a proxy object for the "bus driver" (name "org.freedesktop.DBus") */

    instanceProxy = dbus_g_proxy_new_for_name (connection,
                    "org.sflphone.SFLphone", "/org/sflphone/SFLphone/Instance",
                    "org.sflphone.SFLphone.Instance");

    if (instanceProxy == NULL) {
        ERROR ("Failed to get proxy to Instance");
        return FALSE;
    }

    DEBUG ("DBus connected to Instance");

    callManagerProxy = dbus_g_proxy_new_for_name (connection,
                       "org.sflphone.SFLphone", "/org/sflphone/SFLphone/CallManager",
                       "org.sflphone.SFLphone.CallManager");
    g_assert (callManagerProxy != NULL);

    DEBUG ("DBus connected to CallManager");
    /* STRING STRING STRING Marshaller */
    /* Incoming call */
    dbus_g_object_register_marshaller (
        g_cclosure_user_marshal_VOID__STRING_STRING_STRING, G_TYPE_NONE,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (callManagerProxy, "newCallCreated", G_TYPE_STRING,
			     G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "newCallCreated",
				 G_CALLBACK (new_call_created_cb), NULL, NULL);
    dbus_g_proxy_add_signal (callManagerProxy, "incomingCall", G_TYPE_STRING,
                             G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "incomingCall",
                                 G_CALLBACK (incoming_call_cb), NULL, NULL);

    dbus_g_proxy_add_signal (callManagerProxy, "zrtpNegotiationFailed",
                             G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "zrtpNegotiationFailed",
                                 G_CALLBACK (zrtp_negotiation_failed_cb), NULL, NULL);

    /* Register a marshaller for STRING,STRING */
    dbus_g_object_register_marshaller (
        g_cclosure_user_marshal_VOID__STRING_STRING, G_TYPE_NONE, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (callManagerProxy, "callStateChanged", G_TYPE_STRING,
                             G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "callStateChanged",
                                 G_CALLBACK (call_state_cb), NULL, NULL);

    dbus_g_object_register_marshaller (g_cclosure_user_marshal_VOID__STRING_INT,
                                       G_TYPE_NONE, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (callManagerProxy, "voiceMailNotify", G_TYPE_STRING,
                             G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "voiceMailNotify",
                                 G_CALLBACK (voice_mail_cb), NULL, NULL);

    dbus_g_proxy_add_signal (callManagerProxy, "incomingMessage", G_TYPE_STRING,
                             G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "incomingMessage",
                                 G_CALLBACK (incoming_message_cb), NULL, NULL);

    dbus_g_object_register_marshaller (
        g_cclosure_user_marshal_VOID__STRING_DOUBLE, G_TYPE_NONE, G_TYPE_STRING,
        G_TYPE_DOUBLE, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (callManagerProxy, "volumeChanged", G_TYPE_STRING,
                             G_TYPE_DOUBLE, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "volumeChanged",
                                 G_CALLBACK (volume_changed_cb), NULL, NULL);

    dbus_g_proxy_add_signal (callManagerProxy, "transferSucceded", G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "transferSucceded",
                                 G_CALLBACK (transfer_succeded_cb), NULL, NULL);

    dbus_g_proxy_add_signal (callManagerProxy, "transferFailed", G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "transferFailed",
                                 G_CALLBACK (transfer_failed_cb), NULL, NULL);

    /* Conference related callback */

    dbus_g_object_register_marshaller (g_cclosure_user_marshal_VOID__STRING,
                                       G_TYPE_NONE, G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (callManagerProxy, "conferenceChanged", G_TYPE_STRING,
                             G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "conferenceChanged",
                                 G_CALLBACK (conference_changed_cb), NULL, NULL);

    dbus_g_proxy_add_signal (callManagerProxy, "conferenceCreated", G_TYPE_STRING,
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "conferenceCreated",
                                 G_CALLBACK (conference_created_cb), NULL, NULL);

    dbus_g_proxy_add_signal (callManagerProxy, "conferenceRemoved", G_TYPE_STRING,
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "conferenceRemoved",
                                 G_CALLBACK (conference_removed_cb), NULL, NULL);

    /* Playback related signals */
    dbus_g_proxy_add_signal (callManagerProxy, "recordPlaybackFilepath", G_TYPE_STRING,
				G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "recordPlaybackFilepath",
				G_CALLBACK (record_playback_filepath_cb), NULL, NULL);
    dbus_g_proxy_add_signal (callManagerProxy, "recordPlaybackStopped", G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(callManagerProxy, "recordPlaybackStopped",
    				G_CALLBACK (record_playback_stopped_cb), NULL, NULL);

    /* Security related callbacks */

    dbus_g_proxy_add_signal (callManagerProxy, "secureSdesOn", G_TYPE_STRING,
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "secureSdesOn",
                                 G_CALLBACK (secure_sdes_on_cb), NULL, NULL);

    dbus_g_proxy_add_signal (callManagerProxy, "secureSdesOff", G_TYPE_STRING,
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "secureSdesOff",
                                 G_CALLBACK (secure_sdes_off_cb), NULL, NULL);

    /* Register a marshaller for STRING,STRING,BOOL */
    dbus_g_object_register_marshaller (
        g_cclosure_user_marshal_VOID__STRING_STRING_BOOL, G_TYPE_NONE,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (callManagerProxy, "showSAS", G_TYPE_STRING,
                             G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "showSAS",
                                 G_CALLBACK (show_zrtp_sas_cb), NULL, NULL);

    dbus_g_proxy_add_signal (callManagerProxy, "secureZrtpOn", G_TYPE_STRING,
                             G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "secureZrtpOn",
                                 G_CALLBACK (secure_zrtp_on_cb), NULL, NULL);

    /* Register a marshaller for STRING*/
    dbus_g_object_register_marshaller (g_cclosure_user_marshal_VOID__STRING,
                                       G_TYPE_NONE, G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (callManagerProxy, "secureZrtpOff", G_TYPE_STRING,
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "secureZrtpOff",
                                 G_CALLBACK (secure_zrtp_off_cb), NULL, NULL);
    dbus_g_proxy_add_signal (callManagerProxy, "zrtpNotSuppOther", G_TYPE_STRING,
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "zrtpNotSuppOther",
                                 G_CALLBACK (zrtp_not_supported_cb), NULL, NULL);
    dbus_g_proxy_add_signal (callManagerProxy, "confirmGoClear", G_TYPE_STRING,
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "confirmGoClear",
                                 G_CALLBACK (confirm_go_clear_cb), NULL, NULL);

    /* VOID STRING STRING INT */
    dbus_g_object_register_marshaller (
        g_cclosure_user_marshal_VOID__STRING_STRING_INT, G_TYPE_NONE,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INVALID);

    dbus_g_proxy_add_signal (callManagerProxy, "sipCallStateChanged",
                             G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (callManagerProxy, "sipCallStateChanged",
                                 G_CALLBACK (sip_call_state_cb), NULL, NULL);

    configurationManagerProxy = dbus_g_proxy_new_for_name (connection,
                                "org.sflphone.SFLphone", "/org/sflphone/SFLphone/ConfigurationManager",
                                "org.sflphone.SFLphone.ConfigurationManager");
    g_assert (configurationManagerProxy != NULL);

    DEBUG ("DBus connected to ConfigurationManager");
    dbus_g_proxy_add_signal (configurationManagerProxy, "accountsChanged",
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (configurationManagerProxy, "accountsChanged",
                                 G_CALLBACK (accounts_changed_cb), NULL, NULL);

    dbus_g_object_register_marshaller (g_cclosure_user_marshal_VOID__INT,
                                       G_TYPE_NONE, G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_add_signal (configurationManagerProxy, "errorAlert", G_TYPE_INT,
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (configurationManagerProxy, "errorAlert",
                                 G_CALLBACK (error_alert), NULL, NULL);

    /* Video related signals */
    dbus_g_proxy_add_signal (configurationManagerProxy, "videoDeviceEvent",
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(configurationManagerProxy, "videoDeviceEvent",
                                G_CALLBACK (video_device_event_cb), NULL, NULL);

    /* Marshaller for INT INT INT INT INT */
    dbus_g_object_register_marshaller (
        g_cclosure_user_marshal_VOID__INT_INT_INT_INT_INT, G_TYPE_NONE,
        G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INVALID);

    dbus_g_proxy_add_signal (callManagerProxy, "receivingVideoEvent",
                             G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
                             G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(callManagerProxy, "receivingVideoEvent",
                                G_CALLBACK (receiving_video_event_cb), NULL, NULL);

    /* Marshaller for INT INT */
    dbus_g_object_register_marshaller (
        g_cclosure_user_marshal_VOID__INT_INT, G_TYPE_NONE,
        G_TYPE_INT, G_TYPE_INT, G_TYPE_INVALID);

    dbus_g_proxy_add_signal (callManagerProxy, "stoppedReceivingVideoEvent",
                             G_TYPE_INT, G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(callManagerProxy, "stoppedReceivingVideoEvent",
                                G_CALLBACK (stopped_receiving_video_event_cb),
                                NULL, NULL);

    /* Defines a default timeout for the proxies */
#if HAVE_DBUS_G_PROXY_SET_DEFAULT_TIMEOUT
    dbus_g_proxy_set_default_timeout (callManagerProxy, DEFAULT_DBUS_TIMEOUT);
    dbus_g_proxy_set_default_timeout (instanceProxy, DEFAULT_DBUS_TIMEOUT);
    dbus_g_proxy_set_default_timeout (configurationManagerProxy, DEFAULT_DBUS_TIMEOUT);
#endif

    return TRUE;
}

void
dbus_clean()
{
    g_object_unref (callManagerProxy);
    g_object_unref (configurationManagerProxy);
    g_object_unref (instanceProxy);
}

void
dbus_hold (const callable_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_hold (callManagerProxy, c->_callID, &error);

    if (error) {
        ERROR ("Failed to call hold() on CallManager: %s", error->message);
        g_error_free (error);
    }
}

void
dbus_unhold (const callable_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_unhold (callManagerProxy, c->_callID, &error);

    if (error) {
        ERROR ("Failed to call unhold() on CallManager: %s", error->message);
        g_error_free (error);
    }
}

void
dbus_hold_conference (const conference_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_hold_conference (callManagerProxy, c->_confID, &error);

    if (error) {
        ERROR ("Failed to call hold() on CallManager: %s", error->message);
        g_error_free (error);
    }
}

void
dbus_unhold_conference (const conference_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_unhold_conference (callManagerProxy, c->_confID, &error);

    if (error) {
        ERROR ("Failed to call unhold() on CallManager: %s", error->message);
        g_error_free (error);
    }
}

gboolean
dbus_start_recorded_file_playback(const gchar *filepath)
{
    GError *error = NULL;
    gboolean result;

    org_sflphone_SFLphone_CallManager_start_recorded_file_playback(callManagerProxy,
	filepath, &result, &error);

    if (error) {
        ERROR("Failed to call recorded file playback: %s", error->message);
        g_error_free(error);
    }

    return result;
}

void
dbus_stop_recorded_file_playback(const gchar *filepath)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_stop_recorded_file_playback(callManagerProxy,
	filepath, &error);

    if (error) {
        ERROR("Failed to call stop recorded file playback: %s", error->message);
        g_error_free(error);
    }
}

void
dbus_hang_up (const callable_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_hang_up (callManagerProxy, c->_callID, &error);

    if (error) {
        ERROR ("Failed to call hang_up() on CallManager: %s", error->message);
        g_error_free (error);
    }
}

void
dbus_hang_up_conference (const conference_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_hang_up_conference (callManagerProxy, c->_confID, &error);

    if (error) {
        ERROR ("Failed to call hang_up() on CallManager: %s", error->message);
        g_error_free (error);
    }
}

void
dbus_transfert (const callable_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_transfert (callManagerProxy, c->_callID, c->_trsft_to, &error);

    if (error) {
        ERROR ("Failed to call transfert() on CallManager: %s", error->message);
        g_error_free (error);
    }
}

void
dbus_attended_transfer (const callable_obj_t *transfer, const callable_obj_t *target)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_attended_transfer (callManagerProxy, transfer->_callID, target->_callID, &error);

    if (error) {
        ERROR("Failed to call transfer() on CallManager: %s", error->message);
        g_error_free(error);
    }
}

void
dbus_accept (const callable_obj_t * c)
{
#if GTK_CHECK_VERSION(2,10,0)
    status_tray_icon_blink (FALSE);
#endif

    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_accept (callManagerProxy, c->_callID, &error);

    if (error) {
        ERROR ("Failed to call accept(%s) on CallManager: %s", c->_callID, error->message);
        g_error_free (error);
    }
}

void
dbus_refuse (const callable_obj_t * c)
{
#if GTK_CHECK_VERSION(2,10,0)
    status_tray_icon_blink (FALSE);
#endif
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_refuse (callManagerProxy, c->_callID, &error);

    if (error) {
        ERROR ("Failed to call refuse() on CallManager: %s", error->message);
        g_error_free (error);
    }
}

void
dbus_place_call (const callable_obj_t * c)
{
    GError *error = NULL;

    org_sflphone_SFLphone_CallManager_place_call (callManagerProxy, c->_accountID,
            c->_callID, c->_peer_number, &error);

    if (error) {
        ERROR ("Failed to call placeCall() on CallManager: %s", error->message);
        g_error_free (error);
    }
}

gchar**
dbus_account_list()
{
    GError *error = NULL;
    char ** array = NULL;

    if (!org_sflphone_SFLphone_ConfigurationManager_get_account_list (
                configurationManagerProxy, &array, &error)) {
        if (error->domain == DBUS_GERROR &&
                error->code == DBUS_GERROR_REMOTE_EXCEPTION)
            ERROR ("Caught remote method (get_account_list) exception  %s: %s", dbus_g_error_get_name (error), error->message);
        else
            ERROR ("Error while calling get_account_list: %s", error->message);

        g_error_free (error);
    } else
        DEBUG ("DBus called get_account_list() on ConfigurationManager");

    return array;
}

GHashTable*
dbus_get_account_details (gchar * accountID)
{
    GError *error = NULL;
    GHashTable * details = NULL;

    DEBUG ("Dbus: Get account detail for %s", accountID);

    if (!org_sflphone_SFLphone_ConfigurationManager_get_account_details (
                configurationManagerProxy, accountID, &details, &error)) {
        if (error->domain == DBUS_GERROR && error->code
                == DBUS_GERROR_REMOTE_EXCEPTION) {
            ERROR ("Caught remote method (get_account_details) exception  %s: %s", dbus_g_error_get_name (error), error->message);
        } else {
            ERROR ("Error while calling get_account_details: %s", error->message);
        }

        g_error_free (error);
    }
    return details;
}

void
dbus_set_credentials (account_t *a)
{
    DEBUG ("DBUS: Sending credentials to server");

    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_credentials (
        configurationManagerProxy, a->accountID, a->credential_information, &error);

    if (error) {
        ERROR ("Failed to call set_credential() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_get_credentials (account_t *a)
{
    DEBUG("DBUS: Get credential for account %s", a->accountID);

    GError *error = NULL;
    if (org_sflphone_SFLphone_ConfigurationManager_get_credentials (
                configurationManagerProxy, a->accountID, &a->credential_information, &error))
        return;

    if (error->domain == DBUS_GERROR &&
            error->code == DBUS_GERROR_REMOTE_EXCEPTION)
        ERROR ("Caught remote method (get_account_details) exception  %s: %s", dbus_g_error_get_name (error), error->message);
    else
        ERROR ("Error while calling get_account_details: %s", error->message);

    g_error_free (error);
}

GHashTable*
dbus_get_ip2_ip_details (void)
{
    GError *error = NULL;
    GHashTable * details = NULL;

    if (!org_sflphone_SFLphone_ConfigurationManager_get_ip2_ip_details (
                configurationManagerProxy, &details, &error)) {
        if (error->domain == DBUS_GERROR &&
                error->code == DBUS_GERROR_REMOTE_EXCEPTION)
            ERROR ("Caught remote method (get_ip2_ip_details) exception  %s: %s", dbus_g_error_get_name (error), error->message);
        else
            ERROR ("Error while calling get_ip2_ip_details: %s", error->message);

        g_error_free (error);
    }
    return details;
}

void
dbus_send_register (gchar* accountID, const guint enable)
{
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_send_register (
        configurationManagerProxy, accountID, enable, &error);

    if (error) {
        ERROR ("Failed to call send_register() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_remove_account (gchar * accountID)
{
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_remove_account (
        configurationManagerProxy, accountID, &error);

    if (error) {
        ERROR ("Failed to call remove_account() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_set_account_details (account_t *a)
{
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_account_details (
        configurationManagerProxy, a->accountID, a->properties, &error);

    if (error) {
        ERROR ("Failed to call set_account_details() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_add_account (account_t *a)
{
    GError *error = NULL;
    g_free(a->accountID);
    org_sflphone_SFLphone_ConfigurationManager_add_account (
        configurationManagerProxy, a->properties, &a->accountID, &error);

    if (error) {
        ERROR ("Failed to call add_account() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_set_volume (const gchar * device, gdouble value)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_set_volume (callManagerProxy, device, value,
            &error);

    if (error) {
        ERROR ("Failed to call set_volume() on callManagerProxy: %s",
               error->message);
        g_error_free (error);
    }
}

gdouble
dbus_get_volume (const gchar * device)
{
    gdouble value;
    GError *error = NULL;

    org_sflphone_SFLphone_CallManager_get_volume (callManagerProxy, device,
            &value, &error);

    if (error) {
        ERROR ("Failed to call get_volume() on callManagerProxy: %s",
               error->message);
        g_error_free (error);
    }

    return value;
}

void
dbus_play_dtmf (const gchar * key)
{
    GError *error = NULL;

    org_sflphone_SFLphone_CallManager_play_dt_mf (callManagerProxy, key, &error);

    if (error) {
        ERROR ("Failed to call playDTMF() on callManagerProxy: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_start_tone (const int start, const guint type)
{
    GError *error = NULL;

    org_sflphone_SFLphone_CallManager_start_tone (callManagerProxy, start, type,
            &error);

    if (error) {
        ERROR ("Failed to call startTone() on callManagerProxy: %s",
               error->message);
        g_error_free (error);
    }
}

gboolean
dbus_register (int pid, gchar *name, GError **error)
{
    return org_sflphone_SFLphone_Instance_register (instanceProxy, pid, name, error);
}

void
dbus_unregister (int pid)
{
    GError *error = NULL;

    org_sflphone_SFLphone_Instance_unregister (instanceProxy, pid, &error);

    if (error) {
        ERROR ("Failed to call unregister() on instanceProxy: %s",
               error->message);
        g_error_free (error);
    }
}

gchar**
dbus_audio_codec_list()
{
    GError *error = NULL;
    gchar** array = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_audio_codec_list (
        configurationManagerProxy, &array, &error);

    if (error) {
        ERROR ("Failed to call get_audio_codec_list() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }

    return array;
}

gchar**
dbus_video_codec_list()
{

    GError *error = NULL;
    gchar** array = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_video_codec_list (
        configurationManagerProxy, &array, &error);

    if (error) {
        ERROR ("Failed to call get_video_codec_list() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }

    return array;
}

gchar**
dbus_get_active_video_codec_list (gchar *accountID)
{

    gchar ** array = NULL;
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_active_video_codec_list (
        configurationManagerProxy, accountID, &array, &error);

    if (error) {
        ERROR ("Failed to call get_active_audio_codec_list() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }

    return array;
}

void
dbus_set_active_video_codec_list (const gchar** list, const gchar *accountID)
{

    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_active_video_codec_list (
        configurationManagerProxy, list, accountID, &error);

    if (error) {
        ERROR ("Failed to call set_active_audio_codec_list() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }
}


gchar**
dbus_audio_codec_details (int payload)
{
    GError *error = NULL;
    gchar ** array = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_audio_codec_details (
        configurationManagerProxy, payload, &array, &error);

    if (error) {
        ERROR ("Failed to call get_audio_codec_details() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }

    return array;
}

gchar**
dbus_video_codec_details (gchar *codec)
{

    GError *error = NULL;
    gchar ** array = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_video_codec_details (
        configurationManagerProxy, codec, &array, &error);

    if (error) {
        ERROR ("Failed to call get_video_codec_details() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }

    return array;
}

gchar*
dbus_get_current_video_codec_name (const callable_obj_t * c)
{

    gchar* codecName = NULL;
    GError* error = NULL;

    org_sflphone_SFLphone_CallManager_get_current_video_codec_name (callManagerProxy,
            c->_callID, &codecName, &error);

    if (error) {
        g_error_free (error);
        g_free (codecName);
        codecName = g_strdup("");
    }

    DEBUG ("%s: codecName : %s", __PRETTY_FUNCTION__, codecName);

    return codecName;
}

gchar*
dbus_get_current_audio_codec_name (const callable_obj_t * c)
{
    gchar* codecName = NULL;
    GError* error = NULL;

    org_sflphone_SFLphone_CallManager_get_current_audio_codec_name (callManagerProxy,
            c->_callID, &codecName, &error);

    if (error) {
        g_error_free (error);
        g_free (codecName);
        codecName = g_strdup("");
    }

    DEBUG ("%s: codecName : %s", __PRETTY_FUNCTION__, codecName);

    return codecName;
}

gchar**
dbus_get_active_audio_codec_list (gchar *accountID)
{

    gchar ** array = NULL;
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_active_audio_codec_list (
        configurationManagerProxy, accountID, &array, &error);

    if (error) {
        ERROR ("Failed to call get_active_audio_codec_list() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }

    return array;
}

void
dbus_set_active_audio_codec_list (const gchar** list, const gchar *accountID)
{

    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_active_audio_codec_list (
        configurationManagerProxy, list, accountID, &error);

    if (error) {
        ERROR ("Failed to call set_active_audio_codec_list() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }
}

/**
 * Get a list of output supported audio plugins
 */
gchar**
dbus_get_audio_plugin_list()
{
    gchar** array = NULL;
    GError* error = NULL;

    if (!org_sflphone_SFLphone_ConfigurationManager_get_audio_plugin_list (
                configurationManagerProxy, &array, &error)) {
        if (error->domain == DBUS_GERROR &&
                error->code == DBUS_GERROR_REMOTE_EXCEPTION)
            ERROR ("Caught remote method (get_output_plugin_list) exception  %s: %s", dbus_g_error_get_name (error), error->message);
        else
            ERROR ("Error while calling get_out_plugin_list: %s", error->message);

        g_error_free (error);
    }
    return array;
}

void
dbus_set_audio_plugin (gchar* audioPlugin)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_audio_plugin (
        configurationManagerProxy, audioPlugin, &error);

    if (error) {
        ERROR ("Failed to call set_audio_plugin() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }
}

/**
 * Get all output devices index supported by current audio manager
 */
gchar**
dbus_get_audio_output_device_list()
{
    gchar** array = NULL;
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_audio_output_device_list (
        configurationManagerProxy, &array, &error);

    if (error) {
        ERROR ("Failed to call get_audio_output_device_list() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }

    return array;
}

/**
 * Set audio output device from its index
 */
void
dbus_set_audio_output_device (const int index)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_audio_output_device (
        configurationManagerProxy, index, &error);

    if (error) {
        ERROR ("Failed to call set_audio_output_device() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }
}

/**
 * Set audio input device from its index
 */
void
dbus_set_audio_input_device (const int index)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_audio_input_device (
        configurationManagerProxy, index, &error);

    if (error) {
        ERROR ("Failed to call set_audio_input_device() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }
}

/**
 * Set adio ringtone device from its index
 */
void
dbus_set_audio_ringtone_device (const int index)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_audio_ringtone_device (
        configurationManagerProxy, index, &error);

    if (error) {
        ERROR ("Failed to call set_audio_ringtone_device() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }
}

/**
 * Get all input devices index supported by current audio manager
 */
gchar**
dbus_get_audio_input_device_list()
{
    gchar** array = NULL;
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_audio_input_device_list (
        configurationManagerProxy, &array, &error);

    if (error) {
        ERROR ("Failed to call get_audio_input_device_list() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }

    return array;
}

/**
 * Get output device index and input device index
 */
gchar**
dbus_get_current_audio_devices_index()
{
    gchar** array = NULL;
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_current_audio_devices_index (
        configurationManagerProxy, &array, &error);

    if (error) {
        ERROR ("Failed to call get_current_audio_devices_index() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }

    return array;
}

/**
 * Get index
 */
int
dbus_get_audio_device_index (const gchar *name)
{
    int index = 0;
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_audio_device_index (
        configurationManagerProxy, name, &index, &error);

    if (error) {
        ERROR ("Failed to call get_audio_device_index() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }

    return index;
}

/**
 * Get audio plugin
 */
gchar*
dbus_get_current_audio_output_plugin()
{
    gchar* plugin;
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_current_audio_output_plugin (
        configurationManagerProxy, &plugin, &error);

    if (error) {
        ERROR ("Failed to call get_current_audio_output_plugin() on ConfigurationManager: %s", error->message);
        g_error_free (error);
        plugin = g_strdup("");
    }

    return plugin;
}


/**
 * Get noise reduction state
 */
gchar*
dbus_get_noise_suppress_state()
{
    gchar* state;
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_noise_suppress_state (configurationManagerProxy, &state, &error);

    if (error) {
        ERROR ("DBus: Failed to call get_noise_suppress_state() on ConfigurationManager: %s", error->message);
        g_error_free (error);
        state = g_strdup("");
    }

    return state;
}

/**
 * Set noise reduction state
 */
void
dbus_set_noise_suppress_state (gchar* state)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_noise_suppress_state (
        configurationManagerProxy, state, &error);

    if (error) {
        ERROR ("Failed to call set_noise_suppress_state() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }
}

gchar *
dbus_get_echo_cancel_state(void)
{
    GError *error = NULL;
    gchar *state;
    org_sflphone_SFLphone_ConfigurationManager_get_echo_cancel_state(configurationManagerProxy, &state, &error);

    if(error) {
        ERROR("DBus: Failed to call get_echo_cancel_state() on ConfigurationManager: %s", error->message);
        g_error_free(error);
        state = g_strdup("");
    }

    return state;
}

void
dbus_set_echo_cancel_state(gchar *state)
{
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_echo_cancel_state(configurationManagerProxy, state, &error);

    if(error) {
        ERROR("DBus: Failed to call set_echo_cancel_state() on ConfigurationManager: %s", error->message);
        g_error_free(error);
    }
}

int
dbus_get_echo_cancel_tail_length(void)
{
    GError *error = NULL;
    int length = 0;

    org_sflphone_SFLphone_ConfigurationManager_get_echo_cancel_tail_length(configurationManagerProxy, &length, &error);

    if(error) {
        ERROR("DBus: Failed to call get_echo_cancel_tail_length() on ConfigurationManager: %s", error->message);
        g_error_free(error);
    }

    return length;
}

void
dbus_set_echo_cancel_tail_length(int length)
{
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_echo_cancel_tail_length(configurationManagerProxy, length, &error);

    if(error) {
        ERROR("DBus: Failed to call get_echo_cancel_state() on ConfigurationManager: %s", error->message);
        g_error_free(error);
    }
}

int
dbus_get_echo_cancel_delay(void)
{
    GError *error = NULL;
    int delay = 0;

    org_sflphone_SFLphone_ConfigurationManager_get_echo_cancel_delay(configurationManagerProxy, &delay, &error);

    if (error) {
        ERROR("DBus: Failed to call get_echo_cancel_tail_length() on ConfigurationManager: %s", error->message);
        g_error_free(error);
    }

    return delay;
}

void
dbus_set_echo_cancel_delay(int delay)
{
    GError *error = NULL;

    org_sflphone_SFLphone_ConfigurationManager_set_echo_cancel_delay(configurationManagerProxy, delay, &error);

    if (error) {
        ERROR("DBus: Failed to call get_echo_cancel_delay() on ConfigurationManager: %s", error->message);
        g_error_free(error);
    }
}


int
dbus_is_iax2_enabled()
{
    int res = 0;
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_is_iax2_enabled (
        configurationManagerProxy, &res, &error);

    if (error)
        g_error_free (error);

    return res;
}

void
dbus_join_participant (const gchar* sel_callID, const gchar* drag_callID)
{

    DEBUG ("DBUS: Join participant %s and %s\n", sel_callID, drag_callID);

    GError* error = NULL;

    org_sflphone_SFLphone_CallManager_join_participant (callManagerProxy,
            sel_callID, drag_callID, &error);

    if (error)
        g_error_free (error);

}

void
dbus_create_conf_from_participant_list(const gchar **list) {

    GError *error = NULL;

    DEBUG("DBUS: Create conference from participant list");

    org_sflphone_SFLphone_CallManager_create_conf_from_participant_list(callManagerProxy,
	list, &error);

    if (error) {
        DEBUG("DBUS: Error: %s", error->message);
        g_error_free(error);
    }
}

void
dbus_add_participant (const gchar* callID, const gchar* confID)
{
    DEBUG ("DBUS: Add participant %s to %s\n", callID, confID);

    GError* error = NULL;

    org_sflphone_SFLphone_CallManager_add_participant (callManagerProxy, callID,
            confID, &error);

    if (error)
        g_error_free (error);
}

void
dbus_add_main_participant (const gchar* confID)
{
    DEBUG ("DBUS: Add main participant %s\n", confID);

    GError* error = NULL;

    org_sflphone_SFLphone_CallManager_add_main_participant (callManagerProxy,
            confID, &error);

    if (error)
        g_error_free (error);
}

void
dbus_detach_participant (const gchar* callID)
{

    DEBUG ("DBUS: Detach participant %s\n", callID);

    GError* error = NULL;
    org_sflphone_SFLphone_CallManager_detach_participant (callManagerProxy,
            callID, &error);

    if (error)
        g_error_free (error);
}

void
dbus_join_conference (const gchar* sel_confID, const gchar* drag_confID)
{

    DEBUG ("dbus_join_conference %s and %s\n", sel_confID, drag_confID);

    GError* error = NULL;

    org_sflphone_SFLphone_CallManager_join_conference (callManagerProxy,
            sel_confID, drag_confID, &error);

    if (error)
        g_error_free (error);
}

void
dbus_set_record (const gchar* id)
{
    DEBUG ("Dbus: dbus_set_record %s", id);

    GError* error = NULL;
    org_sflphone_SFLphone_CallManager_set_recording (callManagerProxy, id, &error);

    if (error)
        g_error_free (error);
}

gboolean
dbus_get_is_recording (const callable_obj_t * c)
{
    DEBUG ("Dbus: dbus_get_is_recording %s", c->_callID);
    GError* error = NULL;
    gboolean isRecording;
    org_sflphone_SFLphone_CallManager_get_is_recording (callManagerProxy,
            c->_callID, &isRecording, &error);

    if (error)
        g_error_free (error);

    //DEBUG("RECORDING: %i",isRecording);
    return isRecording;
}

void
dbus_set_record_path (const gchar* path)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_record_path (
        configurationManagerProxy, path, &error);

    if (error)
        g_error_free (error);
}

gchar*
dbus_get_record_path (void)
{
    GError* error = NULL;
    gchar *path = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_record_path (
        configurationManagerProxy, &path, &error);

    if (error)
        g_error_free (error);

    return path;
}

void dbus_set_is_always_recording(const gboolean alwaysRec)
{
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_is_always_recording(
        configurationManagerProxy, alwaysRec, &error);

    if(error) {
        ERROR("DBUS: Could not set isAlwaysRecording");
        g_error_free(error);
    }
}

gboolean dbus_get_is_always_recording(void)
{
    GError *error = NULL;
    int alwaysRec;
    org_sflphone_SFLphone_ConfigurationManager_get_is_always_recording(
        configurationManagerProxy, &alwaysRec, &error);

    if(error) {
        ERROR("DBUS: Could not get isAlwaysRecording");
        g_error_free(error);
    }

    return alwaysRec;
}

void
dbus_set_history_limit (const guint days)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_history_limit (
        configurationManagerProxy, days, &error);

    if (error)
        g_error_free (error);
}

guint
dbus_get_history_limit (void)
{
    GError* error = NULL;
    gint days = 30;
    org_sflphone_SFLphone_ConfigurationManager_get_history_limit (
        configurationManagerProxy, &days, &error);

    if (error)
        g_error_free (error);

    return (guint) days;
}

void
dbus_set_audio_manager (int api)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_audio_manager (
        configurationManagerProxy, api, &error);

    if (error)
        g_error_free (error);
}

int
dbus_get_audio_manager (void)
{
    int api;
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_audio_manager (
        configurationManagerProxy, &api, &error);

    if (error) {
        ERROR ("Error calling dbus_get_audio_manager: %s", error->message);
        g_error_free (error);
    }

    return api;
}

gchar *
dbus_get_video_input_device_channel ()
{
    gchar *str = NULL;
    GError* error = NULL;

    org_sflphone_SFLphone_ConfigurationManager_get_video_input_device_channel(
        configurationManagerProxy, &str, &error);

    if (error) {
        ERROR ("Error calling dbus_get_video_input_device_channel: %s", error->message);
        g_error_free (error);
    }

    return str;
}

gchar *
dbus_get_video_input_device_size ()
{
    gchar *str = NULL;
    GError* error = NULL;

    org_sflphone_SFLphone_ConfigurationManager_get_video_input_device_size(
        configurationManagerProxy, &str, &error);

    if (error) {
        ERROR ("Error calling dbus_get_video_input_device_size: %s", error->message);
        g_error_free (error);
    }

    return str;
}

gchar *
dbus_get_video_input_device_rate ()
{
    gchar *str = NULL;
    GError* error = NULL;

    org_sflphone_SFLphone_ConfigurationManager_get_video_input_device_rate(
        configurationManagerProxy, &str, &error);

    if (error) {
        ERROR ("Error calling dbus_get_video_input_device_rate: %s", error->message);
        g_error_free (error);
    }

    return str;
}

gchar *
dbus_get_video_input_device ()
{
    gchar *str = NULL;
    GError* error = NULL;

    org_sflphone_SFLphone_ConfigurationManager_get_video_input_device(
        configurationManagerProxy, &str, &error);

    if (error) {
        ERROR ("Error calling dbus_get_video_input_device: %s", error->message);
        g_error_free (error);
    }

    return str;
}

/**
 * Set video input device
 */
void
dbus_set_video_input_device (const gchar *device)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_video_input_device(
        configurationManagerProxy, device, &error);

    if (error) {
        ERROR ("Failed to call set_video_input_device() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }
}

/**
 * Set video input device channel
 */
void
dbus_set_video_input_device_channel (const gchar *channel)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_video_input_device_channel(
        configurationManagerProxy, channel, &error);

    if (error) {
        ERROR ("Failed to call set_video_input_device_channel() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }
}

/**
 * Set video input size
 */
void
dbus_set_video_input_size (const gchar *size)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_video_input_device_size(
        configurationManagerProxy, size, &error);

    if (error) {
        ERROR ("Failed to call set_video_input_device_size() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }
}

/**
 * Set video input rate
 */
void
dbus_set_video_input_rate (const gchar *rate)
{
    GError* error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_video_input_device_rate(
        configurationManagerProxy, rate, &error);

    if (error) {
        ERROR ("Failed to call set_video_input_device_rate() on ConfigurationManager: %s", error->message);
        g_error_free (error);
    }
}

/**
 * Get a list of video input devices
 */
gchar**
dbus_get_video_input_device_list()
{
    gchar** array = NULL;
    GError* error = NULL;

    if (!org_sflphone_SFLphone_ConfigurationManager_get_video_input_device_list (
                configurationManagerProxy, &array, &error)) {
        if (error->domain == DBUS_GERROR && error->code
                == DBUS_GERROR_REMOTE_EXCEPTION) {
            ERROR ("Caught remote method (get_video_input_device_list) exception  %s: %s", dbus_g_error_get_name (error), error->message);
        } else {
            ERROR ("Error while calling get_video_input_device_list: %s", error->message);
        }

        g_error_free (error);
    }

    return array;
}

/**
 * Get a list of inputs supported by the video input device
 */
gchar**
dbus_get_video_input_device_channel_list(const gchar *dev)
{
    gchar** array = NULL;
    GError* error = NULL;

    if (!org_sflphone_SFLphone_ConfigurationManager_get_video_input_device_channel_list (
                configurationManagerProxy, dev, &array, &error)) {
        if (error->domain == DBUS_GERROR && error->code
                == DBUS_GERROR_REMOTE_EXCEPTION) {
            ERROR ("Caught remote method (get_video_input_device_channel_list) exception  %s: %s", dbus_g_error_get_name (error), error->message);
        } else {
            ERROR ("Error while calling get_video_input_device_channel_list: %s", error->message);
        }

        g_error_free (error);
    }
    return array;
}

/**
 * Get a list of resolutions supported by the video input
 */
gchar**
dbus_get_video_input_device_size_list(const gchar *dev, const gchar *channel)
{
    gchar** array = NULL;
    GError* error = NULL;

    if (!org_sflphone_SFLphone_ConfigurationManager_get_video_input_device_size_list (
                configurationManagerProxy, dev, channel, &array, &error)) {
        if (error->domain == DBUS_GERROR && error->code
                == DBUS_GERROR_REMOTE_EXCEPTION) {
            ERROR ("Caught remote method (get_video_input_device_size_list) exception  %s: %s", dbus_g_error_get_name (error), error->message);
        } else {
            ERROR ("Error while calling get_video_input_device_size_list: %s", error->message);
        }

        g_error_free (error);
        return NULL;
    } else {
        return array;
    }
}

/**
 * Get a list of frame rates supported by the video input resolution
 */
gchar**
dbus_get_video_input_device_rate_list(const gchar *dev, const gchar *channel, const gchar *size)
{
    gchar** array = NULL;
    GError* error = NULL;

    if (!org_sflphone_SFLphone_ConfigurationManager_get_video_input_device_rate_list (
                configurationManagerProxy, dev, channel, size, &array, &error)) {
        if (error->domain == DBUS_GERROR && error->code
                == DBUS_GERROR_REMOTE_EXCEPTION) {
            ERROR ("Caught remote method (get_video_input_device_rate_list) exception  %s: %s", dbus_g_error_get_name (error), error->message);
        } else {
            ERROR ("Error while calling get_video_input_device_rate_list: %s", error->message);
        }

        g_error_free (error);
        return NULL;
    } else {
        return array;
    }
}

GHashTable*
dbus_get_addressbook_settings (void)
{

    GError *error = NULL;
    GHashTable *results = NULL;

    //DEBUG ("Calling org_sflphone_SFLphone_ConfigurationManager_get_addressbook_settings");

    org_sflphone_SFLphone_ConfigurationManager_get_addressbook_settings (
        configurationManagerProxy, &results, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_ConfigurationManager_get_addressbook_settings");
        g_error_free (error);
    }

    return results;
}

void
dbus_set_addressbook_settings (GHashTable * settings)
{

    GError *error = NULL;

    DEBUG ("Calling org_sflphone_SFLphone_ConfigurationManager_set_addressbook_settings");

    org_sflphone_SFLphone_ConfigurationManager_set_addressbook_settings (
        configurationManagerProxy, settings, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_ConfigurationManager_set_addressbook_settings");
        g_error_free (error);
    }
}

gchar**
dbus_get_addressbook_list (void)
{
    GError *error = NULL;
    gchar** array = NULL;

    org_sflphone_SFLphone_ConfigurationManager_get_addressbook_list (
        configurationManagerProxy, &array, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_ConfigurationManager_get_addressbook_list");
        g_error_free (error);
    }

    return array;
}

void
dbus_set_addressbook_list (const gchar** list)
{
    GError *error = NULL;

    org_sflphone_SFLphone_ConfigurationManager_set_addressbook_list (
        configurationManagerProxy, list, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_ConfigurationManager_set_addressbook_list");
        g_error_free (error);
    }
}

GHashTable*
dbus_get_hook_settings (void)
{

    GError *error = NULL;
    GHashTable *results = NULL;

    //DEBUG ("Calling org_sflphone_SFLphone_ConfigurationManager_get_addressbook_settings");

    org_sflphone_SFLphone_ConfigurationManager_get_hook_settings (
        configurationManagerProxy, &results, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_ConfigurationManager_get_hook_settings");
        g_error_free (error);
    }

    return results;
}

void
dbus_set_hook_settings (GHashTable * settings)
{
    GError *error = NULL;

    org_sflphone_SFLphone_ConfigurationManager_set_hook_settings (
        configurationManagerProxy, settings, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_ConfigurationManager_set_hook_settings");
        g_error_free (error);
    }
}

GHashTable*
dbus_get_call_details (const gchar *callID)
{
    GError *error = NULL;
    GHashTable *details = NULL;

    org_sflphone_SFLphone_CallManager_get_call_details (callManagerProxy, callID,
            &details, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_CallManager_get_call_details");
        g_error_free (error);
    }

    return details;
}

gchar**
dbus_get_call_list (void)
{
    GError *error = NULL;
    gchar **list = NULL;

    org_sflphone_SFLphone_CallManager_get_call_list (callManagerProxy, &list,
            &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_CallManager_get_call_list");
        g_error_free (error);
    }

    return list;
}

gchar**
dbus_get_conference_list (void)
{
    GError *error = NULL;
    gchar **list = NULL;

    org_sflphone_SFLphone_CallManager_get_conference_list (callManagerProxy,
            &list, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_CallManager_get_conference_list");
        g_error_free (error);
    }

    return list;
}

gchar**
dbus_get_participant_list (const gchar *confID)
{
    GError *error = NULL;
    char **list = NULL;

    DEBUG ("DBUS: Get conference %s participant list", confID);

    org_sflphone_SFLphone_CallManager_get_participant_list (callManagerProxy,
            confID, &list, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_CallManager_get_participant_list");
        g_error_free (error);
    }

    return list;
}

GHashTable*
dbus_get_conference_details (const gchar *confID)
{
    GError *error = NULL;
    GHashTable *details = NULL;

    org_sflphone_SFLphone_CallManager_get_conference_details (callManagerProxy,
            confID, &details, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_CallManager_get_conference_details");
        g_error_free (error);
    }

    return details;
}

void
dbus_set_accounts_order (const gchar* order)
{

    GError *error = NULL;

    org_sflphone_SFLphone_ConfigurationManager_set_accounts_order (
        configurationManagerProxy, order, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_ConfigurationManager_set_accounts_order");
        g_error_free (error);
    }
}

gchar **
dbus_get_history (void)
{
    GError *error = NULL;
    gchar **entries = NULL;

    org_sflphone_SFLphone_ConfigurationManager_get_history (
        configurationManagerProxy, &entries, &error);

    if (error) {
        ERROR ("Error calling get history: %s", error->message);
        g_error_free (error);
    }

    return entries;
}

void
dbus_set_history (gchar **entries)
{
    GError *error = NULL;

    org_sflphone_SFLphone_ConfigurationManager_set_history (
        configurationManagerProxy, (const char **)entries, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_CallManager_set_history");
        g_error_free (error);
    }
}

void
dbus_confirm_sas (const callable_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_set_sa_sverified (callManagerProxy,
            c->_callID, &error);

    if (error) {
        ERROR ("Failed to call setSASVerified() on CallManager: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_reset_sas (const callable_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_reset_sa_sverified (callManagerProxy,
            c->_callID, &error);

    if (error) {
        ERROR ("Failed to call resetSASVerified on CallManager: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_set_confirm_go_clear (const callable_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_set_confirm_go_clear (callManagerProxy,
            c->_callID, &error);

    if (error) {
        ERROR ("Failed to call set_confirm_go_clear on CallManager: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_request_go_clear (const callable_obj_t * c)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_request_go_clear (callManagerProxy,
            c->_callID, &error);

    if (error) {
        ERROR ("Failed to call request_go_clear on CallManager: %s",
               error->message);
        g_error_free (error);
    }
}

gchar**
dbus_get_supported_tls_method()
{
    GError *error = NULL;
    gchar** array = NULL;
    org_sflphone_SFLphone_ConfigurationManager_get_supported_tls_method (
        configurationManagerProxy, &array, &error);

    if (error) {
        ERROR ("Failed to call get_supported_tls_method() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }

    return array;
}

GHashTable*
dbus_get_tls_settings_default (void)
{
    GError *error = NULL;
    GHashTable *results = NULL;

    org_sflphone_SFLphone_ConfigurationManager_get_tls_settings_default (
        configurationManagerProxy, &results, &error);

    if (error) {
        ERROR ("Error calling org_sflphone_SFLphone_ConfigurationManager_get_tls_settings_default");
        g_error_free (error);
    }

    return results;
}

gchar *
dbus_get_address_from_interface_name (gchar* interface)
{
    GError *error = NULL;
    gchar * address = NULL;

    org_sflphone_SFLphone_ConfigurationManager_get_addr_from_interface_name (
        configurationManagerProxy, interface, &address, &error);

    if (error != NULL) {
        ERROR ("Error calling org_sflphone_SFLphone_ConfigurationManager_get_addr_from_interface_name\n");
        g_error_free (error);
    }

    return address;
}

gchar **
dbus_get_all_ip_interface (void)
{
    GError *error = NULL;
    gchar ** array = NULL;

    if (!org_sflphone_SFLphone_ConfigurationManager_get_all_ip_interface (
                configurationManagerProxy, &array, &error)) {
        if (error->domain == DBUS_GERROR &&
                error->code == DBUS_GERROR_REMOTE_EXCEPTION)
            ERROR ("Caught remote method (get_all_ip_interface) exception  %s: %s", dbus_g_error_get_name (error), error->message);
        else
            ERROR ("Error while calling get_all_ip_interface: %s", error->message);

        g_error_free (error);
    } else
        DEBUG ("DBus called get_all_ip_interface() on ConfigurationManager");
    return array;
}

gchar **
dbus_get_all_ip_interface_by_name (void)
{
    GError *error = NULL;
    gchar ** array = NULL;

    if (!org_sflphone_SFLphone_ConfigurationManager_get_all_ip_interface_by_name (
                configurationManagerProxy, &array, &error)) {
        if (error->domain == DBUS_GERROR &&
                error->code == DBUS_GERROR_REMOTE_EXCEPTION)
            ERROR ("Caught remote method (get_all_ip_interface) exception  %s: %s", dbus_g_error_get_name (error), error->message);
        else
            ERROR ("Error while calling get_all_ip_interface: %s", error->message);

        g_error_free (error);
    } else
        DEBUG ("DBus called get_all_ip_interface() on ConfigurationManager");
    return array;
}

GHashTable*
dbus_get_shortcuts (void)
{
    GError *error = NULL;
    GHashTable * shortcuts = NULL;

    if (!org_sflphone_SFLphone_ConfigurationManager_get_shortcuts (
                configurationManagerProxy, &shortcuts, &error)) {
        if (error->domain == DBUS_GERROR &&
                error->code == DBUS_GERROR_REMOTE_EXCEPTION)
            ERROR ("Caught remote method (get_shortcuts) exception  %s: %s",
                    dbus_g_error_get_name (error), error->message);
        else
            ERROR ("Error while calling get_shortcuts: %s", error->message);

        g_error_free (error);
    }
    return shortcuts;
}

void
dbus_set_shortcuts (GHashTable * shortcuts)
{
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_set_shortcuts (
        configurationManagerProxy, shortcuts, &error);

    if (error) {
        ERROR ("Failed to call set_shortcuts() on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_send_text_message (const gchar* callID, const gchar *message)
{
    GError *error = NULL;
    org_sflphone_SFLphone_CallManager_send_text_message (
        callManagerProxy, callID, message, &error);

    if (error) {
        ERROR ("Failed to call send_text_message() on CallManager: %s",
               error->message);
        g_error_free (error);
    }
}

void
dbus_start_video_preview (int width, int height, const char *format)
{
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_start_video_preview_async (
        configurationManagerProxy, width, height, format, video_started_cb, &error);

    if (error) {
        ERROR ("Failed to call start_video_preview () on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }
}

static void preview_stopped_cb()
{
    DEBUG("Video preview has stopped");
}

void
dbus_stop_video_preview ()
{
    GError *error = NULL;
    org_sflphone_SFLphone_ConfigurationManager_stop_video_preview_async(
        configurationManagerProxy, preview_stopped_cb, &error);

    if (error) {
        ERROR ("Failed to call stop_video_preview () on ConfigurationManager: %s",
               error->message);
        g_error_free (error);
    }
}
