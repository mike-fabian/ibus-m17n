/* vim:set et sts=4: */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ibus.h>
#include <locale.h>
#include "m17nutil.h"

static void
test_output_component (void)
{
    IBusComponent *component;
    GString *output;

    component = ibus_m17n_get_component ();

    output = g_string_new ("");

    ibus_component_output (component, output, 1);

    g_debug ("\n%s", output->str);

    g_string_free (output, TRUE);
    g_object_unref (component);
}

static void
test_engine_config (void)
{
    IBusM17NEngineConfig *config;

    config = ibus_m17n_get_engine_config ("m17n:non:exsistent");
    g_assert_cmpint (config->rank, ==, 0);
    g_assert_cmpint (config->preedit_highlight, ==, 0);
    ibus_m17n_engine_config_free (config);

    config = ibus_m17n_get_engine_config ("m17n:si:wijesekara");
    g_assert_cmpint (config->rank, ==, 2);
    g_assert_cmpint (config->preedit_highlight, ==, 0);
    ibus_m17n_engine_config_free (config);

    config = ibus_m17n_get_engine_config ("m17n:si:phonetic-dynamic");
    g_assert_cmpint (config->rank, ==, 1);
    g_assert_cmpint (config->preedit_highlight, ==, 0);
    ibus_m17n_engine_config_free (config);

    config = ibus_m17n_get_engine_config ("m17n:si:samanala");
    g_assert_cmpint (config->rank, ==, 0);
    g_assert_cmpint (config->preedit_highlight, ==, 0);
    ibus_m17n_engine_config_free (config);
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");
    ibus_init ();
    ibus_m17n_init_common ();

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/test-m17n/output-component", test_output_component);
    g_test_add_func ("/test-m17n/engine-config", test_engine_config);

    return g_test_run ();
}
