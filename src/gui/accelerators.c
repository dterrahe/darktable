/*
    This file is part of darktable,
    Copyright (C) 2011-2020 darktable developers.

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

#include "gui/accelerators.h"
#include "common/darktable.h"
#include "common/debug.h"
#include "common/file_location.h"
#include "common/utility.h"
#include "control/control.h"
#include "develop/blend.h"

#include "bauhaus/bauhaus.h"

#include <assert.h>
#include <gtk/gtk.h>

typedef struct dt_shortcut_t
{
  dt_input_device_t key_device;
  guint key;
  guint mods;
  guint button;
  enum
  {
    DT_SHORTCUT_CLICK_NONE,
    DT_SHORTCUT_CLICK_SINGLE,
    DT_SHORTCUT_CLICK_DOUBLE,
    DT_SHORTCUT_CLICK_TRIPLE,
    DT_SHORTCUT_CLICK_LONG = 4
  } click;
  dt_input_device_t move_device;
  guint move;
  enum
  {
    DT_SHORTCUT_DIR_NONE,
    DT_SHORTCUT_DIR_UP,
    DT_SHORTCUT_DIR_DOWN,
  } direction;
  dt_view_type_flags_t views;

  enum // these should be defined in widget type block as strings and here indexed
  {
    DT_SHORTCUT_EFFECT_CLOSURE,
    DT_SHORTCUT_EFFECT_UP,
    DT_SHORTCUT_EFFECT_DOWN,
    DT_SHORTCUT_EFFECT_NEXT,
    DT_SHORTCUT_EFFECT_PREVIOUS,
    DT_SHORTCUT_EFFECT_VALUE,
    DT_SHORTCUT_EFFECT_RESET,
    DT_SHORTCUT_EFFECT_END,
    DT_SHORTCUT_EFFECT_BEGIN,
  } effect;
  enum // these should be defined in widget type block as strings and here indexed
  {
    DT_SHORTCUT_SUB_MIN,
    DT_SHORTCUT_SUB_MAX,
    DT_SHORTCUT_SUB_MINEST,
    DT_SHORTCUT_SUB_MAXEST,
    DT_SHORTCUT_SUB_NODE1, // contrast equaliser for example. Node value or node x-axis position can be moved
    DT_SHORTCUT_SUB_NODE2,
    DT_SHORTCUT_SUB_NODE3,
    DT_SHORTCUT_SUB_NODE4,
    DT_SHORTCUT_SUB_NODE5,
    DT_SHORTCUT_SUB_NODE6,
    DT_SHORTCUT_SUB_NODE7,
    DT_SHORTCUT_SUB_NODE8,
  } sub; // this should be index into widget discription structure.
  float speed;
  int instance; // 0 is from prefs, >0 counting from first, <0 counting from last
  dt_action_t *action;
} dt_shortcut_t;

typedef enum dt_shortcut_move_t
{
  DT_SHORTCUT_MOVE_NONE,
  DT_SHORTCUT_MOVE_SCROLL,
  DT_SHORTCUT_MOVE_HORIZONTAL,
  DT_SHORTCUT_MOVE_VERTICAL,
  DT_SHORTCUT_MOVE_DIAGONAL,
  DT_SHORTCUT_MOVE_SKEW,
  DT_SHORTCUT_MOVE_LEFTRIGHT,
  DT_SHORTCUT_MOVE_UPDOWN,
  DT_SHORTCUT_MOVE_PGUPDOWN,
} dt_shortcut_move_t;

typedef struct dt_device_key_t
{
  dt_input_device_t key_device;
  guint key;
} dt_device_key_t;

#define DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE 0

const char *move_string[] = { "", N_("scroll"), N_("horizontal"), N_("vertical"), N_("diagonal"), N_("skew"),
                                  N_("leftright"), N_("updown"), N_("pgupdown"), NULL };
const char *click_string[] = { "", N_("single"), N_("double"), N_("triple"), NULL };


gint shortcut_compare_func(gconstpointer shortcut_a, gconstpointer shortcut_b, gpointer user_data)
{
  const dt_shortcut_t *a = (const dt_shortcut_t *)shortcut_a;
  const dt_shortcut_t *b = (const dt_shortcut_t *)shortcut_b;

  if(a->key_device != b->key_device)
    return a->key_device - b->key_device;
  if(a->key != b->key)
    return a->key - b->key;
  else if(a->mods != b->mods)
    return a->mods - b->mods;
  else if(a->click != b->click)
    return a->click - b->click;
  else if(a->button != b->button)
    return a->button - b->button;
  if(a->move_device != b->move_device)
    return a->move_device - b->move_device;
  else if(a->move != b->move)
    return a->move - b->move;
  else if(a->views != b->views)
  // FIXME: global (views = 0) equates to any view.
  // If multiple views (for libs) -> multiple nodes need to be inserted in tree
  // Maybe add "DT_VIEW_MULTI" bit so we know this is part of a set (so others also need updating/removing)
  // don't include this bit in comparison.
  // actually, when part of multi group, put whole original group in views field (shifted to left by enough bits)
  // so if views = 1+2+8 (1011), create 3 shortcuts (1011 0000 0001, 1011 0000 0010, 1011 0000 1000) to easier find
  // the others that need updating/deleting. But ignore in comparison.
    return a->views - b->views;
  else return 0;
};

void _print_action(FILE *f, dt_action_t *action)
{
  if(action)
  {
    _print_action(f, action->owner);
    fprintf(f, "%s/", action->label); // action->label_translated
  }
}

void _dump_actions(dt_action_t *action)
{
  while(action)
  {
    _print_action(stderr, action->owner);
    fprintf(stderr, "%s\n", action->label); // action->label_translated
    if(action->type <= DT_ACTION_TYPE_SECTION)
      _dump_actions(action->target);
    action = action->next;
  }
}

dt_input_device_t dt_register_input_driver(const dt_input_driver_definition_t *driver)
{
  dt_input_device_t id = 10;

  GSList *device = darktable.control->input_devices;
  while(device)
  {
    if(device->data == driver) return id;
    device = device->next;
    id += 10;
  }

  darktable.control->input_devices = g_slist_append(darktable.control->input_devices, (gpointer)driver);

  return id;
}

#define DT_MOVE_NAME -1
gchar *shortcut_key_move_name(dt_input_device_t id, guint key_or_move, guint mods, gboolean display)
{
  if(id == 0)
  {
    if(mods == DT_MOVE_NAME)
      return g_strdup(_(move_string[key_or_move]));
    else
      return !key_or_move ? g_strdup(display ? "" : "None")
                          : ( display ? gtk_accelerator_get_label(key_or_move, mods)
                                      : gtk_accelerator_name(key_or_move, mods) );
  }
  else
  {
    GSList *device = darktable.control->input_devices;
    while(device)
    {
      if((id -= 10) < 10)
      {
        dt_input_driver_definition_t *driver = device->data;
        gchar *without_device
          = mods == DT_MOVE_NAME
          ? driver->move_to_string(key_or_move, display)
          : driver->key_to_string(key_or_move, mods, display);

        if(display || id == 0) return without_device;

        gchar *with_device = g_strdup_printf("%hhu:%s", id, without_device);
        g_free(without_device);
        return with_device;
      }
      device = device->next;
    }

    return g_strdup(_("Unknown device"));
  }
}

void dt_shortcuts_save(const gchar *file_name)
{
  FILE *f = g_fopen(file_name, "wb");
  if(f)
  {
    for(GSequenceIter *i = g_sequence_get_begin_iter(darktable.control->shortcuts);
        !g_sequence_iter_is_end(i);
        i = g_sequence_iter_next(i))
    {
      dt_shortcut_t *s = g_sequence_get(i);

      gchar *key_name = shortcut_key_move_name(s->key_device, s->key, s->mods, FALSE);
      fprintf(f, "%s", key_name);
      g_free(key_name);
      if(s->button & (1 << GDK_BUTTON_PRIMARY  )) fprintf(f, ";%s", "left");
      if(s->button & (1 << GDK_BUTTON_MIDDLE   )) fprintf(f, ";%s", "middle");
      if(s->button & (1 << GDK_BUTTON_SECONDARY)) fprintf(f, ";%s", "right");
      guint clean_click = s->click & ~DT_SHORTCUT_CLICK_LONG;
      if(clean_click > DT_SHORTCUT_CLICK_SINGLE) fprintf(f, ";%s", click_string[clean_click]);
      if(s->click >= DT_SHORTCUT_CLICK_LONG) fprintf(f, ";%s", _("long"));

      if(s->move_device || s->move)
      {
        gchar *move_name = shortcut_key_move_name(s->move_device, s->move, DT_MOVE_NAME, FALSE);
        fprintf(f, ";%s", move_name);
        g_free(move_name);
      }

      fprintf(f, "=");

      _print_action(f, s->action->owner);
      fprintf(f, "%s", s->action->label);
      if(s->instance == -1) fprintf(f, ";last");
      if(s->instance == +1) fprintf(f, ";first");
      if(abs(s->instance) > 1) fprintf(f, ";%+d", s->instance);
      if(s->speed != 1.0) fprintf(f, ";*%.f", s->speed);

      fprintf(f, "\n");
    }

    fclose(f);
  }
}

void dt_shortcuts_load(const gchar *file_name)
{
  FILE *f = g_fopen(file_name, "rb");
  if(f)
  {
    // start with an empty shortcuts collection
    if(darktable.control->shortcuts) g_sequence_free(darktable.control->shortcuts);
    darktable.control->shortcuts = g_sequence_new(g_free);

    while(!feof(f))
    {
      char line[1024];
      char *read = fgets(line, sizeof(line), f);
      if(read > 0)
      {
        line[strcspn(line, "\r\n")] = '\0';

        char *act_start = strchr(line, '=');
        if(!act_start)
        {
          fprintf(stderr, "[dt_shortcuts_load] line '%s' is not an assignment\n", line);
          continue;
        }
        char *act_end = act_start + strcspn(act_start, ";");

        dt_shortcut_t s = { .speed = 1 };

        char *token = strtok(line, "=;/");
        if(strcmp(token, "None"))
        {
          s.click = DT_SHORTCUT_CLICK_SINGLE;
          gtk_accelerator_parse(token, &s.key, &s.mods);
          if(!s.key)
          {
            dt_input_device_t id = 10;
            if(strlen(token) > 2 && token[1] == ':')
            {
              id += token[0] - '0';
              token += 2;
            }
            GSList *device = darktable.control->input_devices;
            while(device)
            {
              dt_input_driver_definition_t *driver = device->data;
              if(driver->string_to_key(token, &s.key, &s.mods))
              {
                s.key_device = id;
                break;
              }
              id += 10;
              device = device->next;
            }
            if(!device)
            {
              fprintf(stderr, "[dt_shortcuts_load] '%s' is not a valid key\n", line);
              continue;
            }
          }
        }

        while((token = strtok(NULL, "=;/")) && token < act_start)
        {
          if(!strcmp(token, "left"  )) { s.button |= (1 << GDK_BUTTON_PRIMARY  ); continue; }
          if(!strcmp(token, "middle")) { s.button |= (1 << GDK_BUTTON_MIDDLE   ); continue; }
          if(!strcmp(token, "right" )) { s.button |= (1 << GDK_BUTTON_SECONDARY); continue; }

          int click = 0;
          while(click_string[++click])
            if(!strcmp(token, click_string[click]))
            {
              s.click = click;
              break;
            }
          if(click_string[click]) continue;
          if(!strcmp(token, "long")) { s.click |= DT_SHORTCUT_CLICK_LONG; continue; }

          int move = 0;
          while(move_string[++move])
            if(!strcmp(token, move_string[move]))
            {
              s.move = move;
              break;
            }
          if(move_string[move]) continue;

          dt_input_device_t id = 10;
          if(strlen(token) > 2 && token[1] == ':')
          {
            id += token[0] - '0';
            token += 2;
          }
          GSList *device = darktable.control->input_devices;
          while(device)
          {
            dt_input_driver_definition_t *driver = device->data;
            if(driver->string_to_move(token, &s.move))
            {
              s.move_device = id;
              break;
            }
            id += 10;
            device = device->next;
          }
          if(device) continue;

          fprintf(stderr, "[dt_shortcuts_load] token '%s' not recognised\n", token);
        }

        // find action and also views along the way
        dt_action_t *ac = darktable.control->actions;
        while(token && token < act_end && ac)
        {
          if(!strcmp(token, ac->label))
          {
            s.action = ac;
            ac = ac->type <= DT_ACTION_TYPE_SECTION ? ac->target : NULL;
            // FIXME determine views
            token = strtok(NULL, ";/");
          }
          else
          {
            ac = ac->next;
          }
        }

        if((token && token < act_end) || ac)
        {
          for(token = ++act_start; token < act_end; token++) if(!*token) *token = '/';
          fprintf(stderr, "[dt_shortcuts_load] action path '%s' not found\n", act_start);
          continue;
        }

        while(token)
        {
          if(!strcmp(token, "first")) s.instance =  1; else
          if(!strcmp(token, "last" )) s.instance = -1; else
          if(*token == '+' || *token == '-') sscanf(token, "%d", &s.instance); else
          if(*token == '*') sscanf(token, "*%f", &s.speed); else
          fprintf(stderr, "[dt_shortcuts_load] token '%s' not recognised\n", token);

          token = strtok(NULL, ";");
        }

        dt_shortcut_t *new_shortcut = g_malloc(sizeof(dt_shortcut_t));
        *new_shortcut = s;
        g_sequence_append(darktable.control->shortcuts, new_shortcut);
      }
    }
    fclose(f);

    g_sequence_sort(darktable.control->shortcuts, shortcut_compare_func, NULL);
  }
}

static dt_shortcut_t bsc = { 0 };  // building shortcut
static GSList *pressed_keys = NULL; // list of currently pressed keys
static guint pressed_button = 0;
static guint last_time = 0;

static void define_new_mapping()
{
  dt_shortcut_t *s = calloc(sizeof(dt_shortcut_t), 1);
  *s = bsc;
  dt_action_t *action = g_hash_table_lookup(darktable.control->widgets, darktable.control->mapping_widget);
  s->action = action;
  if(action->target != darktable.control->mapping_widget)
  {
    // find relative module instance
    dt_action_t *owner = action->owner;
    while(owner && owner->type != DT_ACTION_TYPE_IOP) owner = owner->owner;
    if(owner)
    {
      // calculate location of module struct from owner, which is a pointer to actions field
      // FIXME this will be better after making module_t a derived class from actions_owner
      dt_iop_module_so_t *module = (dt_iop_module_so_t *)owner;
      module -= (dt_iop_module_so_t *)&module->actions - module;

      int current_instance = 0;
      for(GList *iop_mods = darktable.develop->iop;
          iop_mods;
          iop_mods = g_list_next(iop_mods))
      {
        dt_iop_module_t *mod = (dt_iop_module_t *)iop_mods->data;

        if(mod->so == module && mod->iop_order != INT_MAX)
        {
          current_instance++;

          if(!s->instance)
          {
            for(GSList *w = mod->widget_list; w; w = w->next)
            {
              if(((dt_action_widget_t *)w->data)->widget == darktable.control->mapping_widget)
              {
                s->instance = current_instance;
                break;
              }
            }
          }
        }
      }

      if(current_instance - s->instance < s->instance) s->instance -= current_instance + 1;
    }
  }

  GSequenceIter *existing = g_sequence_lookup(darktable.control->shortcuts, s, shortcut_compare_func, 0 /*view*/);

  if(!existing)
    g_sequence_insert_sorted(darktable.control->shortcuts, s, shortcut_compare_func, 0 /*view*/);
  else
  {
    // FIXME ask confirmation
    dt_shortcut_t *e = g_sequence_get(existing);
    fprintf(stderr,_("replacing mapping to %s, instance %d\n"),
                  e->action->label_translated, e->instance);

    g_sequence_set(existing, s);
  }

  gchar *key_name = shortcut_key_move_name(s->key_device, s->key, s->mods, TRUE);
  gchar *move_name = shortcut_key_move_name(s->move_device, s->move, DT_MOVE_NAME, TRUE);
  dt_control_log(_("key %s, move %s, button %d, click %d mapped to %s, instance %d\n"),
                 key_name, move_name, s->button, s->click,
                 action->label_translated, s->instance);
  fprintf(stderr,_("key %s, move %s, button %d, click %d mapped to %s, instance %d\n"),
                 key_name ? key_name : _("none"), move_name ? move_name : _("none"), s->button, s->click,
                 action->label_translated, s->instance);
  g_free(key_name);
  g_free(move_name);

  darktable.control->mapping_widget = NULL;

  gchar datadir[PATH_MAX] = { 0 };
  dt_loc_get_user_config_dir(datadir, sizeof(datadir));
  gchar *file_name = g_strdup_printf("%s/shortcutsrc", datadir);
  dt_shortcuts_save(file_name);
  g_free(file_name);
}

