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
  PmDeviceID      id;
  PortMidiStream *portmidi_in;
  PortMidiStream *portmidi_out;

  gboolean        config_loaded;

  gboolean        syncing;

  gint            group;
  gint            num_columns;
  gint            group_switch_key;
  gint            group_key_light;
  gint            rating_key_light;
  gint            reset_knob_key;
  gint            first_knob_key;
  gint            num_rotators;
  gint8           last_known[128];

  gint            LED_ring_behavior_channel;
  gint            LED_ring_behavior_off;
  gint            LED_ring_behavior_pan;
  gint            LED_ring_behavior_fan;
  gint            LED_ring_behavior_trim;
} midi_device;
/*
void midi_config_save(midi_device *midi)
{
  FILE *f = 0;

  gchar datadir[PATH_MAX] = { 0 };
  gchar midipath[PATH_MAX] = { 0 };

  dt_loc_get_user_config_dir(datadir, sizeof(datadir));
  snprintf(midipath, sizeof(midipath), "%s/midirc-%s", datadir, midi->model_name);

  f = g_fopen(midipath, "w");
  if(!f) return;

  g_fprintf(f, "num_columns=%d\n", midi->num_columns);
  g_fprintf(f, "group_switch_key=%d\n", midi->group_switch_key);
  g_fprintf(f, "group_key_light=%d\n", midi->group_key_light);
  g_fprintf(f, "rating_key_light=%d\n", midi->rating_key_light);
  g_fprintf(f, "reset_knob_key=%d\n", midi->reset_knob_key);
  g_fprintf(f, "first_knob_key=%d\n", midi->first_knob_key);
  g_fprintf(f, "num_rotators=%d\n", midi->num_rotators);
  g_fprintf(f, "LED_ring_behavior_channel=%d\n", midi->LED_ring_behavior_channel);
  g_fprintf(f, "LED_ring_behavior_off=%d\n", midi->LED_ring_behavior_off);
  g_fprintf(f, "LED_ring_behavior_pan=%d\n", midi->LED_ring_behavior_pan);
  g_fprintf(f, "LED_ring_behavior_fan=%d\n", midi->LED_ring_behavior_fan);
  g_fprintf(f, "LED_ring_behavior_trim=%d\n", midi->LED_ring_behavior_trim);

  g_fprintf(f, "\ngroup;channel;key;path;encoding;accel\n");

  GSList *l = midi->mapping_list;
  while (l)
  {
    dt_midi_knob_t *k = (dt_midi_knob_t *)l->data;

    gchar *spath = g_strndup( k->accelerator->path,strlen(k->accelerator->path)-strlen("/dynamic") );

    g_fprintf(f,"%d;%d;%d;%s;%d;%.4f\n",
                k->group, k->channel, k->key, spath, k->encoding, k->acceleration);

    g_free(spath);

    l = g_slist_next(l);
  }

  g_fprintf(f, "\ngroup,channel,key,path\n");

  l = midi->note_list;
  while (l)
  {
    dt_midi_note_t *n = (dt_midi_note_t *)l->data;
    g_fprintf(f,"%d;%d;%d;%s\n",
                n->group, n->channel, n->key,
                gtk_accelerator_name(n->accelerator_key,n->accelerator_mods));

    l = g_slist_next(l);
  }

  fclose(f);
}

void midi_config_load(midi_device *midi)
{
  midi->config_loaded = TRUE;

  FILE *f = 0;

  int read = 0;

  gchar datadir[PATH_MAX] = { 0 };
  gchar midipath[PATH_MAX] = { 0 };

  dt_loc_get_user_config_dir(datadir, sizeof(datadir));
  snprintf(midipath, sizeof(midipath), "%s/midirc-%s", datadir, midi->model_name);

  f = g_fopen(midipath, "rb");

  dt_loc_get_datadir(datadir, sizeof(datadir));
  snprintf(midipath, sizeof(midipath), "%s/midi/midirc-%s", datadir, midi->model_name);

  while (!f && strlen(midipath)>strlen(datadir))
  {
    f = g_fopen(midipath, "rb");
    if (strrchr(midipath,' ') != NULL)
      *(strrchr(midipath,' ')) = 0;
    else
      *midipath = 0;
  }

  if(!f) return;

  char buffer[200];

  while(fgets(buffer, 100, f))
  {
    if (sscanf(buffer, "num_columns=%d\n", &midi->num_columns) == 1) continue;
    if (sscanf(buffer, "group_switch_key=%d\n", &midi->group_switch_key) == 1) continue;
    if (sscanf(buffer, "group_key_light=%d\n", &midi->group_key_light) == 1) continue;
    if (sscanf(buffer, "rating_key_light=%d\n", &midi->rating_key_light) == 1) continue;
    if (sscanf(buffer, "reset_knob_key=%d\n", &midi->reset_knob_key) == 1) continue;
    if (sscanf(buffer, "first_knob_key=%d\n", &midi->first_knob_key) == 1) continue;
    if (sscanf(buffer, "num_rotators=%d\n", &midi->num_rotators) == 1) continue;
    if (sscanf(buffer, "LED_ring_behavior_channel=%d\n", &midi->LED_ring_behavior_channel) == 1) continue;
    if (sscanf(buffer, "LED_ring_behavior_off=%d\n", &midi->LED_ring_behavior_off) == 1) continue;
    if (sscanf(buffer, "LED_ring_behavior_pan=%d\n", &midi->LED_ring_behavior_pan) == 1) continue;
    if (sscanf(buffer, "LED_ring_behavior_fan=%d\n", &midi->LED_ring_behavior_fan) == 1) continue;
    if (sscanf(buffer, "LED_ring_behavior_trim=%d\n", &midi->LED_ring_behavior_trim) == 1) continue;

    gint group, channel, key, encoding;
    char accelpath[200];
    float acceleration;

    read = sscanf(buffer, "%d;%d;%d;%[^;];%d;%f\n",
                  &group, &channel, &key, accelpath, &encoding, &acceleration);
    if(read == 6)
    {
      g_strlcat(accelpath,"/dynamic",200);
      GList *al = darktable.control->accelerator_list;
      dt_accel_t *da;
      while(al)
      {
        da = (dt_accel_t *)al->data;
        if (!g_strcmp0(da->path, accelpath))
          break;

        al = g_list_next(al);
      }
      if (al)
      {
        dt_midi_knob_t *k = (dt_midi_knob_t *)g_malloc(sizeof(dt_midi_knob_t));

        k->group = group;
        k->channel = channel;
        k->key = key;
        k->accelerator = da;
        k->encoding = encoding;
        k->acceleration = acceleration;

        midi->mapping_list = g_slist_append(midi->mapping_list, k);
      }
      continue;
    }

    read = sscanf(buffer, "%d;%d;%d;%[^\r\n]\r\n",
                  &group, &channel, &key, accelpath);

    if (read == 4)
    {
      guint accelerator_key;
      GdkModifierType accelerator_mods;
      gtk_accelerator_parse(accelpath, &accelerator_key, &accelerator_mods);
      if (accelerator_key)
      {
        dt_midi_note_t *n = (dt_midi_note_t *)g_malloc(sizeof(dt_midi_note_t));

        n->group = group;
        n->channel = channel;
        n->key = key;
        n->accelerator_key = accelerator_key;
        n->accelerator_mods = accelerator_mods;

        midi->note_list = g_slist_append(midi->note_list, n);
      }
      continue;
    }
  }

  fclose(f);
}
*/
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
/*
void refresh_sliders_to_device(midi_device *midi)
{
  if (!midi->config_loaded)
  {
    midi_config_load(midi);
  }

  midi->syncing = FALSE;

  const dt_view_t *cv = dt_view_manager_get_current_view(darktable.view_manager);
  if(cv->view((dt_view_t *)cv) == DT_VIEW_DARKROOM)
  {
    GSList *l = midi->mapping_list;
    while(l)
    {
      dt_midi_knob_t *k = (dt_midi_knob_t *)l->data;

      if (k->encoding == MIDI_ABSOLUTE)
      {
        if (k != midi->stored_knob)
        {
          k->locked = FALSE;
        }

        GtkWidget *w = k->accelerator->closure ? GTK_WIDGET(k->accelerator->closure->data) : NULL;
        if (k->group == midi->group && w)
        {
          const gboolean is_slider = DT_BAUHAUS_WIDGET(w)->type == DT_BAUHAUS_SLIDER;

          float min = is_slider ? DT_BAUHAUS_WIDGET(w)->data.slider.min : 0;
          float max = is_slider ? DT_BAUHAUS_WIDGET(w)->data.slider.max : dt_bauhaus_combobox_length(w);
          float c   = is_slider ? dt_bauhaus_slider_get(w) : dt_bauhaus_combobox_get(w);

          int velocity = is_slider ? round((c-min)/(max-min)*127) : (c>11?c+107:c*127./12.+1.25);
          if(is_slider && DT_BAUHAUS_WIDGET(w)->data.slider.factor < 0) velocity = 127 - velocity;

          if (velocity != midi->last_known[k->key])
          {
            midi_write(midi, k->channel, 0xB, k->key, velocity);
            midi->last_known[k->key] = velocity;
          }

          // For Behringer; set pattern of rotator lights
          if (k->key >= midi->first_knob_key &&
              k->key <= midi->num_rotators &&
              midi->LED_ring_behavior_channel >= 0)
          {
            if (min == -max)
              midi_write(midi, midi->LED_ring_behavior_channel, 0xB, k->key, midi->LED_ring_behavior_trim);
            else if (min == 0 && (max == 1 || max == 100))
              midi_write(midi, midi->LED_ring_behavior_channel, 0xB, k->key, midi->LED_ring_behavior_fan);
            else
              midi_write(midi, midi->LED_ring_behavior_channel, 0xB, k->key, midi->LED_ring_behavior_pan);
          }
        }
      }
      l = g_slist_next(l);
    }

    int image_id = darktable.develop->image_storage.id;

    if (midi->rating_key_light != -1)
    {
      int on_lights = 0;

      if (image_id != -1)
      {
        if (midi->group == 1)
        {
          int rating = dt_ratings_get(image_id);
          if (rating == 6) // if rating=reject, show x0x0x pattern
          {
            on_lights = 1+4+16;
          }
          else
          {
            on_lights = 31 >> (5-rating);
          }
        }
        else if (midi->group == 2)
        {
          on_lights = dt_colorlabels_get_labels(image_id);
        }
      }

      for (int light = 0; light < midi->num_columns; light++)
      {
        midi_write(midi, 0, 0x9, light + midi->rating_key_light, on_lights & 1);
        on_lights >>= 1;
      }
    }

    midi_write(midi, 0, 0x9, midi->group + midi->group_key_light - 1, 1);
  }
}

void refresh_all_devices(gpointer data)
{
  GSList *l = (GSList *)(((dt_lib_module_t *)data)->data);
  while (l)
  {
    refresh_sliders_to_device( (midi_device *)l->data);
    l = g_slist_next(l);
  }
}

static void callback_slider_changed(GtkWidget *w, gpointer data)
{
  if (knob_config_mode)
  {
    mapping_widget = w;

    dt_control_hinter_message
            (darktable.control, (""));

    dt_toast_log(_("slowly move midi controller down to connect"));
  }
  else
  {
    refresh_all_devices(data);
  }

  return;
}

static void callback_image_changed(gpointer instance, gpointer data)
{
  refresh_all_devices(data);
}

static gpointer dynamic_callback = (void *)-1;

static void callback_view_changed(gpointer instance, dt_view_t *old_view, dt_view_t *new_view, gpointer data)
{
  if (new_view->view(new_view) == DT_VIEW_DARKROOM)
  {
    GList *l = darktable.control->accelerator_list;
    while(l)
    {
      dt_accel_t *da = (dt_accel_t *)l->data;

      if (da->closure && !strcmp(strrchr(da->path, '/'), "/dynamic"))
      {
        dynamic_callback = ((struct _GCClosure*)da->closure)->callback;
        break;
      }

      l = g_list_next(l);
    }
    while(l)
    {
      dt_accel_t *da = (dt_accel_t *)l->data;

      if (da->closure && ((struct _GCClosure*)da->closure)->callback == dynamic_callback)
      {
        g_signal_connect(G_OBJECT(da->closure->data), "value-changed", G_CALLBACK(callback_slider_changed), data);
      }

      l = g_list_next(l);
    }

    dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_IMAGE_CHANGED,
                              G_CALLBACK(callback_image_changed), data);

    dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_HISTORY_CHANGE,
                              G_CALLBACK(callback_image_changed), data);
  }
  else
  {
    dt_control_signal_disconnect(darktable.signals,
                                G_CALLBACK(callback_image_changed), data);
  }

  refresh_all_devices(data);
}
*/
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
/*
//  Currently just aggregates one channel/key combination
//  and sends when changing to different key. This might still
//  cause flooding if multiple knobs are turned simultaneously
//  and alternating codes are received.
//  To deal with this would require maintaining a list of currently
//  changed keys and send all at end. Probably not worth extra complexity,
//  Since it would still mean sending multiple updates in one iteration
//  which could cause flooding anyway.
void aggregate_and_set_slider(midi_device *midi,
                              gint channel, gint key, gint velocity)
{
  const dt_view_t *cv = dt_view_manager_get_current_view(darktable.view_manager);
  if(cv->view((dt_view_t *)cv) != DT_VIEW_DARKROOM) return;

  if ((channel == midi->stored_channel) && (key == midi->stored_key))
  {
    if (midi->stored_knob == NULL || midi->stored_knob->encoding == MIDI_ABSOLUTE)
        midi->accum = velocity; // override; just use last one
    else
        midi->accum += interpret_move(midi->stored_knob, velocity);
  }
  else
  {
    if (midi->stored_channel != -1)
    {
      if (midi->stored_knob)
      {
        GtkWidget *w = midi->stored_knob->accelerator->closure
                     ? midi->stored_knob->accelerator->closure->data
                     : NULL;

        if (w)
        {
          const gboolean is_slider = DT_BAUHAUS_WIDGET(w)->type == DT_BAUHAUS_SLIDER;

          float v = is_slider ? dt_bauhaus_slider_get(w) : dt_bauhaus_combobox_get(w);
          float s = is_slider ? dt_bauhaus_slider_get_step(w) : 1;

          int move = midi->accum;

          if (midi->stored_knob->encoding == MIDI_ABSOLUTE)
          {
            midi->last_known[midi->stored_key] = midi->accum;

            float wmin = is_slider ? DT_BAUHAUS_WIDGET(w)->data.slider.min : 0;
            float wmax = is_slider ? DT_BAUHAUS_WIDGET(w)->data.slider.max : dt_bauhaus_combobox_length(w);

            int location = is_slider ? round((v-wmin)/(wmax-wmin)*127) : (v>11?v+107:v*127./12.+1.25);
            if(is_slider && DT_BAUHAUS_WIDGET(w)->data.slider.factor < 0) location = 127 - location;
            move -= location;

            // attempt to limit number of steps if acceleration too high to avoid flipping back and forth between ends of range
            int direct_steps = fabsf((v - (wmin + midi->accum * (wmax-wmin)/127))/(s * midi->stored_knob->acceleration))+1;
            move = MIN(direct_steps,MAX(-direct_steps,move));

            if (midi->stored_knob->locked ||
                abs(move) <= 1)
            {
              midi->stored_knob->locked = TRUE;

              if (midi->syncing)
              {
                dt_toast_log((">%s/%s<"),
                              DT_BAUHAUS_WIDGET(w)->module->name(),
                              DT_BAUHAUS_WIDGET(w)->label);

                midi->syncing = FALSE;
              }
            }
            else
            {
              if (midi->syncing)
              {
                gchar *left_text  = g_strnfill(MAX(1, move)-1,'<');
                gchar *right_text = g_strnfill(MAX(1, -move)-1,'>');

                dt_toast_log(("%s %s / %s %s"),
                              left_text, DT_BAUHAUS_WIDGET(w)->module->name(),
                              DT_BAUHAUS_WIDGET(w)->label, right_text);

                g_free(left_text);
                g_free(right_text);
              }

              // if one knob is out of sync, all on same device may need syncing
              refresh_sliders_to_device(midi);
              midi->syncing = TRUE;
              move = 0;
            }
          }
          if (move != 0)
          {
            if (knob_config_mode)
            {
              // configure acceleration setting
              if (move > 0)
              {
                midi->stored_knob->acceleration *= 2;
              }
              else
              {
                midi->stored_knob->acceleration /= 2;
              }

              dt_toast_log(_("knob acceleration %.2f"), midi->stored_knob->acceleration);

              midi_config_save(midi);

              knob_config_mode = FALSE;

              channel = -1;
              key = -1;
            }
            else
            {
              if(is_slider)
                dt_bauhaus_slider_set(w, v + s * midi->stored_knob->acceleration * move);
              else
                dt_bauhaus_combobox_set(w, MAX(0, v + s * move));
              dt_accel_widget_toast(w);
            }
          }
        }
      }
    }

    midi->stored_knob = NULL;

    if (channel != -1)
    {
      if (mapping_widget)
      {
        knob_config_mode = FALSE;

        // link knob to widget and set encoding type

        if (midi->mapping_channel == -1)
        {
          midi->mapping_channel  = channel;
          midi->mapping_key      = key;
          midi->mapping_velocity = velocity;
        }
        else
        {
          if ((velocity != 1) &&
              (channel == midi->mapping_channel) && (key == midi->mapping_key) &&
              ((midi->mapping_velocity == velocity) || (midi->mapping_velocity - velocity == 1)) )
          {
            // store new mapping in table, overriding existing

            GList *al = darktable.control->accelerator_list;
            dt_accel_t *da = NULL ;
            while(al)
            {
              da = (dt_accel_t *)al->data;
              if (da->closure
                  && ((struct _GCClosure*)da->closure)->callback == dynamic_callback
                  && da->closure->data == mapping_widget)
                break;

              al = g_list_next(al);
            }

            dt_toast_log(_("mapped to %s/%s"),
                           DT_BAUHAUS_WIDGET(mapping_widget)->module->name(),
                           DT_BAUHAUS_WIDGET(mapping_widget)->label);

            dt_midi_knob_t *new_knob = NULL;

            GSList *l = midi->mapping_list;
            while(l)
            {
              dt_midi_knob_t *d = (dt_midi_knob_t *)l->data;
              if ((d->group > midi->group) |
                  ((d->group == midi->group) &&
                   ((d->channel > channel) |
                    ((d->channel == channel) && (d->key >= key)))))
              {
                if ((d->group == midi->group) &&
                    (d->channel == channel) &&
                    (d->key == key))
                {
                  new_knob = d;
                }
                else
                {
                  new_knob = (dt_midi_knob_t *)g_malloc(sizeof(dt_midi_knob_t));
                  midi->mapping_list = g_slist_insert_before(midi->mapping_list, l, new_knob);
                }
                break;
              }
              l = g_slist_next(l);
            }
            if (!new_knob)
            {
              new_knob = (dt_midi_knob_t *)g_malloc(sizeof(dt_midi_knob_t));
              midi->mapping_list = g_slist_append(midi->mapping_list, new_knob);
            }
            new_knob->group = midi->group;
            new_knob->channel = channel;
            new_knob->key = key;
            new_knob->acceleration = 1;
            new_knob->accelerator = da;
            if (midi->mapping_velocity == 0 || midi->mapping_velocity - velocity == 1)
            {
              new_knob->encoding = MIDI_ABSOLUTE;
              new_knob->locked = FALSE;
            }
            else
            {
              new_knob->encoding = velocity | 1; // force last bit to 1
            }

            midi_config_save(midi);
          }

          dt_control_hinter_message (darktable.control, "");

          midi->mapping_channel = -1;
          mapping_widget = NULL;
        }

        channel = -1;
        key = -1;
      }
      else
      {
        GSList *l = midi->mapping_list;
        while(l)
        {
          dt_midi_knob_t *d = (dt_midi_knob_t *)l->data;
          if ((d->group > midi->group) |
              ((d->group == midi->group) &&
                ((d->channel > channel) |
                ((d->channel == channel) && (d->key >= key)))))
          {
            if ((d->group == midi->group) &&
                (d->channel == channel) &&
                (d->key == key))
            {
              midi->stored_knob = d;
            }
            break;
          }
          l = g_slist_next(l);
        }

        if (midi->stored_knob == NULL)
        {
          dt_toast_log(_("knob %d on channel %d not mapped in group %d"),
                         key, channel, midi->group);
        }
        else if (midi->stored_knob->encoding == MIDI_ABSOLUTE)
        {
          midi->accum = velocity;
        }
        else
        {
          midi->accum = interpret_move(midi->stored_knob, velocity);
        }
      }
    }

    midi->stored_channel = channel;
    midi->stored_key = key;
  }
}
*/
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

      if (eventType == 0x9 && // note on
          eventData2 == 0)
      {
        eventType = 0x8; // note off
      }

      switch (eventType)
      {
        case 0x9:  // note on
          dt_print(DT_DEBUG_INPUT, "Note On: Channel %d, Data1 %d\n", eventChannel, eventData1);

          dt_shortcut_key_down(1, event.timestamp, eventData1, eventChannel);
          break;

        case 0x8:  // note off
          dt_print(DT_DEBUG_INPUT, "Note Off: Channel %d, Data1 %d\n", eventChannel, eventData1);

          dt_shortcut_key_up(1, event.timestamp, eventData1, eventChannel);
          break;

        case 0xb:  // controllers, sustain
          dt_print(DT_DEBUG_INPUT, "Controller: Channel %d, Data1 %d, Data2 %d\n", eventChannel, eventData1, eventData2);
  //        aggregate_and_set_slider(midi, eventChannel, eventData1, eventData2);
          break;

        default:
          break;
      }
    }

  //  aggregate_and_set_slider(midi, -1, -1, -1);

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

  for(int i = 0; i < Pm_CountDevices(); i++)
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

      midi->id = i;
      midi->portmidi_in = stream_in;

      if(info->output)
      {
        Pm_OpenOutput(&midi->portmidi_out, i, NULL, 1000, NULL, NULL, 0);
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

void gui_init(dt_lib_module_t *self)
{
  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_no_show_all(self->widget, TRUE);

  self->data = NULL;

  midi_open_devices(self);
}

void gui_cleanup(dt_lib_module_t *self)
{
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
