/*
  Copyright © 2004 Callum McKenzie
  Copyright © 2007, 2008, 2009 Christian Persch

  This library is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <errno.h>
#include <string.h>
#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include "games-debug.h"
#include "games-runtime.h"
#include "games-string-utils.h"

#include "games-card-theme.h"
#include "games-card-theme-private.h"

struct _GamesCardThemePysolClass {
  GamesCardThemeClass parent_class;
};

struct _GamesCardThemePysol {
  GamesCardTheme parent_instance;
};

/* Constants copied from PySol:
 *
 * Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003 Markus Franz Xaver Johannes Oberhumer
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define PYSOL_CONFIG_FILENAME "config.txt"

/* PySol cardset size */
enum {
  PYSOL_CARDSET_SIZE_TINY   = 1,
  PYSOL_CARDSET_SIZE_SMALL  = 2,
  PYSOL_CARDSET_SIZE_MEDIUM = 3,
  PYSOL_CARDSET_SIZE_LARGE  = 4,
  PYSOL_CARDSET_SIZE_XLARGE = 5
};

/* PySol cardset type */
enum {
  PYSOL_CARDSET_TYPE_FRENCH               = 1, /* French type (52 cards)                */
  PYSOL_CARDSET_TYPE_HANAFUDA             = 2, /* Hanafuda type (48 cards)              */
  PYSOL_CARDSET_TYPE_TAROCK               = 3, /* Tarock type (78 cards)                */
  PYSOL_CARDSET_TYPE_MAHJONGG             = 4, /* Mahjongg type (42 tiles)              */
  PYSOL_CARDSET_TYPE_HEXADECK             = 5, /* Hex A Deck type (68 cards)            */
  PYSOL_CARDSET_TYPE_MUGHAL_GANJIFA       = 6, /* Mughal Ganjifa type (96 cards)        */
  PYSOL_CARDSET_TYPE_NAVAGRAHA_GANJIFA    = 7, /* Navagraha Ganjifa type (108 cards)    */
  PYSOL_CARDSET_TYPE_DASHAVATARA_GANJIFA  = 8, /* Dashavatara Ganjifa type (120 cards)  */
  PYSOL_CARDSET_TYPE_TRUMP_ONLY           = 9  /* Trumps only type (variable cards)     */
};

typedef struct {
  char *name;
  char *base_path;
  char *ext;
  int version;
  int type;
  int n_cards;
  CardSize card_size;
  int card_delta;
  char **backs;
  int n_backs;
  int default_back_index;
} PySolConfigTxtData;

static void
pysol_config_txt_data_free (PySolConfigTxtData *data)
{
  g_free (data->name);
  g_free (data->base_path);
  g_free (data->ext);
  g_strfreev (data->backs);
  g_free (data);
}

static gboolean
parse_int (char *string,
           int *value)
{
  char *endptr;

  errno = 0;
  endptr = NULL;
  *value = g_ascii_strtoll (string, &endptr, 10);

  return errno == 0 && endptr != string;
}

static gboolean
pysol_config_txt_parse_line_0 (PySolConfigTxtData *data,
                               const char *line)
{
  char **fields;
  gsize n_fields;
  gboolean retval = FALSE;

  /* FIXMEchpe */
  data->version = 0;
  data->n_cards = 52;
  data->type = 1;

  fields = g_strsplit (line, ";", -1);
  if (!fields)
    return FALSE;

  n_fields = g_strv_length (fields);
  if (n_fields < 2)
    goto out;
  if (strcmp (g_strstrip (fields[0]), "PySol solitaire cardset") != 0)
    goto out;
  if (!parse_int (g_strstrip (fields[1]), &data->version))
    goto out;

  if (data->version >= 3) {
    if (n_fields < 4)
      goto out;

    if (strlen (fields[2]) > 0)
      data->ext = g_strstrip (g_strdup (fields[2]));
    else
      data->ext = g_strdup (".gif");

    if (!parse_int (fields[3], &data->type))
      goto out;
    if (!parse_int (fields[4], &data->n_cards))
      goto out;
  }

  retval = TRUE;
out:
  g_strfreev (fields);
  return retval;
}

