/* vim:set et sts=4: */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <errno.h>
#include "m17nutil.h"

#define N_(text) text

static MConverter *utf8_converter = NULL;

#define DEFAULT_XML (PKGDATADIR "/default.xml")

typedef enum {
    ENGINE_CONFIG_RANK_MASK = 1 << 0,
    ENGINE_CONFIG_SYMBOL_MASK = 1 << 1,
    ENGINE_CONFIG_LONGNAME_MASK = 1 << 2,
    ENGINE_CONFIG_LAYOUT_MASK = 1 << 3,
    ENGINE_CONFIG_PREEDIT_HIGHLIGHT_MASK = 1 << 4
} EngineConfigMask;

struct _EngineConfigNode {
    gchar *name;
    EngineConfigMask mask;
    IBusM17NEngineConfig config;
};
typedef struct _EngineConfigNode EngineConfigNode;

static GSList *config_list = NULL;

void
ibus_m17n_init_common (void)
{
    M17N_INIT ();

    if (utf8_converter == NULL) {
        utf8_converter = mconv_buffer_converter (Mcoding_utf_8, NULL, 0);
    }
}

gchar *
ibus_m17n_mtext_to_utf8 (MText *text)
{
    gint bufsize;
    gchar *buf;

    if (text == NULL)
        return NULL;

    mconv_reset_converter (utf8_converter);

    bufsize = (mtext_len (text) + 1) * 6;
    buf = (gchar *) g_malloc (bufsize);

    mconv_rebind_buffer (utf8_converter,
                         (const unsigned char *) buf,
                         bufsize);
    mconv_encode (utf8_converter, text);

    buf [utf8_converter->nbytes] = 0;

    return buf;
}

gunichar *
ibus_m17n_mtext_to_ucs4 (MText *text, glong *nchars)
{
    glong bufsize;
    gchar *buf;
    gunichar *ucs;

    if (text == NULL)
        return NULL;

    mconv_reset_converter (utf8_converter);

    bufsize = (mtext_len (text) + 1) * 6;
    buf = (gchar *) g_malloc (bufsize);

    mconv_rebind_buffer (utf8_converter,
                         (const unsigned char *) buf,
                         bufsize);
    if (mconv_encode (utf8_converter, text) < 0) {
        g_free (buf);
        return NULL;
    }
    ucs = g_utf8_to_ucs4_fast (buf, bufsize, nchars);
    g_free (buf);
    return ucs;
}

guint
ibus_m17n_parse_color (const gchar *hex)
{
    guint color;

    if (!hex || *hex != '#')
        return (guint)-1;

    errno = 0;
    color = strtoul (&hex[1], NULL, 16);
    if ((errno == ERANGE && color == ULONG_MAX)
        || (errno != 0 && color == 0))
        return (guint)-1;
    return color;
}

static IBusEngineDesc *
ibus_m17n_engine_new (MSymbol  lang,
                      MSymbol  name,
                      MText   *title,
                      MText   *icon,
                      MText   *desc,
                      IBusM17NEngineConfig *config)
{
    IBusEngineDesc *engine;
    gchar *engine_name;
    gchar *engine_longname;
    gchar *engine_title;
    gchar *engine_icon;
    gchar *engine_desc;
    gchar *engine_setup;

    engine_name = g_strdup_printf ("m17n:%s:%s", msymbol_name (lang), msymbol_name (name));

    engine_longname = g_strdup_printf ("%s (m17n)", msymbol_name (name));
    engine_title = ibus_m17n_mtext_to_utf8 (title);
    engine_icon = ibus_m17n_mtext_to_utf8 (icon);
    engine_desc = ibus_m17n_mtext_to_utf8 (desc);
    engine_setup = g_strdup_printf ("%s/ibus-setup-m17n --name %s",
                                    LIBEXECDIR, engine_name);

    engine = ibus_engine_desc_new_varargs ("name",        engine_name,
                                           "longname",    config->longname ? config->longname : engine_longname,
                                           "description", engine_desc ? engine_desc : "",
                                           "language",    msymbol_name (lang),
                                           "license",     "GPL",
                                           "icon",        engine_icon ? engine_icon : "",
                                           "layout",      config->layout ? config->layout : "default",
                                           "rank",        config->rank,
                                           "symbol",      config->symbol ? config->symbol : "",
                                           "setup",       engine_setup,
                                           NULL);

    g_free (engine_name);
    g_free (engine_longname);
    g_free (engine_title);
    g_free (engine_icon);
    g_free (engine_desc);
    g_free (engine_setup);

    return engine;
}