gboolean combobox_idle_value_changed(gpointer widget)
{
  g_signal_emit_by_name(G_OBJECT(widget), "value-changed");

  while(g_idle_remove_by_data(widget));

  return FALSE;
}

static float process_mapping(float move_size)
{
  float return_value = NAN;

  GSequenceIter *existing = g_sequence_lookup(darktable.control->shortcuts, &bsc, shortcut_compare_func, 0 /*view*/);
  if(existing)
  {
    dt_shortcut_t *bac = g_sequence_get(existing);

    GtkWidget *widget = NULL;
    if(bac->instance)
    {
      // find relative module instance
      dt_action_t *owner = bac->action->owner;
      while(owner && owner->type != DT_ACTION_TYPE_IOP) owner = owner->owner;
      if(owner)
      {
        // calculate location of module struct from owner, which is a pointer to actions field
        // FIXME this will be better after making module_t a derived class from actions_owner
        dt_iop_module_so_t *module = (dt_iop_module_so_t *)owner;
        module -= (dt_iop_module_so_t *)&module->actions - module;

        dt_iop_module_t *mod = NULL;
        int current_instance = abs(bac->instance);

        for(GList *iop_mods = bac->instance > 0
                            ? darktable.develop->iop
                            : g_list_last(darktable.develop->iop);
            iop_mods;
            iop_mods = bac->instance > 0
                     ? g_list_next(iop_mods)
                     : g_list_previous(iop_mods))
        {
          mod = (dt_iop_module_t *)iop_mods->data;

          if(mod->so == module &&
             mod->iop_order != INT_MAX &&
             !--current_instance) break;
        }

        if(mod)
        {
          for(GSList *w = mod->widget_list; w; w = w->next)
          {
            dt_action_widget_t *referral = w->data;
            if(referral->action == bac->action)
            {
              widget = referral->widget;
              break;
            }
          }
        }
      }
    }
    else
      widget = bac->action->target;

    if(DTGTK_IS_TOGGLEBUTTON(widget))
    {
      GdkEvent *event = gdk_event_new(GDK_BUTTON_PRESS);
      event->button.state = 0; // FIXME support ctrl-press
      event->button.button = 1;
      event->button.window = gtk_widget_get_window(widget);
      g_object_ref(event->button.window);

      gtk_widget_event(widget, event);

      gdk_event_free(event);
    }
    else if(GTK_IS_BUTTON(widget)) // test DTGTK_IS_TOGGLEBUTTON first, because it is also a button
      gtk_button_clicked(GTK_BUTTON(widget));
    else if(DT_IS_BAUHAUS_WIDGET(widget))
    {
      dt_bauhaus_widget_t *bhw = (dt_bauhaus_widget_t *)DT_BAUHAUS_WIDGET(widget);

      if(bhw->type == DT_BAUHAUS_SLIDER)
      {
        dt_bauhaus_slider_data_t *d = &bhw->data.slider;
        if(move_size != 0 && gtk_widget_get_visible(widget) && gtk_widget_get_visible(gtk_widget_get_parent(widget)))
        {
          float value = dt_bauhaus_slider_get(widget);
          float step = dt_bauhaus_slider_get_step(widget);
          float multiplier = dt_accel_get_slider_scale_multiplier() * bac->speed;

          const float min_visible = powf(10.0f, -dt_bauhaus_slider_get_digits(widget));
          if(fabsf(step*multiplier) < min_visible)
            multiplier = min_visible / fabsf(step);

          d->is_dragging = 1;
          dt_bauhaus_slider_set(widget, value + move_size * step * multiplier);
          d->is_dragging = 0;

          dt_accel_widget_toast(widget);
        }

        return_value = d->pos +
                     ( d->min == -d->max ? 2 :
                     ( d->min == 0 && (d->max == 1 || d->max == 100) ? 4 : 0 ));
      }
      else
      {
        int value = dt_bauhaus_combobox_get(widget);

        if(move_size != 0 && gtk_widget_get_visible(widget) && gtk_widget_get_visible(gtk_widget_get_parent(widget)))
        {
          value = CLAMP(value + move_size, 0, dt_bauhaus_combobox_length(widget) - 1);

          ++darktable.gui->reset;
          dt_bauhaus_combobox_set(widget, value);
          --darktable.gui->reset;

          g_idle_add(combobox_idle_value_changed, widget);

          dt_accel_widget_toast(widget);
        }

        return_value = - 1 - value;
      }

    }
  }

  return return_value;
}

