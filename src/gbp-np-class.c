/*
 * Copyright (C) 2009 Alessandro Decina
 *
 * Authors:
 *   Alessandro Decina <alessandro.d@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "gbp-np-class.h"
#include <string.h>

GbpNPClass gbp_np_class;

typedef struct
{
  const char *name;
  NPInvokeFunctionPtr method;
} GbpNPClassMethod;

typedef enum
{
  PLAYBACK_CMD_STOP,
  PLAYBACK_CMD_PAUSE,
  PLAYBACK_CMD_START,
  PLAYBACK_CMD_QUIT,
} PlaybackCommandCode;

typedef struct
{
  GbpPlayer *player;
  PlaybackCommandCode code;
} PlaybackCommand;

static bool gbp_np_class_method_start (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result);
static bool gbp_np_class_method_stop (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result);
static bool gbp_np_class_method_pause (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result);
static bool gbp_np_class_method_set_error_handler (NPObject *obj,
    NPIdentifier name, const NPVariant *args, uint32_t argCount,
    NPVariant *result);
static bool gbp_np_class_method_set_state_handler (NPObject *obj,
    NPIdentifier name, const NPVariant *args, uint32_t argCount,
    NPVariant *result);

PlaybackCommand *playback_command_new (GbpPlayer *player, PlaybackCommandCode code);
void playback_command_free (PlaybackCommand *command);
void playback_command_push (GbpPlayer *player, PlaybackCommandCode code);
gpointer playback_thread_func (gpointer data);
void gbp_np_class_start_playback_thread ();
void gbp_np_class_stop_playback_thread ();

/* cached method ids, allocated by gbp_np_class_init and destroyed by
 * gbp_np_class_free */
static NPIdentifier *method_identifiers;
static guint methods_num;

GbpNPClassMethod gbp_np_class_methods[] = {
  {"start", gbp_np_class_method_start},
  {"stop", gbp_np_class_method_stop},
  {"pause", gbp_np_class_method_pause},
  {"setErrorHandler", gbp_np_class_method_set_error_handler},
  {"setStateHandler", gbp_np_class_method_set_state_handler},

  /* sentinel */
  {NULL, NULL}
};

static NPObject *
gbp_np_class_allocate (NPP npp, NPClass *aClass)
{
  GbpNPObject *obj = (GbpNPObject *) NPN_MemAlloc (sizeof (GbpNPObject));
  obj->instance = npp;

  return (NPObject *) obj;
}

static void
gbp_np_class_deallocate (NPObject *npobj)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  obj->instance = NULL;
  NPN_MemFree (obj);
}

static bool
gbp_np_class_has_method (NPObject *obj, NPIdentifier name)
{
  guint i;

  for (i = 0; i < methods_num; ++i) {
    if (name == method_identifiers[i])
      return TRUE;
  }

  return FALSE;
}

static bool
gbp_np_class_invoke (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  guint i;

  for (i = 0; i < methods_num; ++i) {
    if (name == method_identifiers[i]) {

      return gbp_np_class_methods[i].method(obj, name,
          args, argCount, result);
    }
  }

  NPN_SetException (obj, "No method with this name exists.");
  return FALSE;
}

static bool
gbp_np_class_has_property (NPObject *obj, NPIdentifier name)
{
  return FALSE;
}

static bool
gbp_np_class_get_property (NPObject *obj, NPIdentifier name, NPVariant *result)
{
  NPN_SetException (obj, "No property with this name exists.");
  return FALSE;
}

static bool
gbp_np_class_set_property (NPObject *obj, NPIdentifier name, const NPVariant *value)
{
  NPN_SetException (obj, "No property with this name exists.");
  return FALSE;
}

static bool
gbp_np_class_remove_property (NPObject *obj, NPIdentifier name)
{
  NPN_SetException (obj, "No property with this name exists.");
  return FALSE;
}

static bool
gbp_np_class_enumerate (NPObject *obj, NPIdentifier **value, uint32_t *count)
{
  return FALSE;
}

static bool
gbp_np_class_method_start (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  playback_command_push (data->player, PLAYBACK_CMD_START);

  VOID_TO_NPVARIANT (*result);
  return TRUE;
}

static bool
gbp_np_class_method_stop (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  playback_command_push (data->player, PLAYBACK_CMD_STOP);

  VOID_TO_NPVARIANT (*result);
  return TRUE;
}

static bool
gbp_np_class_method_pause (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  playback_command_push (data->player, PLAYBACK_CMD_PAUSE);

  VOID_TO_NPVARIANT (*result);
  return TRUE;
}

