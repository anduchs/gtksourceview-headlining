/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; coding: utf-8 -*- */
/* gtksourcestylescheme.c
 * This file is part of GtkSourceView
 *
 * Copyright (C) 2003 - Paolo Maggi <paolo.maggi@polito.it>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "gtksourceview-i18n.h"
#include "gtksourcestyleschememanager.h"
#include "gtksourceview.h"
#include "gtksourcelanguage-private.h"
#include "gtksourcestyle-private.h"
#include <libxml/parser.h>
#include <string.h>

/**
 * SECTION:stylescheme
 * @Short_description: Object controlling the appearance of #GtkSourceView
 * @Title: GtkSourceStyleScheme
 * @See_also: #GtkSourceStyle, #GtkSourceStyleSchemeManager
 *
 * #GtkSourceStyleScheme contains all the text styles to be used in
 * #GtkSourceView and #GtkSourceBuffer. For instance, it contains text styles
 * for syntax highlighting, it may contain foreground and background color for
 * non-highlighted text, color for the line numbers, etc.
 *
 * Style schemes are stored in XML files. The format of a scheme file is
 * documented in the
 * <link linkend="style-reference">style scheme reference</link>.
 */

#define STYLE_TEXT			"text"
#define STYLE_SELECTED			"selection"
#define STYLE_SELECTED_UNFOCUSED	"selection-unfocused"
#define STYLE_BRACKET_MATCH		"bracket-match"
#define STYLE_BRACKET_MISMATCH		"bracket-mismatch"
#define STYLE_CURSOR			"cursor"
#define STYLE_SECONDARY_CURSOR		"secondary-cursor"
#define STYLE_CURRENT_LINE		"current-line"
#define STYLE_LINE_NUMBERS		"line-numbers"
#define STYLE_RIGHT_MARGIN		"right-margin"
#define STYLE_DRAW_SPACES		"draw-spaces"

#define STYLE_SCHEME_VERSION		"1.0"

#define DEFAULT_STYLE_SCHEME		"classic"


enum {
	PROP_0,
	PROP_ID,
	PROP_NAME,
	PROP_DESCRIPTION,
	PROP_FILENAME
};

struct _GtkSourceStyleSchemePrivate
{
	gchar *id;
	gchar *name;
	GPtrArray *authors;
	gchar *description;
	gchar *filename;
	GtkSourceStyleScheme *parent;
	gchar *parent_id;
	GHashTable *defined_styles;
	GHashTable *style_cache;
	GHashTable *named_colors;

	GtkCssProvider *css;
};

G_DEFINE_TYPE (GtkSourceStyleScheme, gtk_source_style_scheme, G_TYPE_OBJECT)

static void
gtk_source_style_scheme_dispose (GObject *object)
{
	GtkSourceStyleScheme *scheme = GTK_SOURCE_STYLE_SCHEME (object);

	if (scheme->priv->named_colors != NULL)
	{
		g_hash_table_unref (scheme->priv->named_colors);
		scheme->priv->named_colors = NULL;
	}

	if (scheme->priv->style_cache != NULL)
	{
		g_hash_table_unref (scheme->priv->style_cache);
		scheme->priv->style_cache = NULL;
	}

	if (scheme->priv->defined_styles != NULL)
	{
		g_hash_table_unref (scheme->priv->defined_styles);
		scheme->priv->defined_styles = NULL;
	}

	g_clear_object (&scheme->priv->parent);
	g_clear_object (&scheme->priv->css);

	G_OBJECT_CLASS (gtk_source_style_scheme_parent_class)->dispose (object);
}

static void
gtk_source_style_scheme_finalize (GObject *object)
{
	GtkSourceStyleScheme *scheme = GTK_SOURCE_STYLE_SCHEME (object);

	if (scheme->priv->authors != NULL)
	{
		g_ptr_array_foreach (scheme->priv->authors, (GFunc)g_free, NULL);
		g_ptr_array_free (scheme->priv->authors, TRUE);
	}

	g_free (scheme->priv->filename);
	g_free (scheme->priv->description);
	g_free (scheme->priv->id);
	g_free (scheme->priv->name);
	g_free (scheme->priv->parent_id);

	G_OBJECT_CLASS (gtk_source_style_scheme_parent_class)->finalize (object);
}