static void process_each_mapping(void *device_key_ptr, void *move_size_ptr)
{
  dt_device_key_t *device_key = device_key_ptr;
  bsc.key_device = device_key->key_device;
  bsc.key = device_key->key;

  process_mapping(*(double *)move_size_ptr);
}

gint cmp_key(gconstpointer a, gconstpointer b)
{
  const dt_device_key_t *key_a = a;
  const dt_device_key_t *key_b = b;
  return key_a->key_device != key_b->key_device || key_a->key != key_b->key;
}

float dt_shortcut_move(dt_input_device_t id, guint time, guint move, double size)
{
  bsc.move_device = id;
  bsc.move = move;
  bsc.speed = 1.0;

  float return_value = 0;

  if(darktable.control->mapping_widget)
  {
    define_new_mapping();
  }
  else
  {
    if(pressed_keys)
      g_slist_foreach(pressed_keys, process_each_mapping, &size);
    else
      return_value = process_mapping(size);
  }

  bsc.move_device = 0;
  bsc.move = DT_SHORTCUT_MOVE_NONE;

  return return_value; // FIXME when requesting value, if any keys pressed, going to get wrong values. Maybe do an (extra) call for bsc.key = 0 and move = 0 to retrieve position to return
}

static guint press_timeout_source = 0;

static gboolean _key_up_delayed(gpointer do_key)
{
  if(do_key) dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, 0, DT_SHORTCUT_MOVE_NONE, 1);

  if(!pressed_keys)
  {
    gdk_seat_ungrab(gdk_display_get_default_seat(gdk_display_get_default()));

    bsc.key_device = DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE;
    bsc.key = 0;
    bsc.click = DT_SHORTCUT_CLICK_NONE;
    bsc.mods = 0;
  }

  press_timeout_source = 0;
  return FALSE;
}

static guint click_timeout_source = 0;

static gboolean _button_release_delayed(gpointer user_data)
{
  dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, 0, DT_SHORTCUT_MOVE_NONE, 1);

  bsc.click = DT_SHORTCUT_CLICK_NONE;
  bsc.button = pressed_button;

  click_timeout_source = 0;
  return FALSE;
}

void dt_shortcut_key_down(dt_input_device_t id, guint time, guint key, guint mods)
{
  dt_device_key_t this_key = { id, key };
  if(!g_slist_find_custom(pressed_keys, &this_key, cmp_key))
  {
    if(press_timeout_source)
    {
      g_source_remove(press_timeout_source);
      press_timeout_source = 0;
    }

    int delay = 0;
    g_object_get(gtk_settings_get_default(), "gtk-double-click-time", &delay, NULL);

    if(!pressed_keys)
    {
      // FIXME if mods not coming from event (midi/gamepad) poll them here. mods == DT_MISSING_MODS
      bsc.mods = mods;
      if(id == bsc.key_device && key == bsc.key &&
         time < last_time + delay && bsc.click < DT_SHORTCUT_CLICK_TRIPLE)
        bsc.click++;
      else
        bsc.click = DT_SHORTCUT_CLICK_SINGLE;

      GdkCursor *cursor = gdk_cursor_new_from_name(gdk_display_get_default(), "all-scroll");
      gdk_seat_grab(gdk_display_get_default_seat(gdk_display_get_default()),
                    gtk_widget_get_window(dt_ui_main_window(darktable.gui->ui)),
                    GDK_SEAT_CAPABILITY_ALL_POINTING, FALSE, cursor,
                    NULL, NULL, NULL);
      g_object_unref(cursor);
    }

    last_time = time;
    bsc.key_device = id;
    bsc.key = key;
    bsc.button = pressed_button = 0;
    bsc.instance = 0;

    dt_device_key_t *new_key = calloc(1, sizeof(dt_device_key_t));
    *new_key = this_key;
    pressed_keys = g_slist_prepend(pressed_keys, new_key);
  }
  // key hold (CTRL-W for example) should fire without key being released if shortcut is marked as "key hold" (or something)??
  // otherwise only fire when key is released (because we are expecting scroll or something)
}

void dt_shortcut_key_up(dt_input_device_t id, guint time, guint key, guint mods)
{
  dt_device_key_t this_key = { id, key };

  GSList *stored_key = g_slist_find_custom(pressed_keys, &this_key, cmp_key);
  if(stored_key)
  {
    g_free(stored_key->data);
    pressed_keys = g_slist_delete_link(pressed_keys, stored_key);

    if(!pressed_keys)
    {
      if(bsc.key_device == id && bsc.key == key)
      {
        int delay = 0;
        g_object_get(gtk_settings_get_default(), "gtk-double-click-time", &delay, NULL);

        guint passed_time = time - last_time;
        if(passed_time < delay && bsc.click < DT_SHORTCUT_CLICK_TRIPLE)
          press_timeout_source = g_timeout_add(delay - passed_time, _key_up_delayed, GINT_TO_POINTER(TRUE));
        else
        {
          if(passed_time > delay) bsc.click |= DT_SHORTCUT_CLICK_LONG;
          _key_up_delayed(GINT_TO_POINTER(passed_time < 2 * delay)); // call immediately
        }
      }
      else
      {
        _key_up_delayed(NULL);
      }
    }
  }
  else
  {
    fprintf(stderr, "[shortcut_dispatcher] released key wasn't stored\n");
  }
}

gboolean dt_shortcut_dispatcher(GtkWidget *w, GdkEvent *event, gpointer user_data)
{
  static gdouble move_start_x = 0;
  static gdouble move_start_y = 0;

//  dt_print(DT_DEBUG_INPUT, "  [shortcut_dispatcher] %d\n", event->type);

  if(pressed_keys == NULL && event->type != GDK_KEY_PRESS) return FALSE;

  switch(event->type)
  {
  case GDK_KEY_PRESS:
    if(event->key.is_modifier) return FALSE;

    dt_shortcut_key_down(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->key.time,
                         event->key.keyval, event->key.state & gtk_accelerator_get_default_mod_mask());

    break;
  case GDK_KEY_RELEASE:
    if(event->key.is_modifier) return FALSE;

    dt_shortcut_key_up(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->key.time,
                       event->key.keyval, event->key.state & gtk_accelerator_get_default_mod_mask());

    break;
  case GDK_GRAB_BROKEN:
    if(event->grab_broken.implicit) break;
  case GDK_WINDOW_STATE:
    event->focus_change.in = FALSE; // fall through to GDK_FOCUS_CHANGE
  case GDK_FOCUS_CHANGE: // dialog boxes and switch to other app release grab
    if(!event->focus_change.in)
    {
      gdk_seat_ungrab(gdk_display_get_default_seat(gdk_display_get_default()));
      g_slist_free_full(pressed_keys, g_free);
      pressed_keys = NULL;
      bsc.click = DT_SHORTCUT_CLICK_NONE;
    }
    break;
  case GDK_SCROLL:
    {
      int delta_y;
      dt_gui_get_scroll_unit_delta((GdkEventScroll *)event, &delta_y);
      dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->scroll.time, DT_SHORTCUT_MOVE_SCROLL, - delta_y);
    }
    break;
  case GDK_MOTION_NOTIFY:
    if(bsc.move == DT_SHORTCUT_MOVE_NONE)
    {
      move_start_x = event->motion.x;
      move_start_y = event->motion.y;
      bsc.move = DT_SHORTCUT_MOVE_HORIZONTAL; // set fake direction so the start position doesn't keep resetting
      break;
    }

    gdouble x_move = event->motion.x - move_start_x;
    gdouble y_move = event->motion.y - move_start_y;
    const gdouble step_size = 10; // FIXME configurable, x & y separately

    gdouble angle = x_move / (0.001 + y_move);

    gdouble size = trunc(x_move / step_size);
    if(size != 0 && fabs(angle) >= 2)
    {
      move_start_x += size * step_size;
      move_start_y = event->motion.y;
      dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->motion.time, DT_SHORTCUT_MOVE_HORIZONTAL, size);
    }
    else
    {
      size = - trunc(y_move / step_size);
      if(size != 0)
      {
        move_start_y -= size * step_size;
        if(fabs(angle) < .5)
        {
          move_start_x = event->motion.x;
          dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->motion.time, DT_SHORTCUT_MOVE_VERTICAL, size);
        }
        else
        {
          move_start_x -= size * step_size * angle;
          dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->motion.time,
                           angle < 0 ? DT_SHORTCUT_MOVE_SKEW : DT_SHORTCUT_MOVE_DIAGONAL, size);
        }
      }
    }
    break;
  case GDK_BUTTON_PRESS:
    pressed_button |= 1 << event->button.button;
    bsc.button = pressed_button;
    bsc.click = DT_SHORTCUT_CLICK_SINGLE;
    bsc.move = DT_SHORTCUT_MOVE_NONE;
    last_time = event->button.time;
    if(click_timeout_source)
    {
      g_source_remove(click_timeout_source);
      click_timeout_source = 0;
    }
    break;
  case GDK_DOUBLE_BUTTON_PRESS:
    bsc.click = DT_SHORTCUT_CLICK_DOUBLE;
    break;
  case GDK_TRIPLE_BUTTON_PRESS:
    bsc.click = DT_SHORTCUT_CLICK_TRIPLE;
    break;
  case GDK_BUTTON_RELEASE:
    // FIXME; check if there's a shortcut defined for double/triple (could be fallback?); if not -> no delay
    // maybe even action on PRESS rather than RELEASE
    // FIXME be careful!!; we seem to be receiving presses and releases twice!?!
    pressed_button &= ~(1 << event->button.button);

    int delay = 0;
    g_object_get(gtk_settings_get_default(), "gtk-double-click-time", &delay, NULL);

    guint passed_time = event->button.time - last_time;
    if(passed_time < delay && bsc.click < DT_SHORTCUT_CLICK_TRIPLE)
    {
      if(!click_timeout_source)
        click_timeout_source = g_timeout_add(delay - passed_time, _button_release_delayed, NULL);
    }
    else
    {
      if(passed_time > delay)
        bsc.click |= DT_SHORTCUT_CLICK_LONG;
      if(passed_time < 2 * delay)
        _button_release_delayed(NULL); // call immediately
    }
    break;
  default:
    break;
  }

  return FALSE; // FIXME is return type used? doesn't seem so (maybe because of grab)
}

