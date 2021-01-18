/*
    This file is part of darktable,
    copyright (c) 2019--2020 Diederik ter Rahe.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/darktable.h"
#include "common/ratings.h"
#include "common/colorlabels.h"
#include "bauhaus/bauhaus.h"
#include "control/conf.h"
#include "gui/gtk.h"
#include "gui/accelerators.h"
#include "common/file_location.h"
#include <fcntl.h>

DT_MODULE(1)

#ifdef HAVE_SDL

#include <SDL.h>

const char *name(dt_lib_module_t *self)
{
  return _("gamepad");
}

const char **views(dt_lib_module_t *self)
{
  static const char *v[] = {"*", NULL};
  return v;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_TOP_CENTER;
}

int expandable(dt_lib_module_t *self)
{
  return 0;
}

int position()
{
  return 1;
}

typedef struct gamepad_device
{
  SDL_GameController *controller;
} gamepad_device;

static gboolean poll_gamepad_devices(gpointer user_data)
{
  SDL_Event event;

  while(SDL_PollEvent(&event) > 0 )
  {
    switch(event.type)
    {
    case SDL_CONTROLLERBUTTONDOWN:
      dt_print(DT_DEBUG_INPUT, "SDL button down event time %d id %d button %hhd state %hhd\n", event.cbutton.timestamp, event.cbutton.which, event.cbutton.button, event.cbutton.state);
      dt_shortcut_key_down(2 + event.cbutton.which, event.cbutton.timestamp, event.cbutton.button, 0);
      break;
    case SDL_CONTROLLERBUTTONUP:
      dt_print(DT_DEBUG_INPUT, "SDL button up event time %d id %d button %hhd state %hhd\n", event.cbutton.timestamp, event.cbutton.which, event.cbutton.button, event.cbutton.state);
      dt_shortcut_key_up(2 + event.cbutton.which, event.cbutton.timestamp, event.cbutton.button, 0);
      break;
    case SDL_CONTROLLERAXISMOTION:
      dt_print(DT_DEBUG_INPUT, "SDL axis event type %d time %d id %d axis %hhd value %hd\n", event.caxis.type, event.caxis.timestamp, event.caxis.which, event.caxis.axis, event.caxis.value);
      break;
    case SDL_CONTROLLERDEVICEADDED:
      break;
    }
  }
  return G_SOURCE_CONTINUE;
}

void gamepad_open_devices(dt_lib_module_t *self)
{
  if(SDL_Init(SDL_INIT_GAMECONTROLLER))
  {
    fprintf(stderr, "[gamepad_open_devices] ERROR initialising SDL\n");
    return;
  }
  else
    dt_print(DT_DEBUG_INPUT, "[gamepad_open_devices] SDL initialized\n");

  for(int i = 0; i < SDL_NumJoysticks(); i++)
  {
    if(SDL_IsGameController(i))
    {
      SDL_GameController *controller = SDL_GameControllerOpen(i);

      if(!controller)
      {
        fprintf(stderr, "[gamepad_open_devices] ERROR opening game controller '%s'\n", SDL_GameControllerNameForIndex(i));
        continue;
      }
      else
      {
        fprintf(stderr, "[gamepad_open_devices] opened game controller '%s'\n", SDL_GameControllerNameForIndex(i));
      }

      gamepad_device *gamepad = (gamepad_device *)g_malloc0(sizeof(gamepad_device));

      gamepad->controller = controller;

      self->data = g_slist_append(self->data, gamepad);

      g_timeout_add(10, poll_gamepad_devices, gamepad);
    }
  }
}

void gamepad_device_free(gamepad_device *gamepad)
{
  g_source_remove_by_user_data(gamepad);

  SDL_GameControllerClose(gamepad->controller);

  g_free(gamepad);
}

void gamepad_close_devices(dt_lib_module_t *self)
{
  g_slist_free_full(self->data, (void (*)(void *))gamepad_device_free);
  self->data = NULL;

  SDL_Quit();
}

void gui_init(dt_lib_module_t *self)
{
  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_no_show_all(self->widget, TRUE);

  self->data = NULL;

  gamepad_open_devices(self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  gamepad_close_devices(self);
}

static gboolean callback_reopen_gamepad(GtkAccelGroup *accel_group,
                                        GObject *acceleratable, guint keyval,
                                        GdkModifierType modifier, gpointer data)
{
  gamepad_close_devices(data);

  gamepad_open_devices(data);

  dt_toast_log(_("Reopened all gamepad devices"));

  return TRUE;
}

void init_key_accels(dt_lib_module_t *self)
{
  dt_accel_register_lib(self, NC_("accel", "reopen gamepad"), GDK_KEY_M, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
}

void connect_key_accels(dt_lib_module_t *self)
{
  dt_accel_connect_lib(self, "reopen gamepad",
                     g_cclosure_new(G_CALLBACK(callback_reopen_gamepad), self, NULL));
}

#endif // HAVE_SDL

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