static gboolean
pysol_config_txt_parse_line_1 (PySolConfigTxtData *data,
                               const char *line)
{
  char **fields;
  gsize n_fields;
  gboolean retval = FALSE;

  fields = g_strsplit (line, ";", -1);
  if (!fields)
    return FALSE;
  n_fields = g_strv_length (fields);
  if (n_fields < 2)
    goto out;

  data->name = g_strstrip (g_strdup (fields[1]));

  retval = TRUE;
out:
  g_strfreev (fields);
  return retval;
}

static gboolean
pysol_config_txt_parse_line_2 (PySolConfigTxtData *data,
                               const char *line)
{
  char **fields;
  gsize n_fields;
  gboolean retval = FALSE;

  fields = g_strsplit (line, " ", -1);
  if (!fields)
    return FALSE;
  n_fields = g_strv_length (fields);
  if (n_fields != 3)
    goto out;
  if (!parse_int (g_strstrip (fields[0]), &data->card_size.width) ||
      !parse_int (g_strstrip (fields[1]), &data->card_size.height) ||
      !parse_int (g_strstrip (fields[2]), &data->card_delta))
    goto out;

  retval = TRUE;
out:
  g_strfreev (fields);
  return retval;
}

static gboolean
pysol_config_txt_parse_line_4_and_5 (PySolConfigTxtData *data,
                                     const char *line4,
                                     const char *line5)
{
  guint i;

  data->backs = g_strsplit (line5, ";", -1);
  if (!data->backs)
    return FALSE;
  data->n_backs = g_strv_length (data->backs);
  if (data->n_backs < 1)
    return FALSE;
  for (i = 0; i < data->n_backs; ++i)
    g_strstrip (data->backs[i]);

  /* Get the index of the default back (specified in line[4]) */
  data->default_back_index = 0;
  for (i = 0; i < data->n_backs; ++i)
    if (strcmp (data->backs[i], line4) == 0)
      data->default_back_index = i;

  return TRUE;
}

static PySolConfigTxtData *
pysol_config_txt_parse (const char *path,
                        const char *subdir)
{
  PySolConfigTxtData *pysol_data = NULL;
  char *config_txt_path;
  char *data = NULL;
  char **lines = NULL;
  gsize len;
  gboolean retval = FALSE;

  config_txt_path = g_build_filename (path, subdir, PYSOL_CONFIG_FILENAME, NULL);
  if (!g_file_get_contents (config_txt_path, &data, &len, NULL) || !len)
    goto out;

  lines = g_strsplit (data, "\n", -1);
  if (!lines || g_strv_length (lines) < 6)
    goto out;

  pysol_data = g_new0 (PySolConfigTxtData, 1);
  if (!pysol_config_txt_parse_line_0 (pysol_data, g_strstrip (lines[0])) ||
        pysol_data->type != PYSOL_CARDSET_TYPE_FRENCH ||
        pysol_data->n_cards != 52 ||
        !pysol_data->ext ||
      !pysol_config_txt_parse_line_1 (pysol_data, g_strstrip (lines[1])) ||
        !pysol_data->name ||
      !pysol_config_txt_parse_line_2 (pysol_data, g_strstrip (lines[2])) ||
      !pysol_config_txt_parse_line_4_and_5 (pysol_data, g_strstrip (lines[4]), g_strstrip (lines[5])))
    goto out;

  pysol_data->base_path = g_build_filename (path, subdir, NULL);

  retval = TRUE;

out:
  g_free (config_txt_path);
  g_free (data);
  g_strfreev (lines);

  if (retval)
    return pysol_data;

  if (pysol_data)
    pysol_config_txt_data_free (pysol_data);
    
  return NULL;
}

/* Class implementation */

G_DEFINE_TYPE (GamesCardThemePysol, games_card_theme_pysol, GAMES_TYPE_CARD_THEME);

static gboolean
games_card_theme_pysol_load (GamesCardTheme *card_theme,
                             GError **error)
{
  /* nothing more to do here, we have all the info in our PySolConfigTxtData */
  return TRUE;
}

static void
games_card_theme_pysol_init (GamesCardThemePysol *theme)
{
}

static gboolean
games_card_theme_pysol_set_card_size (GamesCardTheme *card_theme,
                                      int width,
                                      int height,
                                      double proportion)
{
  /* not changing, ever */
  return FALSE;
}

static void
games_card_theme_pysol_get_card_size (GamesCardTheme *card_theme,
                                      CardSize *size)
{
  GamesCardThemeInfo *theme_info = card_theme->theme_info;
  PySolConfigTxtData *pysol_data = theme_info->data;

  *size = pysol_data->card_size;
}