static void
gtk_source_style_scheme_set_property (GObject 	   *object,
				      guint         prop_id,
				      const GValue *value,
				      GParamSpec   *pspec)
{
	char *tmp;
	GtkSourceStyleScheme *scheme = GTK_SOURCE_STYLE_SCHEME (object);

	switch (prop_id)
	{
		case PROP_ID:
			tmp = scheme->priv->id;
			scheme->priv->id = g_value_dup_string (value);
			g_free (tmp);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gtk_source_style_scheme_get_property (GObject 	 *object,
				      guint 	  prop_id,
				      GValue 	 *value,
				      GParamSpec *pspec)
{
	GtkSourceStyleScheme *scheme = GTK_SOURCE_STYLE_SCHEME (object);

	switch (prop_id)
	{
		case PROP_ID:
			g_value_set_string (value, scheme->priv->id);
			break;

		case PROP_NAME:
			g_value_set_string (value, scheme->priv->name);
			break;

		case PROP_DESCRIPTION:
			g_value_set_string (value, scheme->priv->description);
			break;

		case PROP_FILENAME:
			g_value_set_string (value, scheme->priv->filename);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gtk_source_style_scheme_class_init (GtkSourceStyleSchemeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gtk_source_style_scheme_dispose;
	object_class->finalize = gtk_source_style_scheme_finalize;
	object_class->set_property = gtk_source_style_scheme_set_property;
	object_class->get_property = gtk_source_style_scheme_get_property;

	/**
	 * GtkSourceStyleScheme:id:
	 *
	 * Style scheme id, a unique string used to identify the style scheme
	 * in #GtkSourceStyleSchemeManager.
	 */
	g_object_class_install_property (object_class,
					 PROP_ID,
					 g_param_spec_string ("id",
						 	      _("Style scheme id"),
							      _("Style scheme id"),
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * GtkSourceStyleScheme:name:
	 *
	 * Style scheme name, a translatable string to present to user.
	 */
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
						 	      _("Style scheme name"),
							      _("Style scheme name"),
							      NULL,
							      G_PARAM_READABLE));

	/**
	 * GtkSourceStyleScheme:description:
	 *
	 * Style scheme description, a translatable string to present to user.
	 */
	g_object_class_install_property (object_class,
					 PROP_DESCRIPTION,
					 g_param_spec_string ("description",
						 	      _("Style scheme description"),
							      _("Style scheme description"),
							      NULL,
							      G_PARAM_READABLE));

	/**
	 * GtkSourceStyleScheme:filename:
	 *
	 * Style scheme filename or %NULL.
	 */
	g_object_class_install_property (object_class,
					 PROP_FILENAME,
					 g_param_spec_string ("filename",
						 	      _("Style scheme filename"),
							      _("Style scheme filename"),
							      NULL,
							      G_PARAM_READABLE));

	g_type_class_add_private (object_class, sizeof (GtkSourceStyleSchemePrivate));
}

static void
unref_if_not_null (gpointer object)
{
	if (object != NULL)
		g_object_unref (object);
}

static void
gtk_source_style_scheme_init (GtkSourceStyleScheme *scheme)
{
	scheme->priv = G_TYPE_INSTANCE_GET_PRIVATE (scheme, GTK_SOURCE_TYPE_STYLE_SCHEME,
						    GtkSourceStyleSchemePrivate);

	scheme->priv->defined_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
							      g_free, g_object_unref);
	scheme->priv->style_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
							   g_free, unref_if_not_null);
	scheme->priv->named_colors = g_hash_table_new_full (g_str_hash, g_str_equal,
							    g_free, g_free);

	scheme->priv->css = gtk_css_provider_new ();
}

/**
 * gtk_source_style_scheme_get_id:
 * @scheme: a #GtkSourceStyleScheme.
 *
 * Returns: @scheme id.
 *
 * Since: 2.0
 */
const gchar *
gtk_source_style_scheme_get_id (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);
	g_return_val_if_fail (scheme->priv->id != NULL, "");

	return scheme->priv->id;
}

/**
 * gtk_source_style_scheme_get_name:
 * @scheme: a #GtkSourceStyleScheme.
 *
 * Returns: @scheme name.
 *
 * Since: 2.0
 */
const gchar *
gtk_source_style_scheme_get_name (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);
	g_return_val_if_fail (scheme->priv->name != NULL, "");

	return scheme->priv->name;
}

/**
 * gtk_source_style_scheme_get_description:
 * @scheme: a #GtkSourceStyleScheme.
 *
 * Returns: @scheme description (if defined), or %NULL.
 *
 * Since: 2.0
 */
const gchar *
gtk_source_style_scheme_get_description (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);

	return scheme->priv->description;
}

/**
 * gtk_source_style_scheme_get_authors:
 * @scheme: a #GtkSourceStyleScheme.
 *
 * Returns: (array zero-terminated=1) (transfer none): a %NULL-terminated
 * array containing the @scheme authors or %NULL if no author
 * is specified by the style scheme.
 *
 * Since: 2.0
 */
