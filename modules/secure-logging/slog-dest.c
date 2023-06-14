#include "slog-dest.h"
#include "slog-dest-parser.h"

#include "plugin.h"
#include "messages.h"
#include "misc.h"
#include "stats/stats-registry.h"
#include "logqueue.h"
#include "driver.h"
#include "plugin-types.h"
#include "logthrdest/logthrdestdrv.h"

typedef struct
{
  LogThreadedDestDriver super;
  gchar *filename;
} SlogDriver;

/*
 * Configuration
 */

void slog_dd_set_filename(LogDriver *d, const gchar *filename)
{
    SlogDriver *self = (SlogDriver *)d;

    g_free(self->filename);
    self->filename = g_strdup(filename);
}

/*
 * Utilities
 */

static const gchar * slog_dd_format_stats_instance(LogThreadedDestDriver *d)
{
    SlogDriver *self = (SlogDriver *)d;
    static gchar persist_name[1024];

    g_snprintf(persist_name, sizeof(persist_name),
              "slog,%s", self->filename);

    return persist_name;
}

static const gchar * slog_dd_format_persist_name(const LogPipe *d)
{
    SlogDriver *self = (SlogDriver *)d;
    static gchar persist_name[1024];

    if (d->persist_name)
      g_snprintf(persist_name, sizeof(persist_name), "slog.%s", d->persist_name);
    else
      g_snprintf(persist_name, sizeof(persist_name), "slog.%s", self->filename);

  return persist_name;
}

static gboolean slog_dd_connect(SlogDriver *self, gboolean reconnect)
{
  msg_debug("Dummy connection succeeded",
            evt_tag_str("driver", self->super.super.super.id), NULL);

  return TRUE;
}

static void slog_dd_disconnect(LogThreadedDestDriver *d)
{
    SlogDriver *self = (SlogDriver *)d;

  msg_debug("Dummy connection closed",
            evt_tag_str("driver", self->super.super.super.id), NULL);
}

/*
 * Worker thread
 */

static LogThreadedResult slog_worker_insert(LogThreadedDestDriver *d, LogMessage *msg)
{
    SlogDriver *self = (SlogDriver *)d;

  msg_debug("Dummy message sent",
            evt_tag_str("driver", self->super.super.super.id),
            evt_tag_str("filename", self->filename),
            NULL);

  return LTR_SUCCESS;
  /*
   * LTR_DROP,
   * LTR_ERROR,
   * LTR_SUCCESS,
   * LTR_QUEUED,
   * LTR_NOT_CONNECTED,
   * LTR_RETRY,
  */
}

static void slog_worker_thread_init(LogThreadedDestDriver *d)
{
    SlogDriver *self = (SlogDriver *)d;

    msg_debug("Worker thread started",
              evt_tag_str("driver", self->super.super.super.id),
              NULL);

  slog_dd_connect(self, FALSE);
}

static void slog_worker_thread_deinit(LogThreadedDestDriver *d)
{
    SlogDriver *self = (SlogDriver *)d;

    msg_debug("Worker thread stopped",
              evt_tag_str("driver", self->super.super.super.id),
              NULL);
}

/*
 * Main thread
 */

static gboolean slog_dd_init(LogPipe *d)
{
    SlogDriver *self = (SlogDriver *)d;
    GlobalConfig *cfg = log_pipe_get_config(d);

    if (!log_threaded_dest_driver_init_method(d))
        return FALSE;

    msg_verbose("Initializing Dummy destination",
                evt_tag_str("driver", self->super.super.super.id),
                evt_tag_str("filename", self->filename),
                NULL);

    return log_threaded_dest_driver_start_workers(&self->super);
}

static void slog_dd_free(LogPipe *d)
{
    SlogDriver *self = (SlogDriver *)d;

    g_free(self->filename);

    log_threaded_dest_driver_free(d);
}

/*
 * Plugin glue.
 */

LogDriver * sdlog_dd_new(GlobalConfig *cfg)
{
    SlogDriver *self = g_new0(SlogDriver, 1);

    log_threaded_dest_driver_init_instance(&self->super, cfg);
    self->super.super.super.super.init = slog_dd_init;
    self->super.super.super.super.free_fn = slog_dd_free;

    self->super.worker.thread_init = slog_worker_thread_init;
    self->super.worker.thread_deinit = slog_worker_thread_deinit;
    self->super.worker.disconnect = slog_dd_disconnect;
    self->super.worker.insert = slog_worker_insert;

    self->super.format_stats_instance = slog_dd_format_stats_instance;
    self->super.super.super.super.generate_persist_name = slog_dd_format_persist_name;
    //self->super.stats_source = SCS_SLOG;

    return (LogDriver *)self;
}

/*
static Plugin slog_dest_plugin =
{
  .type = LL_CONTEXT_DESTINATION,
  .name = "slog-dest",
  .parser = &slog_parser,
};*/
/*
gboolean slog_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, &slog_dest_plugin, 1);

  return TRUE;
}*/
/*
const ModuleInfo module_info =
{
  .canonical_name = "dummy",
  .version = SYSLOG_NG_VERSION,
  .description = "This is a dummy destination for syslog-ng.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = &dummy_plugin,
  .plugins_len = 1,
};*/