static gboolean _shortcut_tooltip_callback(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode,
                                           GtkTooltip *tooltip, gpointer user_data)
{
  gchar hint[1024];
  int length = 0;

  dt_action_t *action = g_hash_table_lookup(darktable.control->widgets, widget);
  for(GSequenceIter *iter = g_sequence_get_begin_iter(darktable.control->shortcuts);
      !g_sequence_iter_is_end(iter);
      iter = g_sequence_iter_next(iter))
  {
    dt_shortcut_t *s = g_sequence_get(iter);
    if(s->action == action)
    {
#define add_hint(format, ...) length += length >= sizeof(hint) ? 0 : snprintf(hint + length, sizeof(hint) - length, format, ##__VA_ARGS__)

      gchar *key_name = shortcut_key_move_name(s->key_device, s->key, s->mods, TRUE);
      gchar *move_name = shortcut_key_move_name(s->move_device, s->move, DT_MOVE_NAME, TRUE);
      if(*key_name)
      {
        add_hint("\n%s: %s", _("shortcut"), key_name);
      }
      else
      {
        add_hint("\n%s: %s", _("move"), move_name);
      }

      if(s->button) add_hint(", ");
      if(s->button & (1 << GDK_BUTTON_PRIMARY  )) add_hint(" %s", _("left"));
      if(s->button & (1 << GDK_BUTTON_MIDDLE   )) add_hint(" %s", _("middle"));
      if(s->button & (1 << GDK_BUTTON_SECONDARY)) add_hint(" %s", _("right"));

      guint clean_click = s->click & ~DT_SHORTCUT_CLICK_LONG;
      if(clean_click > DT_SHORTCUT_CLICK_SINGLE) add_hint(" %s", _(click_string[clean_click]));
      if(s->click >= DT_SHORTCUT_CLICK_LONG) add_hint(" %s", _("long"));
      if(s->button)
        add_hint(" %s", _("click"));
      else if(s->click > DT_SHORTCUT_CLICK_SINGLE)
        add_hint(" %s", _("press"));

      if(*move_name && *key_name)
      {
        add_hint(", %s", move_name);
      }
      g_free(key_name);
      g_free(move_name);

      if(s->instance == 1) add_hint(", %s", _("first instance"));
      else
      if(s->instance == -1) add_hint(", %s", _("last instance"));
      else
      if(s->instance != 0) add_hint(", %s %+d", _("relative instance"), s->instance);

      if(s->speed != 1.0) add_hint(_(", %s *%g"), _("speed"), s->speed);

#undef add_hint
    }
  }

  if(length)
  {
    gchar *original_markup = gtk_widget_get_tooltip_markup(widget);
    gchar *hint_escaped = g_markup_escape_text(hint, -1);
    gchar *markup_text = g_strdup_printf("%s<span style='italic' foreground='red'>%s</span>", original_markup, hint_escaped);
    gtk_tooltip_set_markup(tooltip, markup_text);
    g_free(original_markup);
    g_free(hint_escaped);
    g_free(markup_text);

    return TRUE;
  }

  return FALSE;
}

static void _remove_widget_from_hashtable(GtkWidget *widget, gpointer user_data)
{
  dt_action_t *action = g_hash_table_lookup(darktable.control->widgets, widget);
  if(action && action->target == widget)
  {
    action->target = NULL;
    g_hash_table_remove(darktable.control->widgets, widget);
  }
}

dt_action_t *dt_action_locate(dt_action_t *owner, gchar **path)
{
  if(!owner) return NULL;

  gchar *clean_path = NULL;

  dt_action_t *action = owner->target;
  while(*path)
  {
    if(!clean_path) clean_path = g_strdelimit(g_strdup(*path), "=,/.", '-');

    if(!action)
    {
      dt_action_t *new_action = calloc(1, sizeof(dt_action_t));
      new_action->label = clean_path;
      new_action->label_translated = g_strdup(Q_(*path));
      new_action->type = DT_ACTION_TYPE_SECTION;
      new_action->owner = owner;
      new_action->next = owner->target;
      owner->target = new_action;
      owner = new_action;
      action = NULL;
      path++;
      clean_path = NULL; // now owned by action
    }
    else if(!strcmp(action->label, clean_path))
    {
      owner = action;
      action = action->target;
      path++;
      g_free(clean_path);
      clean_path = NULL;
    }
    else
    {
      action = action->next;
    }
  }

  return owner;
}

dt_action_t *dt_action_define(dt_action_t *owner, const gchar *path, gboolean local, guint accel_key, GdkModifierType mods, GtkWidget *widget)
{
  // add to module_so actions list
  // split on `; find any sections or if not found, create (at start)
  gchar **split_path = g_strsplit(path, "`", 6);
  dt_action_t *ac = dt_action_locate(owner, split_path);
  g_strfreev(split_path);

  if(ac)
  {
    ac->type = DT_ACTION_TYPE_WIDGET;

    if(!darktable.control->accel_initialising)
    {
      ac->target = widget;
      g_hash_table_insert(darktable.control->widgets, widget, ac);

      // in case of bauhaus widget more efficient to directly implement in dt_bauhaus_..._destroy
      g_signal_connect(G_OBJECT(widget), "query-tooltip", G_CALLBACK(_shortcut_tooltip_callback), NULL);
      g_signal_connect(G_OBJECT(widget), "destroy", G_CALLBACK(_remove_widget_from_hashtable), NULL);
    }
  }

  return ac;
}


void dt_action_define_iop(dt_iop_module_t *self, const gchar *path, gboolean local, guint accel_key, GdkModifierType mods, GtkWidget *widget)
{
  // add to module_so actions list
  dt_action_t *ac = strstr(path,"blend`") == path
                  ? dt_action_define(&darktable.control->actions_blend, path + strlen("blend`"), local, accel_key, mods, widget)
                  : dt_action_define(&self->so->actions, path, local, accel_key, mods, widget);

  // to support multi-instance, also save per instance widget list
  dt_action_widget_t *referral = g_malloc0(sizeof(dt_action_widget_t));
  referral->action = ac;
  referral->widget = widget;
  self->widget_list = g_slist_prepend(self->widget_list, referral);
}

typedef struct _accel_iop_t
{
  dt_accel_t *accel;
  GClosure *closure;
} _accel_iop_t;

void dt_accel_path_global(char *s, size_t n, const char *path)
{
  snprintf(s, n, "<Darktable>/%s/%s", "global", path);
}

void dt_accel_path_view(char *s, size_t n, char *module, const char *path)
{
  snprintf(s, n, "<Darktable>/%s/%s/%s", "views", module, path);
}

void dt_accel_path_iop(char *s, size_t n, char *module, const char *path)
{
  if(path)
  {

    gchar **split_paths = g_strsplit(path, "`", 4);
    // transitionally keep "preset" translated in keyboardrc to avoid breakage for now
    // this also needs to be amended in preferences
    if(!strcmp(split_paths[0], "preset"))
    {
      g_free(split_paths[0]);
      split_paths[0] = g_strdup(_("preset"));
    }
    for(gchar **cur_path = split_paths; *cur_path; cur_path++)
    {
      gchar *after_context = strchr(*cur_path,'|');
      if(after_context) memmove(*cur_path, after_context + 1, strlen(after_context));
    }
    gchar *joined_paths = g_strjoinv("/", split_paths);
    snprintf(s, n, "<Darktable>/%s/%s/%s", "image operations", module, joined_paths);
    g_free(joined_paths);
    g_strfreev(split_paths);
  }
  else
    snprintf(s, n, "<Darktable>/%s/%s", "image operations", module);
}

void dt_accel_path_lib(char *s, size_t n, char *module, const char *path)
{
  snprintf(s, n, "<Darktable>/%s/%s/%s", "modules", module, path);
}

void dt_accel_path_lua(char *s, size_t n, const char *path)
{
  snprintf(s, n, "<Darktable>/%s/%s", "lua", path);
}

void dt_accel_path_manual(char *s, size_t n, const char *full_path)
{
  snprintf(s, n, "<Darktable>/%s", full_path);
}

static void dt_accel_path_global_translated(char *s, size_t n, const char *path)
{
  snprintf(s, n, "<Darktable>/%s/%s", C_("accel", "global"), g_dpgettext2(NULL, "accel", path));
}

static void dt_accel_path_view_translated(char *s, size_t n, dt_view_t *module, const char *path)
{
  snprintf(s, n, "<Darktable>/%s/%s/%s", C_("accel", "views"), module->name(module),
           g_dpgettext2(NULL, "accel", path));
}

static void dt_accel_path_iop_translated(char *s, size_t n, dt_iop_module_so_t *module, const char *path)
{
  gchar *module_clean = g_strdelimit(g_strdup(module->name()), "/", '-');

  if(path)
  {
    gchar **split_paths = g_strsplit(path, "`", 4);
    for(gchar **cur_path = split_paths; *cur_path; cur_path++)
    {
      gchar *saved_path = *cur_path;
      *cur_path = g_strdelimit(g_strconcat(Q_(*cur_path), (strcmp(*cur_path, "preset") ? NULL : " "), NULL), "/", '`');
      g_free(saved_path);
    }
    gchar *joined_paths = g_strjoinv("/", split_paths);
    snprintf(s, n, "<Darktable>/%s/%s/%s", C_("accel", "processing modules"), module_clean, joined_paths);
    g_free(joined_paths);
    g_strfreev(split_paths);
  }
  else
    snprintf(s, n, "<Darktable>/%s/%s", C_("accel", "processing modules"), module_clean);

  g_free(module_clean);
}

static void dt_accel_path_lib_translated(char *s, size_t n, dt_lib_module_t *module, const char *path)
{
  snprintf(s, n, "<Darktable>/%s/%s/%s", C_("accel", "utility modules"), module->name(module),
           g_dpgettext2(NULL, "accel", path));
}

static void dt_accel_path_lua_translated(char *s, size_t n, const char *path)
{
  snprintf(s, n, "<Darktable>/%s/%s", C_("accel", "lua"), g_dpgettext2(NULL, "accel", path));
}

static void dt_accel_path_manual_translated(char *s, size_t n, const char *full_path)
{
  snprintf(s, n, "<Darktable>/%s", g_dpgettext2(NULL, "accel", full_path));
}

void dt_accel_register_global(const gchar *path, guint accel_key, GdkModifierType mods)
{
  gchar accel_path[256];
  dt_accel_t *accel = (dt_accel_t *)g_malloc0(sizeof(dt_accel_t));

  dt_accel_path_global(accel_path, sizeof(accel_path), path);
  gtk_accel_map_add_entry(accel_path, accel_key, mods);

  g_strlcpy(accel->path, accel_path, sizeof(accel->path));
  dt_accel_path_global_translated(accel_path, sizeof(accel_path), path);
  g_strlcpy(accel->translated_path, accel_path, sizeof(accel->translated_path));

  *(accel->module) = '\0';
  accel->local = FALSE;
  accel->views = DT_VIEW_DARKROOM | DT_VIEW_LIGHTTABLE | DT_VIEW_TETHERING | DT_VIEW_MAP | DT_VIEW_PRINT | DT_VIEW_SLIDESHOW;
  darktable.control->accelerator_list = g_slist_prepend(darktable.control->accelerator_list, accel);
}