const gchar * const *
gtk_source_style_scheme_get_authors (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);

	if (scheme->priv->authors == NULL)
		return NULL;

	return (const gchar * const *)scheme->priv->authors->pdata;
}

/**
 * gtk_source_style_scheme_get_filename:
 * @scheme: a #GtkSourceStyleScheme.
 *
 * Returns: @scheme file name if the scheme was created parsing a
 * style scheme file or %NULL in the other cases.
 *
 * Since: 2.0
 */
const gchar *
gtk_source_style_scheme_get_filename (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);

	return scheme->priv->filename;
}

/**
 * _gtk_source_style_scheme_new:
 * @id: scheme id.
 * @name: scheme name.
 *
 * Returns: new empty #GtkSourceStyleScheme.
 *
 * Since: 2.0
 */
GtkSourceStyleScheme *
_gtk_source_style_scheme_new (const gchar *id,
			      const gchar *name)
{
	GtkSourceStyleScheme *scheme;

	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	scheme = g_object_new (GTK_SOURCE_TYPE_STYLE_SCHEME,
			       "id", id, "name", name, NULL);

	return scheme;
}

/*
 * get_color_by_name:
 * @scheme: a #GtkSourceStyleScheme.
 * @name: color name to find.
 *
 * Returns: color which corresponds to @name in the @scheme.
 * Returned value is actual color string suitable for gdk_rgba_parse().
 * It may be @name or part of @name so copy it or something, if you need
 * it to stay around.
 *
 * Since: 2.0
 */
static const gchar *
get_color_by_name (GtkSourceStyleScheme *scheme,
		   const gchar          *name)
{
	const char *color = NULL;

	g_return_val_if_fail (name != NULL, NULL);

	if (name[0] == '#')
	{
		GdkRGBA dummy;

		if (gdk_rgba_parse (&dummy, name + 1))
		{
			color = name + 1;
		}
		else if (gdk_rgba_parse (&dummy, name))
		{
			color = name;
		}
		else
		{
			g_warning ("could not parse color '%s'", name);
		}
	}
	else
	{
		color = g_hash_table_lookup (scheme->priv->named_colors, name);

		if (color == NULL && scheme->priv->parent != NULL)
			color = get_color_by_name (scheme->priv->parent, name);

		if (color == NULL)
			g_warning ("no color named '%s'", name);
	}

	return color;
}

static GtkSourceStyle *
fix_style_colors (GtkSourceStyleScheme *scheme,
		  GtkSourceStyle       *real_style)
{
	GtkSourceStyle *style;
	guint i;
	struct {
		guint mask;
		guint offset;
	} attributes[] = {
		{ GTK_SOURCE_STYLE_USE_BACKGROUND, G_STRUCT_OFFSET (GtkSourceStyle, background) },
		{ GTK_SOURCE_STYLE_USE_FOREGROUND, G_STRUCT_OFFSET (GtkSourceStyle, foreground) },
		{ GTK_SOURCE_STYLE_USE_LINE_BACKGROUND, G_STRUCT_OFFSET (GtkSourceStyle, line_background) }
	};

	style = gtk_source_style_copy (real_style);

	for (i = 0; i < G_N_ELEMENTS (attributes); i++)
	{
		if (style->mask & attributes[i].mask)
		{
			const gchar **attr = G_STRUCT_MEMBER_P (style, attributes[i].offset);
			const gchar *color = get_color_by_name (scheme, *attr);

			if (color == NULL)
				/* warning is spit out in get_color_by_name,
				 * here we make sure style doesn't have NULL color */
				style->mask &= ~attributes[i].mask;
			else
				*attr = g_intern_string (color);
		}
	}

	return style;
}

/**
 * gtk_source_style_scheme_get_style:
 * @scheme: a #GtkSourceStyleScheme.
 * @style_id: id of the style to retrieve.
 *
 * Returns: (transfer none): style which corresponds to @style_id
 * in the @scheme, or %NULL when no style with this name found.
 * It is owned by @scheme and may not be unref'ed.
 *
 * Since: 2.0
 */
/*
 * It's a little weird because we have named colors: styles loaded from
 * scheme file can have "#red" or "blue", and we want to give out styles
 * which have nice colors suitable for gdk_color_parse(), so that GtkSourceStyle
 * foreground and background properties are the same as GtkTextTag's.
 * Yet we do need to preserve what we got from file in style schemes,
 * since there may be child schemes which may redefine colors or something,
 * so we can't translate colors when loading scheme.
 * So, defined_styles hash has named colors; styles returned with get_style()
 * have real colors.
 */
