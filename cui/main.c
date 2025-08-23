#define _GNU_SOURCE

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <wchar.h>
#include <locale.h>
#include <errno.h>
#include <unistd.h>
#include <curses.h>
#include <dialog.h>
#include "libdevcheck.h"
#include "device.h"
#include "utils.h"
#include "procedure.h"
#include "vis.h"
#include "ncurses_convenience.h"
#include "render.h"
#include "ui_mutual.h"

// Forward declaration
void log_cb(void *priv, enum DC_LogLevel level, const char* fmt, va_list vl);
void register_procedures(void);  // Register erase and other procedures

static int global_init(void);
static void global_fini(void);
static DC_Dev *menu_choose_device(DC_DevList *devlist);
static DC_Procedure *menu_choose_procedure(DC_Dev *dev);

static int ask_option_value(DC_Procedure *act, DC_OptionSetting *setting, DC_ProcedureOption *option) {
    int r;
    char *suggested_value = setting->value;
    char entered_value[200];
    const char *param_type_str;
    char *config_supplied_value;
    char *config_suggested_value;
    char config_search_command[200];

    snprintf(config_search_command, sizeof(config_search_command),
            "grep ^%s.%s= ~/.xhddrc 2>/dev/null | awk -F= '{print $2}' | tr -d '\\n'", act->name, option->name);
    config_supplied_value = cmd_output(config_search_command);
    if (config_supplied_value) {
        setting->value = config_supplied_value;
        return 0;
    }

    snprintf(config_search_command, sizeof(config_search_command),
            "grep ^%s.%s.suggest= ~/.xhddrc 2>/dev/null | awk -F= '{print $2}' | tr -d '\\n'", act->name, option->name);
    config_suggested_value = cmd_output(config_search_command);
    if (config_suggested_value)
        suggested_value = config_suggested_value;

    switch (option->type) {
        case DC_ProcedureOptionType_eInt64:
            param_type_str = "numeric";
            break;
        case DC_ProcedureOptionType_eString:
            param_type_str = "string";
            break;
    }

    char prompt[500];
    snprintf(prompt, sizeof(prompt), "Please enter %s parameter: %s (%s)",
            param_type_str, option->name, option->help);

    dialog_vars.default_button = -1;  // Workaround
    dialog_vars.input_result = NULL;

    if (option->choices) {
        int nb_choices = 0;
        const char * choice;
        const char **choices_for_dialog = NULL;
        int items_table_cols = 2;
        for (nb_choices = 0; choice = option->choices[nb_choices]; nb_choices++) {
            choices_for_dialog = reallocarray(choices_for_dialog, items_table_cols * (nb_choices + 1), sizeof(char*));
            choices_for_dialog[items_table_cols * nb_choices + 0] = choice;
            choices_for_dialog[items_table_cols * nb_choices + 1] = !strcmp(choice, suggested_value) ? "on" : "off";
        }
        r = dialog_checklist("Input box", prompt, 0, 0, 0, nb_choices, (char **)choices_for_dialog, FLAG_RADIO);
    } else {
        r = dialog_inputbox("Input box", prompt, 0, 0, suggested_value, 0);
    }

    if (r != 0) {
        dialog_msgbox("Info", "Action cancelled", 0, 0, 1);
        return 1;
    }

    snprintf(entered_value, sizeof(entered_value), "%s", dialog_vars.input_result);
    if (entered_value[0] == '\0' || entered_value[0] == '\n')
        snprintf(entered_value, sizeof(entered_value), "%s", suggested_value);
    setting->value = strdup(entered_value);
    free(suggested_value);
    return 0;
}

int main() {
    int r;

    r = global_init();
    if (r) {
        fprintf(stderr, "init fail\n");
        return r;
    }

    // Register all procedures including erase
    register_procedures();

    // Get list of devices
    DC_DevList *devlist = dc_dev_list();
    assert(devlist);

    while (1) {
        // Draw menu of device choice
        DC_Dev *chosen_dev = menu_choose_device(devlist);
        if (!chosen_dev) break;

        // Draw procedures menu
        DC_Procedure *act = menu_choose_procedure(chosen_dev);
        if (!act) continue;

        if (act->flags & DC_PROC_FLAG_INVASIVE) {
            char *ask;
            r = asprintf(&ask, "This operation is invasive and may destroy data. Proceed on %s (%s)?",
                    chosen_dev->dev_fs_name, chosen_dev->model_str);
            assert(r != -1);
            dialog_vars.default_button = 1;  // Focus on "No"
            r = dialog_yesno("Confirmation", ask, 0, 0);
            free(ask);
            if (r) continue;

            if (chosen_dev->mounted) {
                dialog_vars.default_button = 1;
                r = dialog_yesno("Confirmation", "This disk is mounted. Are you really sure?", 0, 0);
                if (r) continue;
            }
        }

        DC_OptionSetting *option_set = calloc(act->options_num + 1, sizeof(DC_OptionSetting));
        for (int i = 0; i < act->options_num; i++) {
            option_set[i].name = act->options[i].name;
            r = act->suggest_default_value(chosen_dev, &option_set[i]);
            if (r) break;
            r = ask_option_value(act, &option_set[i], &act->options[i]);
            if (r) break;
        }
        if (r) continue;

        if (!strcmp(act->name, "copy")) {
            int uses_journal = 0;
            for (int i = 0; i < act->options_num; i++) {
                if (!strcmp(option_set[i].name, "use_journal")) {
                    uses_journal = 1;
                    break;
                }
            }
            if (uses_journal)
                dialog_msgbox("Info", "Please wait while operation journal is processed", 0, 0, 0);
        }

        clear_body();

        DC_ProcedureCtx *actctx;
        r = dc_procedure_open(act, chosen_dev, &actctx, option_set);
        if (r) {
            dialog_msgbox("Error", "Procedure init fail", 0, 0, 1);
            continue;
        }
        if (!act->perform) continue;

        DC_Renderer *renderer = !strcmp(act->name, "copy") ? dc_find_renderer("whole_space") : dc_find_renderer("sliding_window");
        render_procedure(actctx, renderer);
    }

    return 0;
}

