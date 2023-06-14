/*
 * Copyright (c) 2002-2016 Balabit
 * Copyright (c) 2016 Viktor Juhasz <viktor.juhasz@balabit.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include <math.h>

#include "diskq.h"
#include "diskq-config.h"

#include "driver.h"
#include "messages.h"
#include "logqueue-disk.h"
#include "logqueue-disk-reliable.h"
#include "logqueue-disk-non-reliable.h"
#include "persist-state.h"

#define DISKQ_PLUGIN_NAME "diskq"

struct _DiskQDestPlugin
{
  LogDriverPlugin super;
  DiskQueueOptions options;
};

static gboolean
log_queue_disk_is_file_in_directory(const gchar *file, const gchar *directory)
{
  gchar *basedir = g_path_get_dirname(file);
  const gboolean result = (0 == strcmp(basedir, directory));

  g_free(basedir);
  return result;
}

static LogQueue *
_acquire_queue(LogDestDriver *dd, const gchar *persist_name)
{
  DiskQDestPlugin *self = log_driver_get_plugin(&dd->super, DiskQDestPlugin, DISKQ_PLUGIN_NAME);
  GlobalConfig *cfg = log_pipe_get_config(&dd->super.super);
  LogQueue *queue = NULL;
  gchar *qfile_name;
  gboolean success;

  if (persist_name)
    queue = cfg_persist_config_fetch(cfg, persist_name);

  if (queue)
    {
      log_queue_unref(queue);
      queue = NULL;
    }

  if (self->options.reliable)
    queue = log_queue_disk_reliable_new(&self->options, persist_name);
  else
    queue = log_queue_disk_non_reliable_new(&self->options, persist_name);
  log_queue_set_throttle(queue, dd->throttle);

  qfile_name = persist_state_lookup_string(cfg->state, persist_name, NULL, NULL);

  if (qfile_name && !log_queue_disk_is_file_in_directory(qfile_name, self->options.dir))
    {
      msg_warning("The disk buffer directory has changed in the configuration, but the disk queue file cannot be moved",
                  evt_tag_str("qfile", qfile_name),
                  evt_tag_str("dir", self->options.dir));
    }

  success = log_queue_disk_load_queue(queue, qfile_name);
  if (!success)
    {
      if (qfile_name && log_queue_disk_load_queue(queue, NULL))
        {
          msg_error("Error opening disk-queue file, a new one started",
                    evt_tag_str("old_filename", qfile_name),
                    evt_tag_str("new_filename", log_queue_disk_get_filename(queue)));
        }
      else
        {
          g_free(qfile_name);
          msg_error("Error initializing log queue");
          return NULL;
        }
    }

  g_free(qfile_name);

  if (persist_name)
    {
      /* save the queue file name to permanent state */
      qfile_name = (gchar *) log_queue_disk_get_filename(queue);
      if (qfile_name)
        {
          persist_state_alloc_string(cfg->state, persist_name, qfile_name, -1);
        }
    }

  return queue;
}

void
_release_queue(LogDestDriver *dd, LogQueue *queue)
{
  GlobalConfig *cfg = log_pipe_get_config(&dd->super.super);
  gboolean persistent;

  log_queue_disk_save_queue(queue, &persistent);
  if (queue->persist_name)
    {
      cfg_persist_config_add(cfg, queue->persist_name, queue, (GDestroyNotify) log_queue_unref, FALSE);
    }
  else
    {
      log_queue_unref(queue);
    }
}

static gboolean
_is_truncate_size_ratio_set_explicitly(DiskQDestPlugin *self, LogDestDriver *dd)
{
  GlobalConfig *cfg = log_pipe_get_config(&dd->super.super);
  return fabs(self->options.truncate_size_ratio - (-1)) >= DBL_EPSILON
         || disk_queue_config_is_truncate_size_ratio_set_explicitly(cfg);
}

static gboolean
_is_prealloc_set_explicitly(DiskQDestPlugin *self, LogDestDriver *dd)
{
  GlobalConfig *cfg = log_pipe_get_config(&dd->super.super);
  return self->options.prealloc != -1 || disk_queue_config_is_prealloc_set_explicitly(cfg);
}