GtkSourceStyle *
gtk_source_style_scheme_get_style (GtkSourceStyleScheme *scheme,
				   const gchar          *style_id)
{
	GtkSourceStyle *style = NULL;
	GtkSourceStyle *real_style;

	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);
	g_return_val_if_fail (style_id != NULL, NULL);

	if (g_hash_table_lookup_extended (scheme->priv->style_cache, style_id,
					  NULL, (gpointer) &style))
		return style;

	real_style = g_hash_table_lookup (scheme->priv->defined_styles, style_id);

	if (real_style == NULL)
	{
		if (scheme->priv->parent != NULL)
			style = gtk_source_style_scheme_get_style (scheme->priv->parent,
								   style_id);
		if (style != NULL)
			g_object_ref (style);
	}
	else
	{
		style = fix_style_colors (scheme, real_style);
	}

	g_hash_table_insert (scheme->priv->style_cache,
			     g_strdup (style_id),
			     style);

	return style;
}

#if 0
/**
 * gtk_source_style_scheme_set_style:
 * @scheme: a #GtkSourceStyleScheme.
 * @name: style name.
 * @style: style to set or %NULL.
 *
 * Since: 2.0
 */
void
gtk_source_style_scheme_set_style (GtkSourceStyleScheme *scheme,
				   const gchar          *name,
				   const GtkSourceStyle *style)
{
	g_return_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme));
	g_return_if_fail (name != NULL);

	if (style != NULL)
		g_hash_table_insert (scheme->priv->styles, g_strdup (name),
				     gtk_source_style_copy (style));
	else
		g_hash_table_remove (scheme->priv->styles, name);
}
#endif

GtkSourceStyle *
_gtk_source_style_scheme_get_matching_brackets_style (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);

	return gtk_source_style_scheme_get_style (scheme, STYLE_BRACKET_MATCH);
}

GtkSourceStyle *
_gtk_source_style_scheme_get_right_margin_style (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);

	return gtk_source_style_scheme_get_style (scheme, STYLE_RIGHT_MARGIN);
}

GtkSourceStyle *
_gtk_source_style_scheme_get_draw_spaces_style (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);

	return gtk_source_style_scheme_get_style (scheme, STYLE_DRAW_SPACES);
}

static gboolean
get_color (GtkSourceStyle *style,
	   gboolean        foreground,
	   GdkRGBA        *dest)
{
	const gchar *color;
	guint mask;

	if (style != NULL)
	{
		if (foreground)
		{
			color = style->foreground;
			mask = GTK_SOURCE_STYLE_USE_FOREGROUND;
		}
		else
		{
			color = style->background;
			mask = GTK_SOURCE_STYLE_USE_BACKGROUND;
		}

		if (style->mask & mask)
		{
			if (color == NULL || !gdk_rgba_parse (dest, color))
			{
				g_warning ("%s: invalid color '%s'", G_STRLOC,
					   color != NULL ? color : "(null)");
				return FALSE;
			}

			return TRUE;
		}
	}

	return FALSE;
}

/*
 * Returns TRUE if the style for current-line set in the scheme
 */
gboolean
_gtk_source_style_scheme_get_current_line_color (GtkSourceStyleScheme *scheme,
						 GdkRGBA              *color)
{
	GtkSourceStyle *style;

	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), FALSE);
	g_return_val_if_fail (color != NULL, FALSE);

	style = gtk_source_style_scheme_get_style (scheme, STYLE_CURRENT_LINE);

	return get_color (style, FALSE, color);
}

static void
set_line_numbers_style (GtkWidget      *widget,
			GtkSourceStyle *style)
{
	GdkRGBA *fg_ptr = NULL;
	GdkRGBA *bg_ptr = NULL;
	GdkRGBA fg;
	GdkRGBA bg;
	GtkStateFlags flags;

	if (get_color (style, TRUE, &fg))
		fg_ptr = &fg;

	if (get_color (style, FALSE, &bg))
		bg_ptr = &bg;

	/* Override the color no matter what the state is */
	flags = GTK_STATE_FLAG_NORMAL;

	gtk_widget_override_color (widget, flags, fg_ptr);
	gtk_widget_override_background_color (widget, flags, bg_ptr);
}