void dt_accel_register_view(dt_view_t *self, const gchar *path, guint accel_key, GdkModifierType mods)
{
  gchar accel_path[256];
  dt_accel_t *accel = (dt_accel_t *)g_malloc0(sizeof(dt_accel_t));

  dt_accel_path_view(accel_path, sizeof(accel_path), self->module_name, path);
  gtk_accel_map_add_entry(accel_path, accel_key, mods);

  g_strlcpy(accel->path, accel_path, sizeof(accel->path));
  dt_accel_path_view_translated(accel_path, sizeof(accel_path), self, path);
  g_strlcpy(accel->translated_path, accel_path, sizeof(accel->translated_path));

  g_strlcpy(accel->module, self->module_name, sizeof(accel->module));
  accel->local = FALSE;
  accel->views = self->view(self);
  darktable.control->accelerator_list = g_slist_prepend(darktable.control->accelerator_list, accel);
}

void dt_accel_register_iop(dt_iop_module_so_t *so, gboolean local, const gchar *path, guint accel_key,
                           GdkModifierType mods)
{
  dt_accel_t *accel = (dt_accel_t *)g_malloc0(sizeof(dt_accel_t));

  dt_accel_path_iop(accel->path, sizeof(accel->path), so->op, path);
  gtk_accel_map_add_entry(accel->path, accel_key, mods);
  dt_accel_path_iop_translated(accel->translated_path, sizeof(accel->translated_path), so, path);

  g_strlcpy(accel->module, so->op, sizeof(accel->module));
  accel->local = local;
  accel->views = DT_VIEW_DARKROOM;
  darktable.control->accelerator_list = g_slist_prepend(darktable.control->accelerator_list, accel);

  // add to module_so actions list
}

void dt_accel_register_lib_as_view(gchar *view_name, const gchar *path, guint accel_key, GdkModifierType mods)
{
  //register a lib shortcut but place it in the path of a view
  gchar accel_path[256];
  dt_accel_path_view(accel_path, sizeof(accel_path), view_name, path);
  if (dt_accel_find_by_path(accel_path)) return; // return if nothing to add, to avoid multiple entries

  dt_accel_t *accel = (dt_accel_t *)g_malloc0(sizeof(dt_accel_t));
  gtk_accel_map_add_entry(accel_path, accel_key, mods);
  g_strlcpy(accel->path, accel_path, sizeof(accel->path));

  snprintf(accel_path, sizeof(accel_path), "<Darktable>/%s/%s/%s", C_("accel", "views"),
           g_dgettext(NULL, view_name),
           g_dpgettext2(NULL, "accel", path));

  g_strlcpy(accel->translated_path, accel_path, sizeof(accel->translated_path));

  g_strlcpy(accel->module, view_name, sizeof(accel->module));
  accel->local = FALSE;

  if(strcmp(view_name, "lighttable") == 0)
    accel->views = DT_VIEW_LIGHTTABLE;
  else if(strcmp(view_name, "darkroom") == 0)
    accel->views = DT_VIEW_DARKROOM;
  else if(strcmp(view_name, "print") == 0)
    accel->views = DT_VIEW_PRINT;
  else if(strcmp(view_name, "slideshow") == 0)
    accel->views = DT_VIEW_SLIDESHOW;
  else if(strcmp(view_name, "map") == 0)
    accel->views = DT_VIEW_MAP;
  else if(strcmp(view_name, "tethering") == 0)
    accel->views = DT_VIEW_TETHERING;

  darktable.control->accelerator_list = g_slist_prepend(darktable.control->accelerator_list, accel);
}

void dt_accel_register_lib_for_views(dt_lib_module_t *self, dt_view_type_flags_t views, const gchar *path,
                                     guint accel_key, GdkModifierType mods)
{
  gchar accel_path[256];
  dt_accel_path_lib(accel_path, sizeof(accel_path), self->plugin_name, path);
  if (dt_accel_find_by_path(accel_path)) return; // return if nothing to add, to avoid multiple entries

  dt_accel_t *accel = (dt_accel_t *)g_malloc0(sizeof(dt_accel_t));

  gtk_accel_map_add_entry(accel_path, accel_key, mods);
  g_strlcpy(accel->path, accel_path, sizeof(accel->path));
  dt_accel_path_lib_translated(accel_path, sizeof(accel_path), self, path);
  g_strlcpy(accel->translated_path, accel_path, sizeof(accel->translated_path));

  g_strlcpy(accel->module, self->plugin_name, sizeof(accel->module));
  accel->local = FALSE;
  // we get the views in which the lib will be displayed
  accel->views = views;
  darktable.control->accelerator_list = g_slist_prepend(darktable.control->accelerator_list, accel);

  // FIXME
}

void dt_accel_register_lib(dt_lib_module_t *self, const gchar *path, guint accel_key, GdkModifierType mods)
{
  dt_view_type_flags_t v = 0;
  int i=0;
  const gchar **views = self->views(self);
  while (views[i])
  {
    if(strcmp(views[i], "lighttable") == 0)
      v |= DT_VIEW_LIGHTTABLE;
    else if(strcmp(views[i], "darkroom") == 0)
      v |= DT_VIEW_DARKROOM;
    else if(strcmp(views[i], "print") == 0)
      v |= DT_VIEW_PRINT;
    else if(strcmp(views[i], "slideshow") == 0)
      v |= DT_VIEW_SLIDESHOW;
    else if(strcmp(views[i], "map") == 0)
      v |= DT_VIEW_MAP;
    else if(strcmp(views[i], "tethering") == 0)
      v |= DT_VIEW_TETHERING;
    else if(strcmp(views[i], "*") == 0)
      v |= DT_VIEW_DARKROOM | DT_VIEW_LIGHTTABLE | DT_VIEW_TETHERING | DT_VIEW_MAP | DT_VIEW_PRINT
           | DT_VIEW_SLIDESHOW;
    i++;
  }
  dt_accel_register_lib_for_views(self, v, path, accel_key, mods);
}

const gchar *_common_actions[]
  = { NC_("accel", "show module"),
      NC_("accel", "enable module"),
      NC_("accel", "focus module"),
      NC_("accel", "reset module parameters"),
      NC_("accel", "show preset menu"),
      NULL };

const gchar *_slider_actions[]
  = { NC_("accel", "increase"),
      NC_("accel", "decrease"),
      NC_("accel", "reset"),
      NC_("accel", "edit"),
      NC_("accel", "dynamic"),
      NULL };

const gchar *_combobox_actions[]
  = { NC_("accel", "next"),
      NC_("accel", "previous"),
      NC_("accel", "dynamic"),
      NULL };

void _accel_register_actions_iop(dt_iop_module_so_t *so, gboolean local, const gchar *path, const char **actions)
{
  gchar accel_path[256];
  gchar accel_path_trans[256];
  dt_accel_path_iop(accel_path, sizeof(accel_path), so->op, path);
  dt_accel_path_iop_translated(accel_path_trans, sizeof(accel_path_trans), so, path);

  for(const char **action = actions; *action; action++)
  {
    dt_accel_t *accel = (dt_accel_t *)g_malloc0(sizeof(dt_accel_t));
    snprintf(accel->path, sizeof(accel->path), "%s/%s", accel_path, *action);
    gtk_accel_map_add_entry(accel->path, 0, 0);
    snprintf(accel->translated_path, sizeof(accel->translated_path), "%s/%s ", accel_path_trans,
             g_dpgettext2(NULL, "accel", *action));
    g_strlcpy(accel->module, so->op, sizeof(accel->module));
    accel->local = local;
    accel->views = DT_VIEW_DARKROOM;

    darktable.control->accelerator_list = g_slist_prepend(darktable.control->accelerator_list, accel);
  }
}

void dt_accel_register_common_iop(dt_iop_module_so_t *so)
{
  _accel_register_actions_iop(so, FALSE, NULL, _common_actions);
}

void dt_accel_register_combobox_iop(dt_iop_module_so_t *so, gboolean local, const gchar *path)
{
  _accel_register_actions_iop(so, local, path, _combobox_actions);
}

void dt_accel_register_slider_iop(dt_iop_module_so_t *so, gboolean local, const gchar *path)
{
  _accel_register_actions_iop(so, local, path, _slider_actions);
}

void dt_accel_register_lua(const gchar *path, guint accel_key, GdkModifierType mods)
{
  gchar accel_path[256];
  dt_accel_t *accel = (dt_accel_t *)g_malloc0(sizeof(dt_accel_t));

  dt_accel_path_lua(accel_path, sizeof(accel_path), path);
  gtk_accel_map_add_entry(accel_path, accel_key, mods);

  g_strlcpy(accel->path, accel_path, sizeof(accel->path));
  dt_accel_path_lua_translated(accel_path, sizeof(accel_path), path);
  g_strlcpy(accel->translated_path, accel_path, sizeof(accel->translated_path));

  *(accel->module) = '\0';
  accel->local = FALSE;
  accel->views = DT_VIEW_DARKROOM | DT_VIEW_LIGHTTABLE | DT_VIEW_TETHERING | DT_VIEW_MAP | DT_VIEW_PRINT | DT_VIEW_SLIDESHOW;
  darktable.control->accelerator_list = g_slist_prepend(darktable.control->accelerator_list, accel);
}

void dt_accel_register_manual(const gchar *full_path, dt_view_type_flags_t views, guint accel_key,
                              GdkModifierType mods)
{
  gchar accel_path[256];
  dt_accel_t *accel = (dt_accel_t *)g_malloc0(sizeof(dt_accel_t));

  dt_accel_path_manual(accel_path, sizeof(accel_path), full_path);
  gtk_accel_map_add_entry(accel_path, accel_key, mods);

  g_strlcpy(accel->path, accel_path, sizeof(accel->path));
  dt_accel_path_manual_translated(accel_path, sizeof(accel_path), full_path);
  g_strlcpy(accel->translated_path, accel_path, sizeof(accel->translated_path));

  *(accel->module) = '\0';
  accel->local = FALSE;
  accel->views = views;
  darktable.control->accelerator_list = g_slist_prepend(darktable.control->accelerator_list, accel);
}

static dt_accel_t *_lookup_accel(const gchar *path)
{
  GSList *l = darktable.control->accelerator_list;
  while(l)
  {
    dt_accel_t *accel = (dt_accel_t *)l->data;
    if(accel && !strcmp(accel->path, path)) return accel;
    l = g_slist_next(l);
  }
  return NULL;
}

void dt_accel_connect_global(const gchar *path, GClosure *closure)
{
  gchar accel_path[256];
  dt_accel_path_global(accel_path, sizeof(accel_path), path);
  dt_accel_t *laccel = _lookup_accel(accel_path);
  laccel->closure = closure;
  gtk_accel_group_connect_by_path(darktable.control->accelerators, accel_path, closure);
}