#ifndef HAVE_MINPUT_LIST
MPlist *minput_list (MSymbol language);
#endif  /* !HAVE_MINPUT_LIST */

GList *
ibus_m17n_list_engines (void)
{
    GList *engines = NULL;
    MPlist *imlist;
    MPlist *elm;

    imlist = minput_list (Mnil);
    for (elm = imlist; elm && mplist_key(elm) != Mnil; elm = mplist_next(elm)) {
        MSymbol lang;
        MSymbol name;
        MSymbol sane;
        MText *title = NULL;
        MText *icon = NULL;
        MText *desc = NULL;
        MPlist *l;
        gchar *engine_name;
        IBusM17NEngineConfig *config;

        l = mplist_value (elm);
        lang = mplist_value (l);
        l = mplist_next (l);
        name = mplist_value (l);
        l = mplist_next (l);
        sane = mplist_value (l);

        if (sane == Mt) {
            /* ignore input-method explicitly blacklisted in default.xml */
            engine_name = g_strdup_printf ("m17n:%s:%s", msymbol_name (lang), msymbol_name (name));
            config = ibus_m17n_get_engine_config (engine_name);
            if (config == NULL) {
                g_warning ("can't load config for %s", engine_name);
                g_free (engine_name);
                continue;
            }
            if (config->rank < 0) {
                g_log ("ibus-m17n",
                       G_LOG_LEVEL_MESSAGE,
                       "skipped %s since its rank is lower than 0",
                       engine_name);
                g_free (engine_name);
                continue;
            }
            g_free (engine_name);

            l = minput_get_variable (lang, name, msymbol ("candidates-charset"));
            if (l) {
                /* check candidates encoding */
                MPlist *sl;
                MSymbol varcharset;

                sl = mplist_value (l);
                /* L = (VAR-NAME DESCRIPTION 'nil' VALUE) */
                sl = mplist_next (sl);
                sl = mplist_next (sl);
                sl = mplist_next (sl);
                varcharset = mplist_value (sl);

                if (varcharset != Mcoding_utf_8 ||
                    varcharset != Mcoding_utf_8_full) {
                    /*
                    g_debug ("%s != %s or %s",
                                msymbol_name (varcharset),
                                msymbol_name (Mcoding_utf_8),
                                msymbol_name (Mcoding_utf_8_full));
                    */
                    continue;
                }

            }
            if (l)
                m17n_object_unref (l);

            desc = minput_get_description (lang, name);
            l = minput_get_title_icon (lang, name);
            if (l && mplist_key (l) == Mtext) {
                title = mplist_value (l);
            }

            MPlist *n = mplist_next (l);
            if (n && mplist_key (n) == Mtext) {
                icon = mplist_value (n);
            }

            engines = g_list_append (engines, ibus_m17n_engine_new (lang, name, title, icon, desc, config));

            if (desc)
                m17n_object_unref (desc);
            m17n_object_unref (l);
        }
    }

    if (imlist) {
        m17n_object_unref (imlist);
    }

    return engines;
}

IBusM17NEngineConfig *
ibus_m17n_get_engine_config (const gchar *engine_name)
{
    IBusM17NEngineConfig *config = g_slice_new0 (IBusM17NEngineConfig);
    GSList *p;

    for (p = config_list; p != NULL; p = p->next) {
        EngineConfigNode *cnode = p->data;

        if (g_pattern_match_simple (cnode->name, engine_name)) {
            if (cnode->mask & ENGINE_CONFIG_RANK_MASK)
                config->rank = cnode->config.rank;
            if (cnode->mask & ENGINE_CONFIG_SYMBOL_MASK)
                config->symbol = cnode->config.symbol;
            if (cnode->mask & ENGINE_CONFIG_LONGNAME_MASK)
                config->longname = cnode->config.longname;
            if (cnode->mask & ENGINE_CONFIG_LAYOUT_MASK)
                config->layout = cnode->config.layout;
            if (cnode->mask & ENGINE_CONFIG_PREEDIT_HIGHLIGHT_MASK)
                config->preedit_highlight = cnode->config.preedit_highlight;
        }
    }
    return config;
}