static gboolean
_set_truncate_size_ratio_and_prealloc(DiskQDestPlugin *self, LogDestDriver *dd)
{
  GlobalConfig *cfg = log_pipe_get_config(&dd->super.super);

  gdouble truncate_size_ratio = self->options.truncate_size_ratio;
  if (truncate_size_ratio < 0)
    {
      if (cfg_is_config_version_older(cfg, VERSION_VALUE_3_33))
        {
          msg_warning_once("WARNING: the truncation of the disk-buffer files is changed starting with "
                           VERSION_3_33 ". You are using an older config version and your config does not "
                           "set the truncate-size-ratio() option. We will not use the new truncating logic "
                           "with this config for compatibility.");
          /* Handle it as truncate-size-ratio(0) was set explicitly by the user. */
          disk_queue_options_set_truncate_size_ratio(&self->options, 0);
          truncate_size_ratio = 0;
        }
      else
        {
          truncate_size_ratio = disk_queue_config_get_truncate_size_ratio(cfg);
        }
    }

  gboolean prealloc = self->options.prealloc;
  if (prealloc < 0)
    {
      prealloc = disk_queue_config_get_prealloc(cfg);
    }

  if (!(prealloc && truncate_size_ratio < 1))
    {
      self->options.truncate_size_ratio = truncate_size_ratio;
      self->options.prealloc = prealloc;
      return TRUE;
    }

  if (_is_truncate_size_ratio_set_explicitly(self, dd) && _is_prealloc_set_explicitly(self, dd))
    {
      msg_error("Cannot enable preallocation and truncation at the same time. "
                "Please disable either the truncation (truncate-size-ratio(1)) or the preallocation (prealloc(no))",
                log_pipe_location_tag(&dd->super.super));
      return FALSE;
    }

  if (_is_truncate_size_ratio_set_explicitly(self, dd))
    {
      msg_warning("Cannot enable preallocation and truncation at the same time. "
                  "Preallocation disabled",
                  log_pipe_location_tag(&dd->super.super));
      self->options.truncate_size_ratio = truncate_size_ratio;
      self->options.prealloc = FALSE;
      return TRUE;
    }

  if (_is_prealloc_set_explicitly(self, dd))
    {
      msg_warning("Cannot enable preallocation and truncation at the same time. "
                  "Truncation disabled",
                  log_pipe_location_tag(&dd->super.super));
      self->options.truncate_size_ratio = 1;
      self->options.prealloc = prealloc;
      return TRUE;
    }

  /*
   * This means that our hard coded default values enabled preallocation and truncation at the same time,
   * which is a coding error.
   */
  g_assert_not_reached();
}

static gboolean
_attach(LogDriverPlugin *s, LogDriver *d)
{
  DiskQDestPlugin *self = (DiskQDestPlugin *) s;
  LogDestDriver *dd = (LogDestDriver *) d;
  GlobalConfig *cfg = log_pipe_get_config(&d->super);

  if (self->options.disk_buf_size < MIN_DISK_BUF_SIZE && self->options.disk_buf_size > 0)
    {
      msg_warning("The value of 'disk_buf_size()' is too low, setting to the smallest acceptable value",
                  evt_tag_long("min_space", MIN_DISK_BUF_SIZE),
                  log_pipe_location_tag(&dd->super.super));
      self->options.disk_buf_size = MIN_DISK_BUF_SIZE;
    }

  if (self->options.mem_buf_length < 0)
    self->options.mem_buf_length = dd->log_fifo_size;
  if (self->options.mem_buf_length < 0)
    self->options.mem_buf_length = cfg->log_fifo_size;
  if (self->options.qout_size < 0)
    self->options.qout_size = 1000;

  if (!_set_truncate_size_ratio_and_prealloc(self, dd))
    return FALSE;

  dd->acquire_queue = _acquire_queue;
  dd->release_queue = _release_queue;
  return TRUE;
}

static void
_free(LogDriverPlugin *s)
{
  DiskQDestPlugin *self = (DiskQDestPlugin *)s;
  disk_queue_options_destroy(&self->options);
  log_driver_plugin_free_method(s);
}

DiskQueueOptions *diskq_get_options(DiskQDestPlugin *self)
{
  return &self->options;
}

DiskQDestPlugin *
diskq_dest_plugin_new(void)
{
  DiskQDestPlugin *self = g_new0(DiskQDestPlugin, 1);

  log_driver_plugin_init_instance(&self->super, DISKQ_PLUGIN_NAME);
  disk_queue_options_set_default_options(&self->options);
  self->super.attach = _attach;
  self->super.free_fn = _free;
  return self;
}