void dt_accel_connect_view(dt_view_t *self, const gchar *path, GClosure *closure)
{
  gchar accel_path[256];
  dt_accel_path_view(accel_path, sizeof(accel_path), self->module_name, path);
  gtk_accel_group_connect_by_path(darktable.control->accelerators, accel_path, closure);
  dt_accel_t *laccel = _lookup_accel(accel_path);
  laccel->closure = closure;

  self->accel_closures = g_slist_prepend(self->accel_closures, laccel);
}

dt_accel_t *dt_accel_connect_lib_as_view(dt_lib_module_t *module, gchar *view_name, const gchar *path, GClosure *closure)
{
  gchar accel_path[256];
  dt_accel_path_view(accel_path, sizeof(accel_path), view_name, path);
  gtk_accel_group_connect_by_path(darktable.control->accelerators, accel_path, closure);

  dt_accel_t *accel = _lookup_accel(accel_path);
  if(!accel) return NULL; // this happens when the path doesn't match any accel (typos, ...)

  accel->closure = closure;

  module->accel_closures = g_slist_prepend(module->accel_closures, accel);
  return accel;
}

dt_accel_t *dt_accel_connect_lib_as_global(dt_lib_module_t *module, const gchar *path, GClosure *closure)
{
  gchar accel_path[256];
  dt_accel_path_global(accel_path, sizeof(accel_path), path);

  dt_accel_t *accel = _lookup_accel(accel_path);
  if(!accel) return NULL; // this happens when the path doesn't match any accel (typos, ...)

  gtk_accel_group_connect_by_path(darktable.control->accelerators, accel_path, closure);

  accel->closure = closure;

  module->accel_closures = g_slist_prepend(module->accel_closures, accel);
  return accel;
}

static dt_accel_t *_store_iop_accel_closure(dt_iop_module_t *module, gchar *accel_path, GClosure *closure)
{
  // Looking up the entry in the global accelerators list
  dt_accel_t *accel = _lookup_accel(accel_path);
  if(!accel) return NULL; // this happens when the path doesn't match any accel (typos, ...)

  GSList **save_list = accel->local ? &module->accel_closures_local : &module->accel_closures;

  _accel_iop_t *stored_accel = g_malloc(sizeof(_accel_iop_t));
  stored_accel->accel = accel;
  stored_accel->closure = closure;

  g_closure_ref(closure);
  g_closure_sink(closure);
  *save_list = g_slist_prepend(*save_list, stored_accel);

  return accel;
}

dt_accel_t *dt_accel_connect_iop(dt_iop_module_t *module, const gchar *path, GClosure *closure)
{
  gchar accel_path[256];
  dt_accel_path_iop(accel_path, sizeof(accel_path), module->op, path);

  return _store_iop_accel_closure(module, accel_path, closure);
}

dt_accel_t *dt_accel_connect_lib(dt_lib_module_t *module, const gchar *path, GClosure *closure)
{
  gchar accel_path[256];
  dt_accel_path_lib(accel_path, sizeof(accel_path), module->plugin_name, path);
  gtk_accel_group_connect_by_path(darktable.control->accelerators, accel_path, closure);

  dt_accel_t *accel = _lookup_accel(accel_path);
  if(!accel) return NULL; // this happens when the path doesn't match any accel (typos, ...)

  accel->closure = closure;

  module->accel_closures = g_slist_prepend(module->accel_closures, accel);
  return accel;
}

void dt_accel_connect_lua(const gchar *path, GClosure *closure)
{
  gchar accel_path[256];
  dt_accel_path_lua(accel_path, sizeof(accel_path), path);
  dt_accel_t *laccel = _lookup_accel(accel_path);
  laccel->closure = closure;
  gtk_accel_group_connect_by_path(darktable.control->accelerators, accel_path, closure);
}

void dt_accel_connect_manual(GSList **list_ptr, const gchar *full_path, GClosure *closure)
{
  gchar accel_path[256];
  dt_accel_path_manual(accel_path, sizeof(accel_path), full_path);
  dt_accel_t *accel = _lookup_accel(accel_path);
  accel->closure = closure;
  gtk_accel_group_connect_by_path(darktable.control->accelerators, accel_path, closure);
  *list_ptr = g_slist_prepend(*list_ptr, accel);
}

static gboolean _press_button_callback(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                       GdkModifierType modifier, gpointer data)
{
  if(!(GTK_IS_BUTTON(data))) return FALSE;

  gtk_button_clicked(GTK_BUTTON(data));
  return TRUE;
}

static gboolean _tooltip_callback(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode,
                                  GtkTooltip *tooltip, gpointer user_data)
{
  char *text = gtk_widget_get_tooltip_text(widget);

  GtkAccelKey key;
  dt_accel_t *accel = g_object_get_data(G_OBJECT(widget), "dt-accel");
  if(accel && gtk_accel_map_lookup_entry(accel->path, &key))
  {
    gchar *key_name = gtk_accelerator_get_label(key.accel_key, key.accel_mods);
    if(key_name && *key_name)
    {
      char *tmp = g_strdup_printf("%s (%s)", text, key_name);
      g_free(text);
      text = tmp;
    }
    g_free(key_name);
  }

  gtk_tooltip_set_text(tooltip, text);
  g_free(text);
  return FALSE;
}

void dt_accel_connect_button_iop(dt_iop_module_t *module, const gchar *path, GtkWidget *button)
{
  GClosure *closure = g_cclosure_new(G_CALLBACK(_press_button_callback), button, NULL);
  dt_accel_t *accel = dt_accel_connect_iop(module, path, closure);
  g_object_set_data(G_OBJECT(button), "dt-accel", accel);

  if(gtk_widget_get_has_tooltip(button))
    g_signal_connect(G_OBJECT(button), "query-tooltip", G_CALLBACK(_tooltip_callback), NULL);

  dt_action_define_iop(module, path, FALSE, 0, 0, button);
}

void dt_accel_connect_button_lib(dt_lib_module_t *module, const gchar *path, GtkWidget *button)
{
  GClosure *closure = g_cclosure_new(G_CALLBACK(_press_button_callback), button, NULL);
  dt_accel_t *accel = dt_accel_connect_lib(module, path, closure);
  g_object_set_data(G_OBJECT(button), "dt-accel", accel);

  if(gtk_widget_get_has_tooltip(button))
    g_signal_connect(G_OBJECT(button), "query-tooltip", G_CALLBACK(_tooltip_callback), NULL);

  dt_action_define(&module->actions, path, FALSE, 0, 0, button);
}

void dt_accel_connect_button_lib_as_global(dt_lib_module_t *module, const gchar *path, GtkWidget *button)
{
  GClosure *closure = g_cclosure_new(G_CALLBACK(_press_button_callback), button, NULL);
  dt_accel_t *accel = dt_accel_connect_lib_as_global(module, path, closure);
  g_object_set_data(G_OBJECT(button), "dt-accel", accel);

  if(gtk_widget_get_has_tooltip(button))
    g_signal_connect(G_OBJECT(button), "query-tooltip", G_CALLBACK(_tooltip_callback), NULL);
}

static gboolean bauhaus_slider_edit_callback(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                             GdkModifierType modifier, gpointer data)
{
  GtkWidget *slider = GTK_WIDGET(data);

  dt_bauhaus_show_popup(DT_BAUHAUS_WIDGET(slider));

  return TRUE;
}

void dt_accel_widget_toast(GtkWidget *widget)
{
  dt_bauhaus_widget_t *w = (dt_bauhaus_widget_t *)DT_BAUHAUS_WIDGET(widget);

  if(!darktable.gui->reset)
  {
    char *text = NULL;

    switch(w->type){
      case DT_BAUHAUS_SLIDER:
      {
        text = dt_bauhaus_slider_get_text(widget);
        break;
      }
      case DT_BAUHAUS_COMBOBOX:
        text = g_strdup(dt_bauhaus_combobox_get_text(widget));
        break;
      default: //literally impossible but hey
        return;
        break;
    }

    if(w->label[0] != '\0')
    { // label is not empty
      if(w->module && w->module->multi_name[0] != '\0')
        dt_toast_log(_("%s %s / %s: %s"), w->module->name(), w->module->multi_name, w->label, text);
      else if(w->module && !strstr(w->module->name(), w->label))
        dt_toast_log(_("%s / %s: %s"), w->module->name(), w->label, text);
      else
        dt_toast_log(_("%s: %s"), w->label, text);
    }
    else
    { //label is empty
      if(w->module && w->module->multi_name[0] != '\0')
        dt_toast_log(_("%s %s / %s"), w->module->name(), w->module->multi_name, text);
      else if(w->module)
        dt_toast_log(_("%s / %s"), w->module->name(), text);
      else
        dt_toast_log("%s", text);
    }

    g_free(text);
  }

}

float dt_accel_get_slider_scale_multiplier()
{
  const int slider_precision = dt_conf_get_int("accel/slider_precision");

  if(slider_precision == DT_IOP_PRECISION_COARSE)
  {
    return dt_conf_get_float("darkroom/ui/scale_rough_step_multiplier");
  }
  else if(slider_precision == DT_IOP_PRECISION_FINE)
  {
    return dt_conf_get_float("darkroom/ui/scale_precise_step_multiplier");
  }

  return dt_conf_get_float("darkroom/ui/scale_step_multiplier");
}

static gboolean bauhaus_slider_increase_callback(GtkAccelGroup *accel_group, GObject *acceleratable,
                                                 guint keyval, GdkModifierType modifier, gpointer data)
{
  GtkWidget *slider = GTK_WIDGET(data);

  float value = dt_bauhaus_slider_get(slider);
  float step = dt_bauhaus_slider_get_step(slider);
  float multiplier = dt_accel_get_slider_scale_multiplier();

  const float min_visible = powf(10.0f, -dt_bauhaus_slider_get_digits(slider));
  if(fabsf(step*multiplier) < min_visible)
    multiplier = min_visible / fabsf(step);

  dt_bauhaus_slider_set(slider, value + step * multiplier);

  g_signal_emit_by_name(G_OBJECT(slider), "value-changed");

  dt_accel_widget_toast(slider);
  return TRUE;
}

static gboolean bauhaus_slider_decrease_callback(GtkAccelGroup *accel_group, GObject *acceleratable,
                                                 guint keyval, GdkModifierType modifier, gpointer data)
{
  GtkWidget *slider = GTK_WIDGET(data);

  float value = dt_bauhaus_slider_get(slider);
  float step = dt_bauhaus_slider_get_step(slider);
  float multiplier = dt_accel_get_slider_scale_multiplier();

  const float min_visible = powf(10.0f, -dt_bauhaus_slider_get_digits(slider));
  if(fabsf(step*multiplier) < min_visible)
    multiplier = min_visible / fabsf(step);

  dt_bauhaus_slider_set(slider, value - step * multiplier);

  g_signal_emit_by_name(G_OBJECT(slider), "value-changed");

  dt_accel_widget_toast(slider);
  return TRUE;
}