static void
update_cursor_colors (GtkWidget      *widget,
		      GtkSourceStyle *style_primary,
		      GtkSourceStyle *style_secondary)
{
	GdkRGBA primary_color;
	GdkRGBA secondary_color;
	GdkRGBA *primary = NULL;
	GdkRGBA *secondary = NULL;

	if (get_color (style_primary, TRUE, &primary_color))
		primary = &primary_color;

	if (get_color (style_secondary, TRUE, &secondary_color))
		secondary = &secondary_color;

	if (primary != NULL && secondary == NULL)
	{
		GtkStyleContext *context;

		context = gtk_widget_get_style_context (widget);
		gtk_style_context_get_background_color (context, GTK_STATE_FLAG_NORMAL,
		                                        &secondary_color);

		/* shade the secondary cursor */
		secondary_color.red *= 0.5;
		secondary_color.green *= 0.5;
		secondary_color.blue *= 0.5;

		secondary = &secondary_color;
	}

	if (primary != NULL)
		gtk_widget_override_cursor (widget, primary, secondary);
	else
		gtk_widget_override_cursor (widget, NULL, NULL);
}

/**
 * _gtk_source_style_scheme_apply:
 * @scheme:: a #GtkSourceStyleScheme.
 * @widget: a #GtkWidget to apply styles to.
 *
 * Sets text colors from @scheme in the @widget.
 *
 * Since: 2.0
 */
void
_gtk_source_style_scheme_apply (GtkSourceStyleScheme *scheme,
				GtkWidget            *widget)
{
	GtkSourceStyle *style, *style2;
	GtkStyleContext *context;

	g_return_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme));
	g_return_if_fail (GTK_IS_WIDGET (widget));

	/* we need to translate some of the style scheme properties in a CSS override */
	context = gtk_widget_get_style_context (GTK_WIDGET (widget));
	gtk_style_context_add_provider (context,
	                                GTK_STYLE_PROVIDER (scheme->priv->css),
	                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	style = gtk_source_style_scheme_get_style (scheme, STYLE_LINE_NUMBERS);
	set_line_numbers_style (widget, style);

	style = gtk_source_style_scheme_get_style (scheme, STYLE_CURSOR);
	style2 = gtk_source_style_scheme_get_style (scheme, STYLE_SECONDARY_CURSOR);
	update_cursor_colors (widget, style, style2);
}

/**
 * _gtk_source_style_scheme_unapply:
 * @scheme: (allow-none): a #GtkSourceStyleScheme or %NULL.
 * @widget: a #GtkWidget to unapply styles to.
 *
 * Removes the style from @scheme in the @widget.
 *
 * Since: 3.0
 */
void
_gtk_source_style_scheme_unapply (GtkSourceStyleScheme *scheme,
                                  GtkWidget            *widget)
{
	GtkStyleContext *context;

	g_return_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme));
	g_return_if_fail (GTK_IS_WIDGET (widget));

	context = gtk_widget_get_style_context (GTK_WIDGET (widget));
	gtk_style_context_remove_provider (context,
	                                   GTK_STYLE_PROVIDER (scheme->priv->css));
	set_line_numbers_style (widget, NULL);
	update_cursor_colors (widget, NULL, NULL);
}

/* --- PARSER ---------------------------------------------------------------- */

#define ERROR_QUARK (g_quark_from_static_string ("gtk-source-style-scheme-parser-error"))

static void
get_css_color_style (GtkSourceStyle *style,
                     gchar         **bg,
                     gchar         **text)
{
	GdkRGBA color;

	if (get_color (style, FALSE, &color))
	{
		gchar *bg_color;
		bg_color = gdk_rgba_to_string (&color);
		*bg = g_strdup_printf ("%s: %s;\n", "background-color", bg_color);
		g_free (bg_color);
	}
	else
	{
		*bg = NULL;
	}

	if (get_color (style, TRUE, &color))
	{
		gchar *text_color;
		text_color = gdk_rgba_to_string (&color);
		*text = g_strdup_printf ("%s: %s;\n", "color", text_color);
		g_free (text_color);
	}
	else
	{
		*text = NULL;
	}
}

static void
append_css_style (GString        *string,
                  GtkSourceStyle *style,
                  const gchar    *state)
{
	gchar *bg, *text;
	const gchar css_style[] =
		".view%s {\n"
		"	%s"
		"	%s"
		"}\n";

	get_css_color_style (style, &bg, &text);

	if (bg || text)
	{
		g_string_append_printf (string, css_style, state,
		                        bg != NULL ? bg : "",
		                        text != NULL ? text : "");

		g_free (bg);
		g_free (text);
	}
}

static void
generate_css_style (GtkSourceStyleScheme *scheme)
{
	GString *final_style;
	GtkSourceStyle *style, *style2;

	final_style = g_string_new ("");

	style = gtk_source_style_scheme_get_style (scheme, STYLE_TEXT);
	append_css_style (final_style, style, "");

	style = gtk_source_style_scheme_get_style (scheme, STYLE_SELECTED);
	append_css_style (final_style, style, ":selected:focused");

	style2 = gtk_source_style_scheme_get_style (scheme, STYLE_SELECTED_UNFOCUSED);
	if (style2 == NULL)
		style2 = style;
	append_css_style (final_style, style2, ":selected");

	if (*final_style->str != '\0')
	{
		GError *error = NULL;

		if (!gtk_css_provider_load_from_data (scheme->priv->css, final_style->str,
		                                      final_style->len, &error))
		{
			g_warning ("%s", error->message);
			g_error_free (error);
		}
	}

	g_string_free (final_style, TRUE);
}