static double
games_card_theme_pysol_get_card_aspect (GamesCardTheme *card_theme)
{
  PySolConfigTxtData *pysol_data = card_theme->theme_info->data;

  return ((double) pysol_data->card_size.width) / ((double) pysol_data->card_size.height);
}

static GdkPixbuf *
games_card_theme_pysol_get_card_pixbuf (GamesCardTheme *card_theme,
                                        int card_id)
{
  PySolConfigTxtData *data = card_theme->theme_info->data;
  GdkPixbuf *pixbuf;
  char *path;
  GError *error = NULL;

  if (G_UNLIKELY (card_id == GAMES_CARD_SLOT)) {
    path = g_build_filename (data->base_path, "bottom01.gif" /* FIXMEchpe ext! */, NULL);
  } else if (G_UNLIKELY (card_id == GAMES_CARD_BACK)) {
    path = g_build_filename (data->base_path, data->backs[data->default_back_index], NULL);
  } else {
    static const char suit_char[] = "cdhs";
    int suit, rank;
    char filename[32];

    suit = card_id / 13;
    rank = card_id % 13;

    if (G_UNLIKELY (suit == 4)) /* Joker */
      return NULL; /* FIXMEchpe */

    g_snprintf (filename, sizeof (filename), "%02d%c%s", rank + 1, suit_char[suit], data->ext);
    path = g_build_filename (data->base_path, filename, NULL);
  }

  pixbuf = gdk_pixbuf_new_from_file (path, &error);
  if (!pixbuf) {
    _games_debug_print (GAMES_DEBUG_CARD_THEME,
                        "Failed to load card ID %d: %s\n",
                        card_id, error->message);
    g_error_free (error);
  }

  g_free (path);

  return pixbuf;
}

static GamesCardThemeInfo *
games_card_theme_pysol_class_get_theme_info (GamesCardThemeClass *klass,
                                             const char *path,
                                             const char *filename)
{
  GamesCardThemeInfo *info = NULL;
  PySolConfigTxtData *pysol_data;
  char *display_name, *pref_name;

  if (!g_str_has_prefix (filename, "cardset-"))
    return NULL;

  pysol_data = pysol_config_txt_parse (path, filename);
  if (!pysol_data)
    return NULL;

  display_name = g_strdup_printf ("%s (PySol)", pysol_data->name);
  pref_name = g_strdup_printf ("pysol:%s", filename);
  info = _games_card_theme_info_new (G_OBJECT_CLASS_TYPE (klass),
                                     path,
                                     filename,
                                     display_name /* adopts */,
                                     pref_name /* adopts */,
                                     pysol_data,
                                     (GDestroyNotify) pysol_config_txt_data_free);

  return info;
}

static gboolean
games_card_theme_pysol_class_foreach_theme_dir (GamesCardThemeClass *klass,
                                                GamesCardThemeForeachFunc callback,
                                                gpointer data)
{
  if (!_games_card_theme_class_foreach_env (klass, "GAMES_CARD_THEME_PATH_PYSOL", callback, data))
    return FALSE;

  /* FIXMEchpe: is this univeral or ubuntu specific? */
  return callback (klass, "/usr/share/games/pysol", data);
}

static void
games_card_theme_pysol_class_init (GamesCardThemePysolClass * klass)
{
  GamesCardThemeClass *theme_class = GAMES_CARD_THEME_CLASS (klass);

  theme_class->get_theme_info = games_card_theme_pysol_class_get_theme_info;
  theme_class->foreach_theme_dir = games_card_theme_pysol_class_foreach_theme_dir;

  theme_class->load = games_card_theme_pysol_load;
  theme_class->set_card_size = games_card_theme_pysol_set_card_size;
  theme_class->get_card_size = games_card_theme_pysol_get_card_size;
  theme_class->get_card_aspect = games_card_theme_pysol_get_card_aspect;
  theme_class->get_card_pixbuf = games_card_theme_pysol_get_card_pixbuf;
}

/* public API */

/**
 * games_card_theme_pysol_new:
 *
 * Returns: a new #GamesCardThemePysol
 */
GamesCardTheme *
games_card_theme_pysol_new (void)
{
  return g_object_new (GAMES_TYPE_CARD_THEME_PYSOL, NULL);
}