static gboolean bauhaus_slider_reset_callback(GtkAccelGroup *accel_group, GObject *acceleratable,
                                              guint keyval, GdkModifierType modifier, gpointer data)
{
  GtkWidget *slider = GTK_WIDGET(data);

  dt_bauhaus_slider_reset(slider);

  g_signal_emit_by_name(G_OBJECT(slider), "value-changed");

  dt_accel_widget_toast(slider);
  return TRUE;
}

static gboolean bauhaus_dynamic_callback(GtkAccelGroup *accel_group, GObject *acceleratable,
                                         guint keyval, GdkModifierType modifier, gpointer data)
{
  if(DT_IS_BAUHAUS_WIDGET(data))
  {
    dt_bauhaus_widget_t *widget = DT_BAUHAUS_WIDGET(data);

    darktable.view_manager->current_view->dynamic_accel_current = GTK_WIDGET(widget);

    gchar *txt = g_strdup_printf (_("scroll to change <b>%s</b> of module %s %s"),
                                  dt_bauhaus_widget_get_label(GTK_WIDGET(widget)),
                                  widget->module->name(), widget->module->multi_name);
    dt_control_hinter_message(darktable.control, txt);
    g_free(txt);
  }
  else
    dt_control_hinter_message(darktable.control, "");

  return TRUE;
}

static gboolean bauhaus_combobox_next_callback(GtkAccelGroup *accel_group, GObject *acceleratable,
                                                 guint keyval, GdkModifierType modifier, gpointer data)
{
  GtkWidget *combobox = GTK_WIDGET(data);

  const int currentval = dt_bauhaus_combobox_get(combobox);
  const int nextval = currentval + 1 >= dt_bauhaus_combobox_length(combobox) ? 0 : currentval + 1;
  dt_bauhaus_combobox_set(combobox, nextval);

  dt_accel_widget_toast(combobox);

  return TRUE;
}

static gboolean bauhaus_combobox_prev_callback(GtkAccelGroup *accel_group, GObject *acceleratable,
                                                 guint keyval, GdkModifierType modifier, gpointer data)
{
  GtkWidget *combobox = GTK_WIDGET(data);

  const int currentval = dt_bauhaus_combobox_get(combobox);
  const int prevval = currentval - 1 < 0 ? dt_bauhaus_combobox_length(combobox) : currentval - 1;
  dt_bauhaus_combobox_set(combobox, prevval);

  dt_accel_widget_toast(combobox);

  return TRUE;
}

void _accel_connect_actions_iop(dt_iop_module_t *module, const gchar *path,
                               GtkWidget *w, const gchar *actions[], void *callbacks[])
{
  gchar accel_path[256];
  dt_accel_path_iop(accel_path, sizeof(accel_path) - 1, module->op, path);
  size_t path_len = strlen(accel_path);
  accel_path[path_len++] = '/';

  for(const char **action = actions; *action; action++, callbacks++)
  {
    strncpy(accel_path + path_len, *action, sizeof(accel_path) - path_len);

    GClosure *closure = g_cclosure_new(G_CALLBACK(*callbacks), (gpointer)w, NULL);

    _store_iop_accel_closure(module, accel_path, closure);
  }
}

void dt_accel_connect_combobox_iop(dt_iop_module_t *module, const gchar *path, GtkWidget *combobox)
{
  assert(DT_IS_BAUHAUS_WIDGET(combobox));

  void *combobox_callbacks[]
    = { bauhaus_combobox_next_callback,
        bauhaus_combobox_prev_callback,
        bauhaus_dynamic_callback };

  _accel_connect_actions_iop(module, path, combobox, _combobox_actions, combobox_callbacks);
}

void dt_accel_connect_slider_iop(dt_iop_module_t *module, const gchar *path, GtkWidget *slider)
{
  assert(DT_IS_BAUHAUS_WIDGET(slider));

  void *slider_callbacks[]
    = { bauhaus_slider_increase_callback,
        bauhaus_slider_decrease_callback,
        bauhaus_slider_reset_callback,
        bauhaus_slider_edit_callback,
        bauhaus_dynamic_callback };

  _accel_connect_actions_iop(module, path, slider, _slider_actions, slider_callbacks);
}

void dt_accel_connect_instance_iop(dt_iop_module_t *module)
{
  for(GSList *l = module->accel_closures; l; l = g_slist_next(l))
  {
    _accel_iop_t *stored_accel = (_accel_iop_t *)l->data;
    if(stored_accel && stored_accel->accel && stored_accel->closure)
    {

      if(stored_accel->accel->closure)
        gtk_accel_group_disconnect(darktable.control->accelerators, stored_accel->accel->closure);

      stored_accel->accel->closure = stored_accel->closure;

      gtk_accel_group_connect_by_path(darktable.control->accelerators,
                                      stored_accel->accel->path, stored_accel->closure);
    }
  }

  for(GSList *w = module->widget_list; w; w = w->next)
  {
    dt_action_widget_t *referral = w->data;
    referral->action->target = referral->widget;
  }
}

void dt_accel_connect_locals_iop(dt_iop_module_t *module)
{
  GSList *l = module->accel_closures_local;
  while(l)
  {
    _accel_iop_t *accel = (_accel_iop_t *)l->data;
    if(accel)
    {
      gtk_accel_group_connect_by_path(darktable.control->accelerators, accel->accel->path, accel->closure);
    }
    l = g_slist_next(l);
  }

  module->local_closures_connected = TRUE;
}

void dt_accel_disconnect_list(GSList **list_ptr)
{
  GSList *list = *list_ptr;
  while(list)
  {
    dt_accel_t *accel = (dt_accel_t *)list->data;
    if(accel) gtk_accel_group_disconnect(darktable.control->accelerators, accel->closure);
    list = g_slist_delete_link(list, list);
  }
  *list_ptr = NULL;
}

void dt_accel_disconnect_locals_iop(dt_iop_module_t *module)
{
  if(!module->local_closures_connected) return;

  GSList *l = module->accel_closures_local;
  while(l)
  {
    _accel_iop_t *accel = (_accel_iop_t *)l->data;
    if(accel)
    {
      gtk_accel_group_disconnect(darktable.control->accelerators, accel->closure);
    }
    l = g_slist_next(l);
  }

  module->local_closures_connected = FALSE;
}

void _free_iop_accel(gpointer data)
{
  _accel_iop_t *accel = (_accel_iop_t *) data;

  if(accel->accel->closure == accel->closure)
  {
    gtk_accel_group_disconnect(darktable.control->accelerators, accel->closure);
    accel->accel->closure = NULL;
  }

  if(accel->closure->ref_count != 1)
    fprintf(stderr, "iop accel refcount %d %s\n", accel->closure->ref_count, accel->accel->path);

  g_closure_unref(accel->closure);

  g_free(accel);
}

void dt_accel_cleanup_closures_iop(dt_iop_module_t *module)
{
  dt_accel_disconnect_locals_iop(module);

  g_slist_free_full(module->accel_closures, _free_iop_accel);
  g_slist_free_full(module->accel_closures_local, _free_iop_accel);
  module->accel_closures = NULL;
  module->accel_closures_local = NULL;
}

typedef struct
{
  dt_iop_module_t *module;
  char *name;
} preset_iop_module_callback_description;

static void preset_iop_module_callback_destroyer(gpointer data, GClosure *closure)
{
  preset_iop_module_callback_description *callback_description
      = (preset_iop_module_callback_description *)data;
  g_free(callback_description->name);
  g_free(data);
}

static gboolean preset_iop_module_callback(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                           GdkModifierType modifier, gpointer data)
{
  preset_iop_module_callback_description *callback_description
      = (preset_iop_module_callback_description *)data;
  dt_iop_module_t *module = callback_description->module;
  const char *name = callback_description->name;

  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "SELECT op_params, enabled, blendop_params, "
                                                             "blendop_version FROM data.presets "
                                                             "WHERE operation = ?1 AND name = ?2",
                              -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->op, -1, SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, name, -1, SQLITE_TRANSIENT);

  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    const void *op_params = sqlite3_column_blob(stmt, 0);
    int op_length = sqlite3_column_bytes(stmt, 0);
    int enabled = sqlite3_column_int(stmt, 1);
    const void *blendop_params = sqlite3_column_blob(stmt, 2);
    int bl_length = sqlite3_column_bytes(stmt, 2);
    int blendop_version = sqlite3_column_int(stmt, 3);
    if(op_params && (op_length == module->params_size))
    {
      memcpy(module->params, op_params, op_length);
      module->enabled = enabled;
    }
    if(blendop_params && (blendop_version == dt_develop_blend_version())
       && (bl_length == sizeof(dt_develop_blend_params_t)))
    {
      memcpy(module->blend_params, blendop_params, sizeof(dt_develop_blend_params_t));
    }
    else if(blendop_params
            && dt_develop_blend_legacy_params(module, blendop_params, blendop_version, module->blend_params,
                                              dt_develop_blend_version(), bl_length) == 0)
    {
      // do nothing
    }
    else
    {
      memcpy(module->blend_params, module->default_blendop_params, sizeof(dt_develop_blend_params_t));
    }
  }
  sqlite3_finalize(stmt);
  dt_iop_gui_update(module);
  dt_dev_add_history_item(darktable.develop, module, FALSE);
  gtk_widget_queue_draw(module->widget);
  return TRUE;
}

void dt_accel_connect_preset_iop(dt_iop_module_t *module, const gchar *path)
{
  char build_path[1024];
  gchar *name = g_strdup(path);
  snprintf(build_path, sizeof(build_path), "%s`%s", N_("preset"), name);
  preset_iop_module_callback_description *callback_description
      = g_malloc(sizeof(preset_iop_module_callback_description));
  callback_description->module = module;
  callback_description->name = name;

  GClosure *closure = g_cclosure_new(G_CALLBACK(preset_iop_module_callback), callback_description,
                                     preset_iop_module_callback_destroyer);
  dt_accel_connect_iop(module, build_path, closure);
}



typedef struct
{
  dt_lib_module_t *module;
  char *name;
} preset_lib_module_callback_description;

static void preset_lib_module_callback_destroyer(gpointer data, GClosure *closure)
{
  preset_lib_module_callback_description *callback_description
      = (preset_lib_module_callback_description *)data;
  g_free(callback_description->name);
  g_free(data);
}
static gboolean preset_lib_module_callback(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                           GdkModifierType modifier, gpointer data)