static int global_init(void) {
    int r;
    setlocale(LC_ALL, "");
    initscr();
    init_dialog(stdin, stdout);
    dialog_vars.item_help = 0;

    start_color();
    init_my_colors();
    noecho();
    cbreak();
    scrollok(stdscr, FALSE);
    keypad(stdscr, TRUE);

    clear_body();

    r = dc_init();
    assert(!r);
    RENDERER_REGISTER(sliding_window);
    RENDERER_REGISTER(whole_space);
    dc_log_set_callback(log_cb, NULL);

    r = atexit(global_fini);
    assert(r == 0);
    return 0;
}

static void global_fini(void) {
    clear();
    endwin();
}

static DC_Dev *menu_choose_device(DC_DevList *devlist) {
    int devs_num = dc_dev_list_size(devlist);
    if (devs_num == 0) {
        dialog_msgbox("Info", "No devices found", 0, 0, 1);
        return NULL;
    }

    char *items[2 * devs_num];
    for (int i = 0; i < devs_num; i++) {
        DC_Dev *dev = dc_dev_list_get_entry(devlist, i);
        char dev_descr_buf[80];
        ui_dev_descr_format(dev_descr_buf, sizeof(dev_descr_buf), dev);
        items[2*i] = dev->dev_fs_name;
        items[2*i+1] = strdup(dev_descr_buf);
    }

    clear_body();
    dialog_vars.no_items = 0;
    dialog_vars.item_help = 0;
    dialog_vars.input_result = NULL;
    dialog_vars.default_button = 0;
    int ret = dialog_menu("Choose device", "", 0, 0, 0, devs_num, items);

    for (int i = 0; i < devs_num; i++)
        free(items[2*i+1]);

    if (ret != 0) return NULL;

    for (int i = 0; i < devs_num; i++) {
        DC_Dev *dev = dc_dev_list_get_entry(devlist, i);
        if (!strcmp(dev->dev_fs_name, dialog_vars.input_result))
            return dev;
    }
    assert(0);
    return NULL;
}

static DC_Procedure *menu_choose_procedure(DC_Dev *dev) {
    (void)dev;
    int nb_procedures = dc_get_nb_procedures();
    const char *items[nb_procedures];
    DC_Procedure *procedures[nb_procedures];
    int nb_items = 0;
    DC_Procedure *procedure = NULL;

    while ((procedure = dc_get_next_procedure(procedure))) {
        if (!dev->ata_capable && (procedure->flags & DC_PROC_FLAG_REQUIRES_ATA))
            continue;
        items[nb_items] = procedure->display_name;
        procedures[nb_items] = procedure;
        nb_items++;
    }

    while (1) {
        clear_body();
        dialog_vars.no_items = 1;
        dialog_vars.item_help = 0;
        dialog_vars.input_result = NULL;
        dialog_vars.default_button = 0;
        dialog_vars.extra_button = 1;
        dialog_vars.extra_label = "Help";
        int ret = dialog_menu("Choose procedure", "", 0, 0, 0, nb_items, (char**)items);
        dialog_vars.extra_button = 0;

        if ((ret != DLG_EXIT_OK) && (ret != DLG_EXIT_EXTRA))
            return NULL;

        procedure = NULL;
        for (int i = 0; i < nb_items; i++)
            if (!strcmp(items[i], dialog_vars.input_result)) {
                procedure = procedures[i];
                break;
            }
        assert(procedure);

        if (ret == DLG_EXIT_EXTRA) {
            dialog_msgbox(procedure->display_name, procedure->help ? : "No help", 0, 0, 1);
            continue;
        }
        assert(ret == DLG_EXIT_OK);
        return procedure;
    }
}

void log_cb(void *priv, enum DC_LogLevel level, const char* fmt, va_list vl) {
    (void)priv;
    char *msg = dc_log_default_form_string(level, fmt, vl);
    assert(msg);
    dialog_msgbox(log_level_name(level), msg, 0, 0, 1);
    free(msg);
}

