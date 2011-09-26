/*
 *  Copyright (C) 2004, 2005, 2006, 2009, 2008, 2009, 2010, 2011 Savoir-Faire Linux Inc.
 *  Author: Tristan Matthews <tristan.matthews@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
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

#include "video_renderer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h> /* semaphore functions and structs. */
#include <sys/shm.h>

#include <assert.h>
#include <string.h>

#include <clutter/clutter.h>
#include <clutter-gtk/clutter-gtk.h>
#include <cairo.h>

#include "actions.h"

static GtkWidget *receivingVideoWindow;
static GtkWidget *receivingVideoArea;
static VideoRenderer *video_renderer = NULL;


#define VIDEO_RENDERER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
            VIDEO_RENDERER_TYPE, VideoRendererPrivate))

/* This macro will implement the video_renderer_get_type function
   and define a parent class pointer accessible from the whole .c file */
G_DEFINE_TYPE (VideoRenderer, video_renderer, G_TYPE_OBJECT);

enum
{
    PROP_0,
    PROP_RUNNING,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_DRAWAREA,
    PROP_SHMKEY,
    PROP_SEMKEY,
    PROP_VIDEO_BUFFER_SIZE,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static void video_renderer_finalize (GObject *gobject);
static void video_renderer_get_property (GObject *object, guint prop_id,
                            GValue *value, GParamSpec *pspec);
static void video_renderer_set_property (GObject *object, guint prop_id,
                            const GValue *value, GParamSpec *pspec);

/* Our private member structure */
struct _VideoRendererPrivate {
    guint width;
    guint height;

    gchar *shm_buffer;
    gint sem_set_id;

    gint sem_key;
    gint shm_key;
    gint videobuffersize;
    gboolean using_clutter;
    ClutterActor *texture;

    gpointer drawarea;

    gboolean is_running;
};

/* See /bits/sem.h line 55 for why this is necessary */
#if _SEM_SEMUN_UNDEFINED
union semun
{
    int val;				    /* value for SETVAL */
    struct semid_ds *buf;		/* buffer for IPC_STAT & IPC_SET */
    unsigned short int *array;	/* array for GETALL & SETALL */
    struct seminfo *__buf;		/* buffer for IPC_INFO */
};
#endif

/* join and/or create a shared memory segment */
static int
getShm(unsigned numBytes, int shmKey)
{
    key_t key = shmKey;
    int shm_id;
    /* connect to a segment with 600 permissions
       (r--r--r--) */
    shm_id = shmget(key, numBytes, 0644);

    if (shm_id == -1)
      perror("shmget");

    return shm_id;
}


/* attach a shared memory segment */
static char *
attachShm(int shm_id)
{
    char *data = NULL;

    /* attach to the segment and get a pointer to it */
    data = shmat(shm_id, (void *)0, 0);
    if (data == (char *)(-1)) {
        perror("shmat");
        data = NULL;
    }

    return data;
}

static void
detachShm(char *data)
{
    /* detach from the segment: */
    if (shmdt(data) == -1) {
        perror("shmdt");
    }
}

static int
get_sem_set(int semKey)
{
    /* this variable will contain the semaphore set. */
    int sem_set_id;
    key_t key = semKey;

    /* semaphore value, for semctl().                */
    union semun sem_val;

    /* first we get a semaphore set with a single semaphore, */
    /* whose counter is initialized to '0'.                     */
    sem_set_id = semget(key, 1, 0600);
    if (sem_set_id == -1) {
        perror("semget");
        return sem_set_id;
    }
    sem_val.val = 0;
    semctl(sem_set_id, 0, SETVAL, sem_val);
    return sem_set_id;
}

static void
video_renderer_class_init (VideoRendererClass *klass)
{
    int i;
    GObjectClass *gobject_class;
    gobject_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (VideoRendererPrivate));
    gobject_class->finalize = video_renderer_finalize;
    gobject_class->get_property = video_renderer_get_property;
    gobject_class->set_property = video_renderer_set_property;

    properties[PROP_RUNNING] = g_param_spec_boolean ("running", "Running",
                                                     "True if preview is running",
                                                     FALSE,
                                                     G_PARAM_READABLE);