static void
get_bool (xmlNode    *node,
	  const char *propname,
	  guint      *mask,
	  guint       mask_value,
	  gboolean   *value)
{
	xmlChar *tmp = xmlGetProp (node, BAD_CAST propname);

	if (tmp != NULL)
	{
		*mask |= mask_value;
		*value = g_ascii_strcasecmp ((char*) tmp, "true") == 0 ||
			 g_ascii_strcasecmp ((char*) tmp, "yes") == 0 ||
			 g_ascii_strcasecmp ((char*) tmp, "1") == 0;
	}

	xmlFree (tmp);
}

static gboolean
parse_style (GtkSourceStyleScheme *scheme,
	     xmlNode              *node,
	     gchar               **style_name_p,
	     GtkSourceStyle      **style_p,
	     GError              **error)
{
	GtkSourceStyle *use_style = NULL;
	GtkSourceStyle *result = NULL;
	xmlChar *fg = NULL;
	xmlChar *bg = NULL;
	xmlChar *line_bg = NULL;
	gchar *style_name = NULL;
	guint mask = 0;
	gboolean bold = FALSE;
	gboolean italic = FALSE;
	gboolean underline = FALSE;
	gboolean strikethrough = FALSE;
	xmlChar *tmp;

	tmp = xmlGetProp (node, BAD_CAST "name");
	if (tmp == NULL)
	{
		g_set_error (error, ERROR_QUARK, 0, "name attribute missing");
		return FALSE;
	}
	style_name = g_strdup ((char*) tmp);
	xmlFree (tmp);

	tmp = xmlGetProp (node, BAD_CAST "use-style");
	if (tmp != NULL)
	{
		if (use_style != NULL)
		{
			g_set_error (error, ERROR_QUARK, 0,
				     "in style '%s': duplicated use-style attribute",
				     style_name);
			g_free (style_name);
			g_object_unref (use_style);
			return FALSE;
		}

		use_style = gtk_source_style_scheme_get_style (scheme, (char*) tmp);

		if (use_style == NULL)
		{
			g_set_error (error, ERROR_QUARK, 0,
				     "in style '%s': unknown style '%s'",
				     style_name, tmp);
			g_free (style_name);
			return FALSE;
		}

		g_object_ref (use_style);
	}
	xmlFree (tmp);

	fg = xmlGetProp (node, BAD_CAST "foreground");
	bg = xmlGetProp (node, BAD_CAST "background");
	line_bg = xmlGetProp (node, BAD_CAST "line-background");
	get_bool (node, "italic", &mask, GTK_SOURCE_STYLE_USE_ITALIC, &italic);
	get_bool (node, "bold", &mask, GTK_SOURCE_STYLE_USE_BOLD, &bold);
	get_bool (node, "underline", &mask, GTK_SOURCE_STYLE_USE_UNDERLINE, &underline);
	get_bool (node, "strikethrough", &mask, GTK_SOURCE_STYLE_USE_STRIKETHROUGH, &strikethrough);

	if (use_style)
	{
		if (fg != NULL || bg != NULL || line_bg != NULL || mask != 0)
		{
			g_set_error (error, ERROR_QUARK, 0,
				     "in style '%s': style attributes used along with use-style",
				     style_name);
			g_object_unref (use_style);
			g_free (style_name);
			xmlFree (fg);
			xmlFree (bg);
			xmlFree (line_bg);
			return FALSE;
		}

		result = use_style;
		use_style = NULL;
	}
	else
	{
		result = g_object_new (GTK_SOURCE_TYPE_STYLE, NULL);

		result->mask = mask;
		result->bold = bold;
		result->italic = italic;
		result->underline = underline;
		result->strikethrough = strikethrough;

		if (fg != NULL)
		{
			result->foreground = g_intern_string ((char*) fg);
			result->mask |= GTK_SOURCE_STYLE_USE_FOREGROUND;
		}

		if (bg != NULL)
		{
			result->background = g_intern_string ((char*) bg);
			result->mask |= GTK_SOURCE_STYLE_USE_BACKGROUND;
		}

		if (line_bg != NULL)
		{
			result->line_background = g_intern_string ((char*) line_bg);
			result->mask |= GTK_SOURCE_STYLE_USE_LINE_BACKGROUND;
		}
	}

	*style_p = result;
	*style_name_p = style_name;

	xmlFree (line_bg);
	xmlFree (bg);
	xmlFree (fg);

	return TRUE;
}

