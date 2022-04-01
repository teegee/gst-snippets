/*
 * Dynamic pipelines example, uridecodebin with sinks added and removed
 *
 * Copyright (c) 2014 Sebastian Dröge <sebastian@centricular.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <string.h>
#include <gst/gst.h>

static GMainLoop *loop;
static GstElement *pipeline;
static GstElement *pbin, *vsbin, *tee;
static GList *sinks;

typedef struct
{
  GstPad *teepad;
  GstElement *queue;
  GstElement *conv;
  GstElement *sink;
  gboolean removing;
} Sink;

static gboolean
message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (message->src);
      gst_message_parse_error (message, &err, &debug);

      g_printerr ("ERROR: from element %s: %s\n", name, err->message);
      if (debug != NULL)
        g_printerr ("Additional debug info:\n%s\n", debug);

      g_error_free (err);
      g_free (debug);
      g_free (name);

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_WARNING:{
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (message->src);
      gst_message_parse_warning (message, &err, &debug);

      g_printerr ("ERROR: from element %s: %s\n", name, err->message);
      if (debug != NULL)
        g_printerr ("Additional debug info:\n%s\n", debug);

      g_error_free (err);
      g_free (debug);
      g_free (name);
      break;
    }
    case GST_MESSAGE_EOS:
      g_print ("Got EOS\n");
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return TRUE;
}

static GstPadProbeReturn
unlink_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  Sink *sink = user_data;
  GstPad *sinkpad;

  if (!g_atomic_int_compare_and_exchange (&sink->removing, FALSE, TRUE))
    return GST_PAD_PROBE_OK;

  sinkpad = gst_element_get_static_pad (sink->queue, "sink");
  gst_pad_unlink (sink->teepad, sinkpad);
  gst_object_unref (sinkpad);

  gst_bin_remove (GST_BIN (vsbin), sink->queue);
  gst_bin_remove (GST_BIN (vsbin), sink->conv);
  gst_bin_remove (GST_BIN (vsbin), sink->sink);

  gst_element_set_state (sink->sink, GST_STATE_NULL);
  gst_element_set_state (sink->conv, GST_STATE_NULL);
  gst_element_set_state (sink->queue, GST_STATE_NULL);

  gst_object_unref (sink->queue);
  gst_object_unref (sink->conv);
  gst_object_unref (sink->sink);

  gst_element_release_request_pad (tee, sink->teepad);
  gst_object_unref (sink->teepad);

  g_print ("removed\n");

  return GST_PAD_PROBE_REMOVE;
}

static gboolean
tick_cb (gpointer data)
{
    Sink *sink = g_new0 (Sink, 1);
    GstPad *sinkpad;
    GstPadTemplate *templ;

    if (!sinks) {
        GST_DEBUG_BIN_TO_DOT_FILE
            (GST_BIN (pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "before");

        templ =
            gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (tee),
                                                "src_%u");

        g_print ("add\n");

        sink->teepad = gst_element_request_pad (tee, templ, NULL, NULL);

        sink->queue = gst_element_factory_make ("queue", "vsbqueue");
        sink->conv = gst_element_factory_make ("videoconvert", "vsbconv");
        sink->sink = gst_element_factory_make ("autovideosink", "vsbsink");
        sink->removing = FALSE;

        gst_bin_add_many (GST_BIN (vsbin), gst_object_ref (sink->queue),
                          gst_object_ref (sink->conv),
                          gst_object_ref (sink->sink), NULL);
        gst_element_link_many (sink->queue, sink->conv, sink->sink, NULL);

        gst_element_sync_state_with_parent (sink->queue);
        gst_element_sync_state_with_parent (sink->conv);
        gst_element_sync_state_with_parent (sink->sink);

        sinkpad = gst_element_get_static_pad (sink->queue, "sink");

        gst_pad_link (sink->teepad, sinkpad);
        gst_object_unref (sinkpad);

        g_print ("added\n");

        GST_DEBUG_BIN_TO_DOT_FILE
            (GST_BIN (pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "added");

        sinks = g_list_append (sinks, sink);
    }

    else {
        Sink *sink;

        g_print ("remove\n");

        sink = sinks->data;
        sinks = g_list_delete_link (sinks, sinks);
        gst_pad_add_probe (sink->teepad, GST_PAD_PROBE_TYPE_IDLE, unlink_cb,
                           sink, (GDestroyNotify) g_free);
    }

    return TRUE;
}


int
main (int argc, char **argv)
{
  GstBus *bus;

  if (argc != 2) {
    g_error ("Usage: %s filename", argv[0]);
    return 0;
  }

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new (NULL);
  pbin = gst_element_factory_make ("playbin", NULL);

  // Create a new bin to act as playbin videosink.
  vsbin = gst_bin_new ("videosinkbin");
  tee = gst_element_factory_make ("tee", NULL);
  GstElement *queue = gst_element_factory_make ("queue", NULL);
  GstElement *sink = gst_element_factory_make ("fakesink", NULL);

  if (!pipeline || !pbin || !tee || !queue || !sink) {
    g_error ("Failed to create elements");
    return -1;
  }

  g_object_set (sink, "sync", TRUE, NULL);
  g_object_set (pbin, "uri", argv[1], NULL);

  gst_bin_add_many (GST_BIN (pipeline), pbin, NULL);
  gst_bin_add_many (GST_BIN (vsbin), tee, queue, sink, NULL);

  if (!gst_element_link_many (queue, sink, NULL)) {
    g_error ("Failed to link elements");
    return -2;
  }


  GstPadTemplate *templ =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (tee),
                                          "src_%u");
  GstPad *teepad = gst_element_request_pad (tee, templ, NULL, NULL);

  GstPad *sinkpad = gst_element_get_static_pad (queue, "sink");
  gst_pad_link (teepad, sinkpad);
  gst_object_unref (sinkpad);

  GstPad     *pad = gst_element_get_static_pad (tee, "sink");
  GstPad     *ghostPad = gst_ghost_pad_new ("sink", pad);

  gst_pad_set_active (ghostPad, TRUE);
  gst_element_add_pad (vsbin, ghostPad);
  gst_object_unref (pad);

  g_object_set (pbin, "video-sink", vsbin, NULL);



  g_timeout_add_seconds (2, tick_cb, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (message_cb), NULL);
  gst_object_unref (GST_OBJECT (bus));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_main_loop_unref (loop);

  gst_object_unref (pipeline);

  return 0;
}
