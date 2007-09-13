#include <config.h>

#include "gasynchelper.h"

static void
async_result_free (gpointer data)
{
  GAsyncResultData *res = data;

  if (res->error)
    g_error_free (res->error);

  g_object_unref (res->async_object);
  
  g_free (res);
}

void
_g_queue_async_result (GAsyncResultData *result,
		       gpointer        async_object,
		       GError         *error,
		       gpointer        user_data,
		       GSourceFunc     source_func)
{
  GSource *source;

  g_return_if_fail (G_IS_OBJECT (async_object));
  
  result->async_object = g_object_ref (async_object);
  result->user_data = user_data;
  result->error = error;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_callback (source, source_func, result, async_result_free);
  g_source_attach (source, NULL);
  g_source_unref (source);
}

/*************************************************************************
 *             fd source                                                 *
 ************************************************************************/

typedef struct 
{
  GSource source;
  GPollFD pollfd;
  GCancellable *cancellable;
  gulong cancelled_tag;
} FDSource;

static gboolean 
fd_source_prepare (GSource  *source,
		   gint     *timeout)
{
  FDSource *fd_source = (FDSource *)source;
  *timeout = -1;
  
  return g_cancellable_is_cancelled (fd_source->cancellable);
}

static gboolean 
fd_source_check (GSource  *source)
{
  FDSource *fd_source = (FDSource *)source;

  return
    g_cancellable_is_cancelled  (fd_source->cancellable) ||
    fd_source->pollfd.revents != 0;
}

static gboolean
fd_source_dispatch (GSource     *source,
		    GSourceFunc  callback,
		    gpointer     user_data)

{
  GFDSourceFunc func = (GFDSourceFunc)callback;
  FDSource *fd_source = (FDSource *)source;

  g_assert (func != NULL);

  return (*func) (user_data, fd_source->pollfd.revents, fd_source->pollfd.fd);
}

static void 
fd_source_finalize (GSource *source)
{
  FDSource *fd_source = (FDSource *)source;

  if (fd_source->cancelled_tag)
    g_signal_handler_disconnect (fd_source->cancellable,
				 fd_source->cancelled_tag);

  if (fd_source->cancellable)
    g_object_unref (fd_source->cancellable);
}

static GSourceFuncs fd_source_funcs = {
  fd_source_prepare,
  fd_source_check,
  fd_source_dispatch,
  fd_source_finalize
};

/* Might be called on another thread */
static void
fd_source_cancelled_cb (GCancellable *cancellable,
			gpointer data)
{
  /* Wake up the mainloop in case we're waiting on async calls with FDSource */
  g_main_context_wakeup (NULL);
}

GSource *
_g_fd_source_new (int fd,
		  gushort events,
		  GCancellable *cancellable)
{
  GSource *source;
  FDSource *fd_source;

  source = g_source_new (&fd_source_funcs, sizeof (FDSource));
  fd_source = (FDSource *)source;

  if (cancellable)
    fd_source->cancellable = g_object_ref (cancellable);
  
  fd_source->pollfd.fd = fd;
  fd_source->pollfd.events = events;
  g_source_add_poll (source, &fd_source->pollfd);

  if (cancellable)
    fd_source->cancelled_tag =
      g_signal_connect_data (cancellable, "cancelled",
			     (GCallback)fd_source_cancelled_cb,
			     NULL, NULL,
			     0);
  
  return source;
}