static bool
gbp_np_class_method_set_error_handler (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (argCount == 1, FALSE);
  g_return_val_if_fail (args[0].type == NPVariantType_Object, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  data->errorHandler = NPN_RetainObject(args[0].value.objectValue);

  VOID_TO_NPVARIANT (*result);
  return TRUE;
}

static bool
gbp_np_class_method_set_state_handler (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (argCount == 1, FALSE);
  g_return_val_if_fail (args[0].type == NPVariantType_Object, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  data->stateHandler = NPN_RetainObject(args[0].value.objectValue);

  VOID_TO_NPVARIANT (*result);
  return TRUE;

}

void
gbp_np_class_init ()
{
  int i;
  const char **method_names;

  NPClass *klass = (NPClass *) &gbp_np_class;

  /* only init once */
  g_return_if_fail (klass->structVersion == 0);

  klass->structVersion = NP_CLASS_STRUCT_VERSION;
  klass->allocate = gbp_np_class_allocate;
  klass->deallocate = gbp_np_class_deallocate;
  klass->hasMethod = gbp_np_class_has_method;
  klass->invoke = gbp_np_class_invoke;
  klass->hasProperty = gbp_np_class_has_property;
  klass->getProperty = gbp_np_class_get_property;
  klass->setProperty = gbp_np_class_set_property;
  klass->removeProperty = gbp_np_class_remove_property;
  klass->enumerate = gbp_np_class_enumerate;

  /* setup method identifiers */
  methods_num = \
      (sizeof (gbp_np_class_methods) / sizeof (GbpNPClassMethod)) - 1;

  /* alloc method_identifiers */
  method_identifiers = \
      (NPIdentifier *) NPN_MemAlloc (sizeof (NPIdentifier) * methods_num);

  method_names = (const char **) NPN_MemAlloc (sizeof (char *) * methods_num);
  for (i = 0; gbp_np_class_methods[i].name != NULL; ++i)
    method_names[i] = gbp_np_class_methods[i].name;

  /* fill method_identifiers */
  NPN_GetStringIdentifiers (method_names, methods_num, method_identifiers);

  NPN_MemFree (method_names);

  gbp_np_class_start_playback_thread ();
}

void
gbp_np_class_free ()
{
  NPClass *klass = (NPClass *) &gbp_np_class;

  g_return_if_fail (klass->structVersion != 0);

  gbp_np_class_stop_playback_thread ();

  NPN_MemFree (method_identifiers);

  memset (&gbp_np_class, 0, sizeof (GbpNPClass));

}

void
gbp_np_class_start_playback_thread ()
{
  g_return_if_fail (gbp_np_class.playback_thread == NULL);

  gbp_np_class.playback_queue = g_async_queue_new ();
  gbp_np_class.playback_thread = g_thread_create (playback_thread_func,
      NULL, TRUE, NULL);
}

void
gbp_np_class_stop_playback_thread ()
{
  g_return_if_fail (gbp_np_class.playback_thread != NULL);

  playback_command_push (NULL, PLAYBACK_CMD_QUIT);
  g_thread_join (gbp_np_class.playback_thread);
  gbp_np_class.playback_thread = NULL;
  g_async_queue_unref (gbp_np_class.playback_queue);
  gbp_np_class.playback_queue = NULL;
}

PlaybackCommand *
playback_command_new (GbpPlayer *player, PlaybackCommandCode code)
{
  PlaybackCommand *command = g_new0 (PlaybackCommand, 1);
  if (player)
    command->player = g_object_ref (player);
  command->code = code;

  return command;
}

void
playback_command_free (PlaybackCommand *command)
{
  g_return_if_fail (command != NULL);

  if (command->player)
    g_object_unref (command->player);
  g_free (command);
}

void
playback_command_push (GbpPlayer *player, PlaybackCommandCode code)
{
  PlaybackCommand *command = playback_command_new (player, code);
  g_async_queue_push (gbp_np_class.playback_queue, command);
}

gpointer
playback_thread_func (gpointer data)
{
  PlaybackCommand *command;
  gboolean exit = FALSE;

  while (exit == FALSE) {
    command = g_async_queue_pop (gbp_np_class.playback_queue);

    switch (command->code) {
      case PLAYBACK_CMD_STOP:
        gbp_player_stop (command->player);
        break;

      case PLAYBACK_CMD_PAUSE:
        gbp_player_pause (command->player);
        break;

      case PLAYBACK_CMD_START:
        gbp_player_start (command->player);
        break;

      case PLAYBACK_CMD_QUIT:
        exit = TRUE;
        break;

      default:
        g_warn_if_reached ();
    }

    playback_command_free (command);
    command = NULL;
  }

  return NULL;
}