void
ibus_m17n_engine_config_free (IBusM17NEngineConfig *config)
{
    g_slice_free (IBusM17NEngineConfig, config);
}

static gboolean
ibus_m17n_engine_config_parse_xml_node (EngineConfigNode *cnode,
                                        XMLNode          *node)
{
    GList *p;

    for (p = node->sub_nodes; p != NULL; p = p->next) {
        XMLNode *sub_node = (XMLNode *) p->data;

        if (g_strcmp0 (sub_node->name, "name") == 0) {
            g_free (cnode->name);
            cnode->name = g_strdup (sub_node->text);
            continue;
        }
        if (g_strcmp0 (sub_node->name , "rank") == 0) {
            cnode->config.rank = atoi (sub_node->text);
            cnode->mask |= ENGINE_CONFIG_RANK_MASK;
            continue;
        }
        if (g_strcmp0 (sub_node->name , "symbol") == 0) {
            cnode->config.symbol = g_strdup (sub_node->text);
            cnode->mask |= ENGINE_CONFIG_SYMBOL_MASK;
            continue;
        }
        if (g_strcmp0 (sub_node->name , "longname") == 0) {
            cnode->config.longname = g_strdup (sub_node->text);
            cnode->mask |= ENGINE_CONFIG_LONGNAME_MASK;
            continue;
        }
        if (g_strcmp0 (sub_node->name , "layout") == 0) {
            g_free (cnode->config.layout);
            cnode->config.layout = g_strdup (sub_node->text);
            cnode->mask |= ENGINE_CONFIG_LAYOUT_MASK;
            continue;
        }
        if (g_strcmp0 (sub_node->name , "preedit-highlight") == 0) {
            if (g_ascii_strcasecmp ("TRUE", sub_node->text) == 0)
                cnode->config.preedit_highlight = TRUE;
            else if (g_ascii_strcasecmp ("FALSE", sub_node->text) != 0)
                g_warning ("<%s> element contains invalid boolean value %s",
                           sub_node->name, sub_node->text);
            cnode->mask |= ENGINE_CONFIG_PREEDIT_HIGHLIGHT_MASK;
            continue;
        }
        g_warning ("<engine> element contains invalid element <%s>",
                   sub_node->name);
    }
    return TRUE;
}

IBusComponent *
ibus_m17n_get_component (void)
{
    GList *engines, *p;
    IBusComponent *component;
    XMLNode *node;
    const gchar *pkgdatadir;
    gchar *default_xml;

    component = ibus_component_new ("org.freedesktop.IBus.M17n",
                                    N_("M17N"),
                                    PACKAGE_VERSION,
                                    "GPL",
                                    "Peng Huang <shawn.p.huang@gmail.com>",
                                    "http://code.google.com/p/ibus/",
                                    "",
                                    PACKAGE_NAME);

    pkgdatadir = g_getenv ("IBUS_M17N_PKGDATADIR");
    if (pkgdatadir == NULL)
        pkgdatadir = PKGDATADIR;
    default_xml = g_build_filename (pkgdatadir, "default.xml", NULL);

    node = ibus_xml_parse_file (default_xml);
    if (node && g_strcmp0 (node->name, "engines") == 0) {
        for (p = node->sub_nodes; p != NULL; p = p->next) {
            XMLNode *sub_node = p->data;
            EngineConfigNode *cnode;

            if (g_strcmp0 (sub_node->name, "engine") != 0) {
                g_warning ("<engines> element contains invalid element <%s>",
                           sub_node->name);
                continue;
            }

            cnode = g_slice_new0 (EngineConfigNode);
            if (!ibus_m17n_engine_config_parse_xml_node (cnode, sub_node)) {
                g_slice_free (EngineConfigNode, cnode);
                continue;
            }
            config_list = g_slist_prepend (config_list, cnode);
        }
        config_list = g_slist_reverse (config_list);
    } else
        g_warning ("failed to parse %s", default_xml);
    if (node)
        ibus_xml_free (node);

    g_free (default_xml);

    engines = ibus_m17n_list_engines ();

    for (p = engines; p != NULL; p = p->next)
        ibus_component_add_engine (component, p->data);

    g_list_free (engines);

    return component;
}