    properties[PROP_DRAWAREA] = g_param_spec_pointer ("drawarea", "DrawArea",
                                                     "Pointer to the drawing area",
                                                     G_PARAM_READABLE|G_PARAM_WRITABLE|G_PARAM_CONSTRUCT_ONLY);

    properties[PROP_WIDTH] = g_param_spec_int ("width", "Width", "Width of preview video", G_MININT, G_MAXINT, -1,
                                                     G_PARAM_READABLE|G_PARAM_WRITABLE|G_PARAM_CONSTRUCT_ONLY);


    properties[PROP_HEIGHT] = g_param_spec_int ("height", "Height", "Height of preview video", G_MININT, G_MAXINT, -1,
                                                     G_PARAM_READABLE|G_PARAM_WRITABLE|G_PARAM_CONSTRUCT_ONLY);

    properties[PROP_SHMKEY] = g_param_spec_int ("shmkey", "ShmKey", "Unique key for shared memory identifier", G_MININT, G_MAXINT, -1,
                                                     G_PARAM_READABLE|G_PARAM_WRITABLE|G_PARAM_CONSTRUCT_ONLY);

    properties[PROP_SEMKEY] = g_param_spec_int ("semkey", "SemKey", "Unique key for semaphore identifier", G_MININT, G_MAXINT, -1,
                                                     G_PARAM_READABLE|G_PARAM_WRITABLE|G_PARAM_CONSTRUCT_ONLY);

    properties[PROP_VIDEO_BUFFER_SIZE] = g_param_spec_int ("vbsize", "VideoBufferSize", "Size of shared memory buffer", G_MININT, G_MAXINT, -1,
                                                     G_PARAM_READABLE|G_PARAM_WRITABLE|G_PARAM_CONSTRUCT_ONLY);

    for (i = PROP_0 + 1; i < PROP_LAST; i++) {
        g_object_class_install_property (gobject_class, i, properties[i]);
    }
}