{
  preset_lib_module_callback_description *callback_description
      = (preset_lib_module_callback_description *)data;
  dt_lib_module_t *module = callback_description->module;
  const char *pn = callback_description->name;

  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(
      dt_database_get(darktable.db),
      "SELECT op_params FROM data.presets WHERE operation = ?1 AND op_version = ?2 AND name = ?3", -1, &stmt,
      NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->plugin_name, -1, SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, module->version());
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, pn, -1, SQLITE_TRANSIENT);

  int res = 0;
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    const void *blob = sqlite3_column_blob(stmt, 0);
    int length = sqlite3_column_bytes(stmt, 0);
    if(blob)
    {
      GList *it = darktable.lib->plugins;
      while(it)
      {
        dt_lib_module_t *search_module = (dt_lib_module_t *)it->data;
        if(!strncmp(search_module->plugin_name, module->plugin_name, 128))
        {
          res = module->set_params(module, blob, length);
          break;
        }
        it = g_list_next(it);
      }
    }
  }
  sqlite3_finalize(stmt);
  if(res)
  {
    dt_control_log(_("deleting preset for obsolete module"));
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                                "DELETE FROM data.presets WHERE operation = ?1 AND op_version = ?2 AND name = ?3",
                                -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->plugin_name, -1, SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, module->version());
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, pn, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  return TRUE;
}

void dt_accel_connect_preset_lib(dt_lib_module_t *module, const gchar *path)
{
  char build_path[1024];
  gchar *name = g_strdup(path);
  snprintf(build_path, sizeof(build_path), "%s/%s", _("preset"), name);
  preset_lib_module_callback_description *callback_description
      = g_malloc(sizeof(preset_lib_module_callback_description));
  callback_description->module = module;
  callback_description->name = name;

  GClosure *closure = g_cclosure_new(G_CALLBACK(preset_lib_module_callback), callback_description,
                                     preset_lib_module_callback_destroyer);
  dt_accel_connect_lib(module, build_path, closure);
}

void dt_accel_deregister_iop(dt_iop_module_t *module, const gchar *path)
{
  char build_path[1024];
  dt_accel_path_iop(build_path, sizeof(build_path), module->op, path);

  dt_accel_t *accel = NULL;

  GList *modules = g_list_first(darktable.develop->iop);
  while(modules)
  {
    dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;

    if(mod->so == module->so)
    {
      GSList **current_list = &mod->accel_closures;
      GSList *l = *current_list;
      while(l)
      {
        _accel_iop_t *iop_accel = (_accel_iop_t *)l->data;

        if(iop_accel && iop_accel->accel && !strncmp(iop_accel->accel->path, build_path, 1024))
        {
          accel = iop_accel->accel;

          if(iop_accel->closure == accel->closure || (accel->local && module->local_closures_connected))
            gtk_accel_group_disconnect(darktable.control->accelerators, iop_accel->closure);

          *current_list = g_slist_delete_link(*current_list, l);

          g_closure_unref(iop_accel->closure);

          g_free(iop_accel);

          break;
        }

        l = g_slist_next(l);
        if(!l && current_list == &mod->accel_closures) l = *(current_list = &module->accel_closures_local);
      }
    }

    modules = g_list_next(modules);
  }

  if(accel)
  {
      darktable.control->accelerator_list = g_slist_remove(darktable.control->accelerator_list, accel);

      g_free(accel);
  }
}

void dt_accel_deregister_lib(dt_lib_module_t *module, const gchar *path)
{
  char build_path[1024];
  dt_accel_path_lib(build_path, sizeof(build_path), module->plugin_name, path);
  GSList *l = module->accel_closures;
  while(l)
  {
    dt_accel_t *accel = (dt_accel_t *)l->data;
    if(accel && !strncmp(accel->path, build_path, 1024))
    {
      module->accel_closures = g_slist_delete_link(module->accel_closures, l);
      gtk_accel_group_disconnect(darktable.control->accelerators, accel->closure);
      l = NULL;
    }
    else
    {
      l = g_slist_next(l);
    }
  }
  l = darktable.control->accelerator_list;
  while(l)
  {
    dt_accel_t *accel = (dt_accel_t *)l->data;
    if(accel && !strncmp(accel->path, build_path, 1024))
    {
      darktable.control->accelerator_list = g_slist_delete_link(darktable.control->accelerator_list, l);
      l = NULL;
      g_free(accel);
    }
    else
    {
      l = g_slist_next(l);
    }
  }
}

void dt_accel_deregister_global(const gchar *path)
{
  char build_path[1024];
  dt_accel_path_global(build_path, sizeof(build_path), path);
  GSList *l = darktable.control->accelerator_list;
  while(l)
  {
    dt_accel_t *accel = (dt_accel_t *)l->data;
    if(accel && !strncmp(accel->path, build_path, 1024))
    {
      darktable.control->accelerator_list = g_slist_delete_link(darktable.control->accelerator_list, l);
      gtk_accel_group_disconnect(darktable.control->accelerators, accel->closure);
      l = NULL;
      g_free(accel);
    }
    else
    {
      l = g_slist_next(l);
    }
  }
}

void dt_accel_deregister_lua(const gchar *path)
{
  char build_path[1024];
  dt_accel_path_lua(build_path, sizeof(build_path), path);
  GSList *l = darktable.control->accelerator_list;
  while(l)
  {
    dt_accel_t *accel = (dt_accel_t *)l->data;
    if(accel && !strncmp(accel->path, build_path, 1024))
    {
      darktable.control->accelerator_list = g_slist_delete_link(darktable.control->accelerator_list, l);
      gtk_accel_group_disconnect(darktable.control->accelerators, accel->closure);
      l = NULL;
      g_free(accel);
    }
    else
    {
      l = g_slist_next(l);
    }
  }
}

void dt_accel_deregister_manual(GSList *list, const gchar *full_path)
{
  GSList *l;
  char build_path[1024];
  dt_accel_path_manual(build_path, sizeof(build_path), full_path);
  l = list;
  while(l)
  {
    dt_accel_t *accel = (dt_accel_t *)l->data;
    if(accel && !strncmp(accel->path, build_path, 1024))
    {
      list = g_slist_delete_link(list, l);
      gtk_accel_group_disconnect(darktable.control->accelerators, accel->closure);
      l = NULL;
    }
    else
    {
      l = g_slist_next(l);
    }
  }
  l = darktable.control->accelerator_list;
  while(l)
  {
    dt_accel_t *accel = (dt_accel_t *)l->data;
    if(accel && !strncmp(accel->path, build_path, 1024))
    {
      darktable.control->accelerator_list = g_slist_delete_link(darktable.control->accelerator_list, l);
      l = NULL;
      g_free(accel);
    }
    else
    {
      l = g_slist_next(l);
    }
  }
}

gboolean find_accel_internal(GtkAccelKey *key, GClosure *closure, gpointer data)
{
  return (closure == data);
}

void dt_accel_rename_preset_iop(dt_iop_module_t *module, const gchar *path, const gchar *new_path)
{
  char *path_preset = g_strdup_printf("%s`%s", N_("preset"), path);

  char build_path[1024];
  dt_accel_path_iop(build_path, sizeof(build_path), module->op, path_preset);

  GSList *l = module->accel_closures;
  while(l)
  {
    _accel_iop_t *iop_accel = (_accel_iop_t *)l->data;
    if(iop_accel && iop_accel->accel && !strncmp(iop_accel->accel->path, build_path, 1024))
    {
      GtkAccelKey tmp_key
          = *(gtk_accel_group_find(darktable.control->accelerators, find_accel_internal, iop_accel->closure));
      gboolean local = iop_accel->accel->local;

      dt_accel_deregister_iop(module, path_preset);

      snprintf(build_path, sizeof(build_path), "%s`%s", N_("preset"), new_path);
      dt_accel_register_iop(module->so, local, build_path, tmp_key.accel_key, tmp_key.accel_mods);

      GList *modules = g_list_first(darktable.develop->iop);
      while(modules)
      {
        dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;

        if(mod->so == module->so) dt_accel_connect_preset_iop(mod, new_path);

        modules = g_list_next(modules);
      }

      break;
    }

    l = g_slist_next(l);
  }

  g_free(path_preset);

  dt_accel_connect_instance_iop(module);
}

void dt_accel_rename_preset_lib(dt_lib_module_t *module, const gchar *path, const gchar *new_path)
{
  char build_path[1024];
  dt_accel_path_lib(build_path, sizeof(build_path), module->plugin_name, path);
  GSList *l = module->accel_closures;
  while(l)
  {
    dt_accel_t *accel = (dt_accel_t *)l->data;
    if(accel && !strncmp(accel->path, build_path, 1024))
    {
      GtkAccelKey tmp_key
          = *(gtk_accel_group_find(darktable.control->accelerators, find_accel_internal, accel->closure));
      dt_accel_deregister_lib(module, path);
      snprintf(build_path, sizeof(build_path), "%s/%s", _("preset"), new_path);
      dt_accel_register_lib(module, build_path, tmp_key.accel_key, tmp_key.accel_mods);
      dt_accel_connect_preset_lib(module, new_path);
      l = NULL;
    }
    else
    {
      l = g_slist_next(l);
    }
  }
}

void dt_accel_rename_global(const gchar *path, const gchar *new_path)
{
  char build_path[1024];
  dt_accel_path_global(build_path, sizeof(build_path), path);
  GSList *l = darktable.control->accelerator_list;
  while(l)
  {
    dt_accel_t *accel = (dt_accel_t *)l->data;
    if(accel && !strncmp(accel->path, build_path, 1024))
    {
      GtkAccelKey tmp_key
          = *(gtk_accel_group_find(darktable.control->accelerators, find_accel_internal, accel->closure));
      dt_accel_deregister_global(path);
      g_closure_ref(accel->closure);
      dt_accel_register_global(new_path, tmp_key.accel_key, tmp_key.accel_mods);
      dt_accel_connect_global(new_path, accel->closure);
      g_closure_unref(accel->closure);
      l = NULL;
    }
    else
    {
      l = g_slist_next(l);
    }
  }
}

void dt_accel_rename_lua(const gchar *path, const gchar *new_path)
{
  char build_path[1024];
  dt_accel_path_lua(build_path, sizeof(build_path), path);
  GSList *l = darktable.control->accelerator_list;
  while(l)
  {
    dt_accel_t *accel = (dt_accel_t *)l->data;
    if(accel && !strncmp(accel->path, build_path, 1024))
    {
      GtkAccelKey tmp_key
          = *(gtk_accel_group_find(darktable.control->accelerators, find_accel_internal, accel->closure));
      dt_accel_deregister_lua(path);
      g_closure_ref(accel->closure);
      dt_accel_register_lua(new_path, tmp_key.accel_key, tmp_key.accel_mods);
      dt_accel_connect_lua(new_path, accel->closure);
      g_closure_unref(accel->closure);
      l = NULL;
    }
    else
    {
      l = g_slist_next(l);
    }
  }
}

dt_accel_t *dt_accel_find_by_path(const gchar *path)
{
  return _lookup_accel(path);
}
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