static gboolean
parse_color (GtkSourceStyleScheme *scheme,
	     xmlNode              *node,
	     GError              **error)
{
	xmlChar *name, *value;
	gboolean result = FALSE;

	name = xmlGetProp (node, BAD_CAST "name");
	value = xmlGetProp (node, BAD_CAST "value");

	if (name == NULL || name[0] == 0)
		g_set_error (error, ERROR_QUARK, 0, "name attribute missing in 'color' tag");
	else if (value == NULL)
		g_set_error (error, ERROR_QUARK, 0, "value attribute missing in 'color' tag");
	else if (value[0] != '#' || value[1] == 0)
		g_set_error (error, ERROR_QUARK, 0, "value in 'color' tag is not of the form '#RGB' or '#name'");
	else if (g_hash_table_lookup (scheme->priv->named_colors, name) != NULL)
		g_set_error (error, ERROR_QUARK, 0, "duplicated color '%s'", name);
	else
		result = TRUE;

	if (result)
		g_hash_table_insert (scheme->priv->named_colors,
				     g_strdup ((char *) name),
				     g_strdup ((char *) value));

	xmlFree (value);
	xmlFree (name);

	return result;
}

static gboolean
parse_style_scheme_child (GtkSourceStyleScheme *scheme,
			  xmlNode              *node,
			  GError              **error)
{
	if (strcmp ((char*) node->name, "style") == 0)
	{
		GtkSourceStyle *style;
		gchar *style_name;

		if (!parse_style (scheme, node, &style_name, &style, error))
			return FALSE;

		g_hash_table_insert (scheme->priv->defined_styles, style_name, style);
	}
	else if (strcmp ((char*) node->name, "color") == 0)
	{
		if (!parse_color (scheme, node, error))
			return FALSE;
	}
	else if (strcmp ((char*) node->name, "author") == 0)
	{
		xmlChar *tmp = xmlNodeGetContent (node);
		if (scheme->priv->authors == NULL)
			scheme->priv->authors = g_ptr_array_new ();

		g_ptr_array_add (scheme->priv->authors, g_strdup ((char*) tmp));

		xmlFree (tmp);
	}
	else if (strcmp ((char*) node->name, "description") == 0)
	{
		xmlChar *tmp = xmlNodeGetContent (node);
		scheme->priv->description = g_strdup ((char*) tmp);
		xmlFree (tmp);
	}
	else if (strcmp ((char*) node->name, "_description") == 0)
	{
		xmlChar *tmp = xmlNodeGetContent (node);
		scheme->priv->description = g_strdup (_((char*) tmp));
		xmlFree (tmp);
	}
	else
	{
		g_set_error (error, ERROR_QUARK, 0, "unknown node '%s'", node->name);
		return FALSE;
	}

	return TRUE;
}

static void
parse_style_scheme_element (GtkSourceStyleScheme *scheme,
			    xmlNode              *scheme_node,
			    GError              **error)
{
	xmlChar *tmp;
	xmlNode *node;

	if (strcmp ((char*) scheme_node->name, "style-scheme") != 0)
	{
		g_set_error (error, ERROR_QUARK, 0,
			     "unexpected element '%s'",
			     (char*) scheme_node->name);
		return;
	}

	tmp = xmlGetProp (scheme_node, BAD_CAST "version");
	if (tmp == NULL)
	{
		g_set_error (error, ERROR_QUARK, 0, "missing 'version' attribute");
		return;
	}
	if (strcmp ((char*) tmp, STYLE_SCHEME_VERSION) != 0)
	{
		g_set_error (error, ERROR_QUARK, 0, "unsupported version '%s'", (char*) tmp);
		xmlFree (tmp);
		return;
	}
	xmlFree (tmp);

	tmp = xmlGetProp (scheme_node, BAD_CAST "id");
	if (tmp == NULL)
	{
		g_set_error (error, ERROR_QUARK, 0, "missing 'id' attribute");
		return;
	}
	scheme->priv->id = g_strdup ((char*) tmp);
	xmlFree (tmp);

	tmp = xmlGetProp (scheme_node, BAD_CAST "_name");
	if (tmp != NULL)
		scheme->priv->name = g_strdup (_((char*) tmp));
	else if ((tmp = xmlGetProp (scheme_node, BAD_CAST "name")) != NULL)
		scheme->priv->name = g_strdup ((char*) tmp);
	else
	{
		g_set_error (error, ERROR_QUARK, 0, "missing 'name' attribute");
		return;
	}
	xmlFree (tmp);

	tmp = xmlGetProp (scheme_node, BAD_CAST "parent-scheme");
	if (tmp != NULL)
		scheme->priv->parent_id = g_strdup ((char*) tmp);
	xmlFree (tmp);

	for (node = scheme_node->children; node != NULL; node = node->next)
		if (node->type == XML_ELEMENT_NODE)
			if (!parse_style_scheme_child (scheme, node, error))
				return;

	/* NULL-terminate the array of authors */
	if (scheme->priv->authors != NULL)
		g_ptr_array_add (scheme->priv->authors, NULL);
}