static void
video_renderer_get_property (GObject *object, guint prop_id,
                            GValue *value, GParamSpec *pspec)
{
    VideoRenderer *preview;
    VideoRendererPrivate *priv;

    preview = VIDEO_RENDERER(object);
    priv = preview->priv;

    switch (prop_id)
    {
        case PROP_RUNNING:
            g_value_set_boolean (value, priv->is_running);
            break;
        case PROP_DRAWAREA:
            g_value_set_pointer(value, priv->drawarea);
            break;
        case PROP_WIDTH:
            g_value_set_int(value, priv->width);
            break;
        case PROP_HEIGHT:
            g_value_set_int(value, priv->height);
            break;
        case PROP_SHMKEY:
            g_value_set_int(value, priv->shm_key);
            break;
        case PROP_SEMKEY:
            g_value_set_int(value, priv->sem_key);
            break;
        case PROP_VIDEO_BUFFER_SIZE:
            g_value_set_int(value, priv->videobuffersize);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
video_renderer_set_property (GObject *object, guint prop_id,
                            const GValue *value, GParamSpec *pspec)
{
    VideoRenderer *preview;
    VideoRendererPrivate *priv;

    preview = VIDEO_RENDERER(object);
    priv = preview->priv;

    switch (prop_id)
    {
        case PROP_DRAWAREA:
            priv->drawarea = g_value_get_pointer(value);
            break;
        case PROP_WIDTH:
            priv->width = g_value_get_int(value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_int(value);
            break;
        case PROP_SHMKEY:
            priv->shm_key = g_value_get_int(value);
            break;
        case PROP_SEMKEY:
            priv->sem_key = g_value_get_int(value);
            break;
        case PROP_VIDEO_BUFFER_SIZE:
            priv->videobuffersize = g_value_get_int(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}


static void
video_renderer_init (VideoRenderer *self)
{
    self->priv = VIDEO_RENDERER_GET_PRIVATE (self);
}

static void
video_renderer_finalize (GObject *obj)
{
    VideoRenderer *self = VIDEO_RENDERER (obj);
    if (self->priv->shm_buffer)
        detachShm(self->priv->shm_buffer);

    /* Chain up to the parent class */
    G_OBJECT_CLASS (video_renderer_parent_class)->finalize (obj);
}

/*
 * function: sem_wait. wait for frame from other process
 * input:    semaphore set ID.
 * output:   none.
 */
static int
sem_wait(int sem_set_id)
{
    /* structure for semaphore operations.   */
    struct sembuf sem_op;

    /* wait on the semaphore, unless it's value is non-negative. */
    sem_op.sem_num = 0;
    sem_op.sem_op = -1;
    sem_op.sem_flg = IPC_NOWAIT;
    return semop(sem_set_id, &sem_op, 1);
}

static gboolean
readFrameFromShm(VideoRendererPrivate *priv)
{
    int width = priv->width;
    int height = priv->height;
    void *data = priv->shm_buffer;
    int sem_set_id = priv->sem_set_id;
    ClutterActor *texture = priv->texture;

    if (sem_set_id == -1)
        return FALSE;

    if (sem_wait(sem_set_id) == -1) {
      if (errno != EAGAIN) {
          g_print("Could not read from shared memory!\n");
          perror("shm: ");
          return FALSE;
      }
      else {
          /* No new frame, so we'll try later */
          return TRUE;
      }
    }

    if (priv->using_clutter) {
        clutter_texture_set_from_rgb_data(CLUTTER_TEXTURE(texture),
                (void*)data,
                TRUE,
                width,
                height,
                4 /* bytes per pixel */ * width, // stride
                4,
                CLUTTER_TEXTURE_RGB_FLAG_BGR,
                NULL);
    } else {
        const cairo_format_t format = CAIRO_FORMAT_RGB24;
        int stride = cairo_format_stride_for_width (format, width);
        cairo_surface_t *surface = cairo_image_surface_create_for_data (data,
                                                                 format,
                                                                 width,
                                                                 height,
                                                                 stride);

        if (surface) {
            cairo_t *cairo = gdk_cairo_create(gtk_widget_get_window(priv->drawarea));
            cairo_set_source_surface(cairo, surface, 0, 0);

            cairo_status_t status = cairo_surface_status(surface);
            if (status != CAIRO_STATUS_SURFACE_FINISHED)
                cairo_paint(cairo);

            cairo_surface_destroy(surface);
            cairo_destroy(cairo);
        }
    }

    return TRUE;
}

void
video_renderer_stop(VideoRenderer *preview)
{
    VideoRendererPrivate *priv = VIDEO_RENDERER_GET_PRIVATE(preview);
    assert(priv);

    priv->is_running = FALSE;

#if GLIB_CHECK_VERSION(2,26,0)
    g_object_notify_by_pspec(G_OBJECT(preview), properties[PROP_RUNNING]);
#else
    g_object_notify(G_OBJECT(preview), "running");
#endif

    g_object_unref(G_OBJECT(preview));
}

static gboolean
updateTexture(gpointer data)
{
    VideoRenderer *preview = (VideoRenderer *) data;
    VideoRendererPrivate *priv = VIDEO_RENDERER_GET_PRIVATE(preview);

    if (!priv->is_running) {
        /* preview was stopped already */
        g_object_unref(G_OBJECT(data));
        return FALSE;
    }

    gboolean ret = readFrameFromShm(priv);

    if (!ret) {
        priv->is_running = FALSE; // no need to notify daemon
        video_renderer_stop(data);
        g_object_unref(G_OBJECT(data));
    }

    return ret;
}

/**
 * video_renderer_new:
 *
 * Create a new #VideoRenderer instance.
 */
VideoRenderer *
video_renderer_new (GtkWidget *drawarea, int width, int height, int shmkey, int semkey, int vbsize)
{
    VideoRenderer *result;

    result = g_object_new (VIDEO_RENDERER_TYPE,
          "drawarea", (gpointer)drawarea,
          "width", (gint)width,
          "height", (gint)height,
          "shmkey", (gint)shmkey,
          "semkey", (gint)semkey,
          "vbsize", (gint)vbsize,
          NULL);
    return result;
}

int
video_renderer_run(VideoRenderer *preview)
{
    VideoRendererPrivate * priv = VIDEO_RENDERER_GET_PRIVATE(preview);
    priv->shm_buffer = NULL;

    int shm_id = getShm(priv->videobuffersize, priv->shm_key);
    if (shm_id == -1)
        return 1;

    priv->shm_buffer = attachShm(shm_id);
    if (!priv->shm_buffer)
        return 1;

    priv->sem_set_id = get_sem_set(priv->sem_key);
    if (priv->sem_set_id == -1)
        return 1;

    priv->using_clutter = GTK_CLUTTER_IS_EMBED(priv->drawarea);
    g_print("Preview: using %s render\n", priv->using_clutter ? "clutter" : "cairo");

    if (priv->using_clutter) {
        ClutterActor *stage;

        stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED(priv->drawarea));
        assert(stage);
        priv->texture = clutter_texture_new();

        /* Add ClutterTexture to the stage */
        clutter_container_add(CLUTTER_CONTAINER (stage), priv->texture, NULL);

        clutter_actor_show_all(stage);
    }

    /* frames are read and saved here */
    g_object_ref(preview);
    g_timeout_add(1000/25, updateTexture, preview);

    priv->is_running = TRUE;
    /* emit the notify signal for this property */
#if GLIB_CHECK_VERSION(2,26,0)
    g_object_notify_by_pspec (G_OBJECT(preview), properties[PROP_RUNNING]);
#else
    g_object_notify(G_OBJECT(data), "running");
#endif

    return 0;
}

static void receiving_video_window_deleted_cb(GtkWidget *widget UNUSED, gpointer data UNUSED)
{
    sflphone_hang_up();
}


void receiving_video_event_cb(DBusGProxy *proxy, gint shmKey, gint semKey,
                              gint videoBufferSize, gint destWidth,
                              gint destHeight, GError *error, gpointer userdata)
{
    if (!receivingVideoWindow) {
        receivingVideoWindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        g_signal_connect (receivingVideoWindow, "delete-event", G_CALLBACK (receiving_video_window_deleted_cb), NULL);
    }

    (void)proxy;
    (void)error;
    (void)userdata;
    gboolean using_clutter = clutter_init(NULL, NULL) == CLUTTER_INIT_SUCCESS;

    if (!receivingVideoArea) {
        if (using_clutter) {
            receivingVideoArea = gtk_clutter_embed_new();
            if (!gtk_clutter_embed_get_stage(GTK_CLUTTER_EMBED(receivingVideoArea))) {
                gtk_widget_destroy(receivingVideoArea);
                using_clutter = 0;
            }
        }
        if (!using_clutter)
          receivingVideoArea = gtk_drawing_area_new();
    }

    g_assert(receivingVideoArea);
    gtk_container_add(GTK_CONTAINER(receivingVideoWindow), receivingVideoArea);

    if (shmKey == -1 || semKey == -1 || videoBufferSize == -1)
        return;

    gtk_widget_set_size_request (receivingVideoArea, destWidth, destHeight);
    gtk_widget_show_all(receivingVideoWindow);

    DEBUG("Video started for shm:%d sem:%d bufferSz:%d width:%d height:%d",
           shmKey, semKey, videoBufferSize, destWidth, destHeight);

    video_renderer = video_renderer_new(receivingVideoArea, destWidth, destHeight, shmKey, semKey, videoBufferSize);
    g_assert(video_renderer);
    if (video_renderer_run(video_renderer)) {
        g_object_unref(video_renderer);
        video_renderer = NULL;
        DEBUG("Could not run video renderer");
    }
    else
        DEBUG("Running video renderer");
}

void stopped_receiving_video_event_cb(DBusGProxy *proxy, gint shmKey, gint semKey, GError *error, gpointer userdata)
{
    (void)proxy;
    (void)error;
    (void)userdata;

    DEBUG("Video stopped for shm:%d sem:%d", shmKey, semKey);

    if (video_renderer) {
        if (receivingVideoWindow) {
            if (GTK_IS_WIDGET(receivingVideoWindow))
                    gtk_widget_destroy(receivingVideoWindow);
            receivingVideoArea = receivingVideoWindow = NULL;
        }
        video_renderer = NULL;
    }
}
