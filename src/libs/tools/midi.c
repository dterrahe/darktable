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

char midi_devices_default[] = "portmidi,alsa,/dev/midi1,/dev/midi2,/dev/midi3,/dev/midi4";

DT_MODULE(1)

#ifdef HAVE_PORTMIDI

#include <portmidi.h>

const char *name(dt_lib_module_t *self)
{
  return _("midi");
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

typedef struct dt_midi_knob_t
{
  gint group;
  gint channel;
  gint key;

  dt_accel_t *accelerator;

  gint encoding;
#define MIDI_ABSOLUTE 0
  gboolean locked;
  float acceleration;
} dt_midi_knob_t;

typedef struct dt_midi_note_t
{
  gint group;
  gint channel;
  gint key;

  guint accelerator_key;
  GdkModifierType accelerator_mods;
} dt_midi_note_t;

typedef struct midi_device
{
  dt_input_device_t   id;
  const PmDeviceInfo *info;
  PortMidiStream     *portmidi_in;
  PortMidiStream     *portmidi_out;

  gboolean            syncing;
  gint                encoding;
  gint8               last_known[128];
  gint8               channel;
} midi_device;

const char *note_names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B", NULL };

gchar *key_to_string(guint key, guint mods, gboolean display)
{
  return g_strdup_printf(display ? "%s%d" : "Note %s%d", note_names[key % 12], key / 12 - 1);
}

gboolean string_to_key(gchar *string, guint *key, guint *mods)
{
  int octave = 0;
  char name[3];

  if(sscanf(string, "Note %2[ABCDEFG#]%d", name, &octave) == 2)
  {
    for(int note = 0; note_names[note]; note++)
    {
      if(!strcmp(name, note_names[note]))
      {
        *key = note + 12 * (octave + 1);
        return TRUE;
      }
    }
  }

  return FALSE;
}

gchar *move_to_string(guint move, gboolean display)
{
  return g_strdup_printf("CC%u", move);
}

gboolean string_to_move(gchar *string, guint *move)
{
  return sscanf(string, "CC%u", move) == 1;
}

const dt_input_driver_definition_t driver_definition
  = { key_to_string, string_to_key, move_to_string, string_to_move };

void midi_write(midi_device *midi, gint channel, gint type, gint key, gint velocity)
{
  if (midi->portmidi_out)
  {
    PmMessage message = Pm_Message((type << 4) + channel, key, velocity);
    PmError pmerror = Pm_WriteShort(midi->portmidi_out, 0, message);
    if (pmerror != pmNoError)
    {
      g_print("Portmidi error: %s\n", Pm_GetErrorText(pmerror));
    }
  }
}

gint interpret_move(dt_midi_knob_t *k, gint velocity)
{
  if(k)
  {
    switch(k->encoding)
    {
      case 127: // 2s Complement
        if(velocity < 65)
          return velocity;
        else
          return velocity - 128;
        break;
      case 63: // Offset
        return velocity - 64;
        break;
      case 33: // Sign
        if(velocity < 32)
          return velocity;
        else
          return 32 - velocity;
        break;
      case 15: // Offset 5 bit
        return velocity - 16;
        break;
      case 65: // Sign 6 bit (x-touch mini in MC mode)
        if(velocity < 64)
          return velocity;
        else
          return 64 - velocity;
        break;
      default:
        return 0;
        break;
    }
  }
  else
  {
    return 0;
  }
}

static gboolean poll_midi_devices(gpointer user_data)
{
  GSList *devices = (GSList *)((dt_lib_module_t *)user_data)->data;
  while(devices)
  {
    midi_device *midi = devices->data;

    PmEvent event;

    while(Pm_Poll(midi->portmidi_in) > 0 )
    {
      Pm_Read(midi->portmidi_in, &event, 1);

      int eventStatus = Pm_MessageStatus(event.message);
      int eventData1 = Pm_MessageData1(event.message);
      int eventData2 = Pm_MessageData2(event.message);

      int eventType = eventStatus >> 4;
      int eventChannel = eventStatus & 0x0F;

      midi->channel = eventChannel;

      if (eventType == 0x9 && // note on
          eventData2 == 0)
      {
        eventType = 0x8; // note off
      }

      switch (eventType)
      {
        case 0x9:  // note on
          dt_print(DT_DEBUG_INPUT, "Note On: Channel %d, Data1 %d\n", eventChannel, eventData1);

          dt_shortcut_key_down(midi->id, event.timestamp, eventData1, eventChannel);
          break;
        case 0x8:  // note off
          dt_print(DT_DEBUG_INPUT, "Note Off: Channel %d, Data1 %d\n", eventChannel, eventData1);

          dt_shortcut_key_up(midi->id, event.timestamp, eventData1, eventChannel);
          break;
        case 0xb:  // controllers, sustain
          dt_print(DT_DEBUG_INPUT, "Controller: Channel %d, Data1 %d, Data2 %d\n", eventChannel, eventData1, eventData2);

          // FIXME read all with same controller, aggregate (or just take last if absolute) and only at end send

// relative         dt_shortcut_move(midi->id, event.timestamp, eventData1, eventData2 < 65 ? eventData2 : eventData2 - 128);
          float new_position = dt_shortcut_move(midi->id, event.timestamp, eventData1, eventData2 - midi->last_known[eventData1]);

          if(strstr(midi->info->name, "X-TOUCH MINI"))
          {
            if(new_position >= 4)
              midi_write(midi, 0, 0xB, eventData1, 2); // fan
            else if(new_position >= 2)
              midi_write(midi, 0, 0xB, eventData1, 4); // trim
            else
              midi_write(midi, 0, 0xB, eventData1, 1); // pan
          }

          int rotor_position = 0;
          if(new_position >= 0)
          {
            new_position = fmodf(new_position, 2.0);
            if(new_position != 0.0)
            {
              if(new_position == 1.0)
                rotor_position = 127;
              else
              {
                rotor_position = 2.0 + new_position * 124.0; // 2-125
              }
            }
          }
          else
          {
            int c = - new_position - 1;
            rotor_position = c > 11 ? c+107 : c*127./12.+1.25;
          }

          midi->last_known[eventData1] = rotor_position;
          midi_write(midi, midi->channel, 0xB, eventData1, rotor_position);
          break;
        default:
          break;
      }
    }

    devices = devices->next;
  }

  return G_SOURCE_CONTINUE;
}

void midi_open_devices(dt_lib_module_t *self)
{
  if(Pm_Initialize())
  {
    fprintf(stderr, "[midi_open_devices] ERROR initialising PortMidi\n");
    return;
  }
  else
    dt_print(DT_DEBUG_INPUT, "[midi_open_devices] PortMidi initialized\n");

  dt_input_device_t id = dt_register_input_driver(&driver_definition);

  for(int i = 0; i < Pm_CountDevices() && i < 10; i++)
  {
    const PmDeviceInfo *info = Pm_GetDeviceInfo(i);

    if(info->input)
    {
      PortMidiStream *stream_in;
      PmError error = Pm_OpenInput(&stream_in, i, NULL, 1000, NULL, NULL);

      if(error < 0)
      {
        fprintf(stderr, "[midi_open_devices] ERROR opening midi device '%s' via '%s'\n", info->name, info->interf);
        continue;
      }
      else
      {
        fprintf(stderr, "[midi_open_devices] opened midi device '%s' via '%s'\n", info->name, info->interf);
      }

      midi_device *midi = (midi_device *)g_malloc0(sizeof(midi_device));

      midi->id = id++;
      midi->info = info;
      midi->portmidi_in = stream_in;

      for(int j = 0; j < Pm_CountDevices(); j++)
      {
        const PmDeviceInfo *infoOutput = Pm_GetDeviceInfo(j);

        if(infoOutput->output && !strcmp(info->name, infoOutput->name))
        {
          Pm_OpenOutput(&midi->portmidi_out, j, NULL, 1000, NULL, NULL, 0);
        }
      }

      self->data = g_slist_append(self->data, midi);
    }
  }

  if(self->data) g_timeout_add(10, poll_midi_devices, self);
}

void midi_device_free(midi_device *midi)
{
  Pm_Close(midi->portmidi_in);

  if (midi->portmidi_out)
  {
    Pm_Close(midi->portmidi_out);
  }

  g_free(midi);
}

void midi_close_devices(dt_lib_module_t *self)
{
  g_source_remove_by_user_data(self);

  g_slist_free_full(self->data, (void (*)(void *))midi_device_free);
  self->data = NULL;

  Pm_Terminate();
}

static void callback_image_changed(gpointer instance, gpointer data)
{
}

static void callback_view_changed(gpointer instance, dt_view_t *old_view, dt_view_t *new_view, gpointer data)
{
}

void gui_init(dt_lib_module_t *self)
{
  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_no_show_all(self->widget, TRUE);

  self->data = NULL;

  midi_open_devices(self);

  dt_control_signal_connect(darktable.signals, DT_SIGNAL_VIEWMANAGER_VIEW_CHANGED,
                            G_CALLBACK(callback_view_changed), self);

  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_IMAGE_CHANGED,
                            G_CALLBACK(callback_image_changed), self);

  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_HISTORY_CHANGE,
                            G_CALLBACK(callback_image_changed), self);
}

void gui_cleanup(dt_lib_module_t *self)
{
 dt_control_signal_disconnect(darktable.signals,
                               G_CALLBACK(callback_view_changed), self);

  dt_control_signal_disconnect(darktable.signals,
                               G_CALLBACK(callback_image_changed), self);

  dt_control_signal_disconnect(darktable.signals,
                               G_CALLBACK(callback_image_changed), self);

  midi_close_devices(self);
}

static gboolean callback_reopen_midi(GtkAccelGroup *accel_group,
                                     GObject *acceleratable, guint keyval,
                                     GdkModifierType modifier, gpointer data)
{
  midi_close_devices(data);

  midi_open_devices(data);

  dt_toast_log(_("Reopened all midi devices"));

  return TRUE;
}

void init_key_accels(dt_lib_module_t *self)
{
  dt_accel_register_lib(self, NC_("accel", "reopen midi"), GDK_KEY_M, GDK_CONTROL_MASK);
}

void connect_key_accels(dt_lib_module_t *self)
{
  dt_accel_connect_lib(self, "reopen midi",
                     g_cclosure_new(G_CALLBACK(callback_reopen_midi), self, NULL));
}

#endif // HAVE_PORTMIDI

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