/**
 * _gtk_source_style_scheme_new_from_file:
 * @filename: file to parse.
 *
 * Returns: new #GtkSourceStyleScheme created from file, or
 * %NULL on error.
 *
 * Since: 2.0
 */
GtkSourceStyleScheme *
_gtk_source_style_scheme_new_from_file (const gchar *filename)
{
	GtkSourceStyleScheme *scheme;
	gchar *text;
	gsize text_len;
	xmlDoc *doc;
	xmlNode *node;
	GError *error = NULL;

	g_return_val_if_fail (filename != NULL, NULL);

	if (!g_file_get_contents (filename, &text, &text_len, &error))
	{
		gchar *filename_utf8 = g_filename_display_name (filename);
		g_warning ("could not load style scheme file '%s': %s",
			   filename_utf8, error->message);
		g_free (filename_utf8);
		g_error_free (error);
		return NULL;
	}

	doc = xmlParseMemory (text, text_len);

	if (!doc)
	{
		gchar *filename_utf8 = g_filename_display_name (filename);
		g_warning ("could not parse scheme file '%s'", filename_utf8);
		g_free (filename_utf8);
		g_free (text);
		return NULL;
	}

	node = xmlDocGetRootElement (doc);

	if (node == NULL)
	{
		gchar *filename_utf8 = g_filename_display_name (filename);
		g_warning ("could not load scheme file '%s': empty document", filename_utf8);
		g_free (filename_utf8);
		xmlFreeDoc (doc);
		g_free (text);
		return NULL;
	}

	scheme = g_object_new (GTK_SOURCE_TYPE_STYLE_SCHEME, NULL);
	scheme->priv->filename = g_strdup (filename);

	parse_style_scheme_element (scheme, node, &error);

	if (error != NULL)
	{
		gchar *filename_utf8 = g_filename_display_name (filename);
		g_warning ("could not load style scheme file '%s': %s",
			   filename_utf8, error->message);
		g_free (filename_utf8);
		g_error_free (error);
		g_object_unref (scheme);
		scheme = NULL;
	}
	else
	{
		/* css style part */
		generate_css_style (scheme);
	}

	xmlFreeDoc (doc);
	g_free (text);

	return scheme;
}

/**
 * _gtk_source_style_scheme_get_parent_id:
 * @scheme: a #GtkSourceStyleScheme.
 *
 * Returns: parent style scheme id or %NULL.
 *
 * Since: 2.0
 */
const gchar *
_gtk_source_style_scheme_get_parent_id (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);

	return scheme->priv->parent_id;
}

/**
 * _gtk_source_style_scheme_set_parent:
 * @scheme: a #GtkSourceStyleScheme.
 * @parent_scheme: parent #GtkSourceStyleScheme for @scheme.
 *
 * Sets @parent_scheme as parent scheme for @scheme, @scheme will
 * look for styles in @parent_scheme if it doesn't have style set
 * for given name.
 *
 * Since: 2.0
 */
void
_gtk_source_style_scheme_set_parent (GtkSourceStyleScheme *scheme,
				     GtkSourceStyleScheme *parent_scheme)
{
	g_return_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme));
	g_return_if_fail (parent_scheme == NULL || GTK_SOURCE_IS_STYLE_SCHEME (parent_scheme));

	if (scheme->priv->parent != NULL)
		g_object_unref (scheme->priv->parent);
	if (parent_scheme)
		g_object_ref (parent_scheme);
	scheme->priv->parent = parent_scheme;
}

/**
 * _gtk_source_style_scheme_get_default:
 *
 * Returns: default style scheme to be used when user didn't set
 * style scheme explicitly.
 *
 * Since: 2.0
 */
GtkSourceStyleScheme *
_gtk_source_style_scheme_get_default (void)
{
	GtkSourceStyleSchemeManager *manager;

	manager = gtk_source_style_scheme_manager_get_default ();

	return gtk_source_style_scheme_manager_get_scheme (manager,
							   DEFAULT_STYLE_SCHEME);
}
