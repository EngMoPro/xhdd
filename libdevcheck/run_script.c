#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "procedure.h"
#include "device.h"
#include "ata.h"  // your AtaCommand / prepare_ata_command

typedef struct {
    const char *script_file;
    uint8_t regs[7];  // r1-r7
} RunScriptPriv;

static int run_script_suggest_default(DC_Dev *dev, DC_OptionSetting *setting) {
    if (!strcmp(setting->name, "script_file")) {
        setting->value = "libdevcheck/SmartScript.bin";
    }
    return 0;
}

static int run_script_open(DC_ProcedureCtx *ctx) {
    RunScriptPriv *priv = calloc(1, sizeof(RunScriptPriv));
    if (!priv) return 1;
    priv->script_file = "libdevcheck/SmartScript.bin";
    memset(priv->regs, 0, sizeof(priv->regs));
    ctx->priv = priv;
    ctx->progress.num = 0;
    ctx->progress.den = 1;
    return 0;
}

// Forward declaration for recursion
static int run_script_file(DC_Dev *dev, const char *filename, int *line_counter, RunScriptPriv *priv);

static int execute_ata_line(DC_Dev *dev, RunScriptPriv *priv, const char *line) {
    char tmp[256];
    sscanf(line, "%255s", tmp);

    if (strncmp(tmp, "r1", 2) == 0) sscanf(strchr(line, '$') + 1, "%2hhx", &priv->regs[0]);
    else if (strncmp(tmp, "r2", 2) == 0) sscanf(strchr(line, '$') + 1, "%2hhx", &priv->regs[1]);
    else if (strncmp(tmp, "r3", 2) == 0) sscanf(strchr(line, '$') + 1, "%2hhx", &priv->regs[2]);
    else if (strncmp(tmp, "r4", 2) == 0) sscanf(strchr(line, '$') + 1, "%2hhx", &priv->regs[3]);
    else if (strncmp(tmp, "r5", 2) == 0) sscanf(strchr(line, '$') + 1, "%2hhx", &priv->regs[4]);
    else if (strncmp(tmp, "r6", 2) == 0) sscanf(strchr(line, '$') + 1, "%2hhx", &priv->regs[5]);
    else if (strncmp(tmp, "r7", 2) == 0) sscanf(strchr(line, '$') + 1, "%2hhx", &priv->regs[6]);
    else if (strncmp(tmp, "reset", 5) == 0) printf("[CMD] reset\n");
    else if (strncmp(tmp, "waitnbsy", 7) == 0) printf("[CMD] waitnbsy\n");
    else if (strncmp(tmp, "checkdrq", 7) == 0) printf("[CMD] checkdrq\n");
    else if (strncmp(tmp, "sectorsfrom", 11) == 0) {
        char sec_file[128];
        sscanf(line + 13, "%127s", sec_file);
        char fullpath[256];
        if (strchr(sec_file, '/')) strncpy(fullpath, sec_file, sizeof(fullpath));
        else snprintf(fullpath, sizeof(fullpath), "libdevcheck/%s", sec_file);
        int lc = 0;
        run_script_file(dev, fullpath, &lc, priv);
    } else {
        printf("[ATA CMD] r1=%02x r2=%02x r3=%02x r4=%02x r5=%02x r6=%02x r7=%02x\n",
               priv->regs[0], priv->regs[1], priv->regs[2],
               priv->regs[3], priv->regs[4], priv->regs[5], priv->regs[6]);
        // Actual ata_send_command(dev, &cmd, ...) call goes here
    }
    return 0;
}

static int run_script_file(DC_Dev *dev, const char *filename, int *line_counter, RunScriptPriv *priv) {
    FILE *f = fopen(filename, "rb");
    if (!f) { perror(filename); return 1; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        (*line_counter)++;
        printf("[Line %d] %s", *line_counter, line);
        execute_ata_line(dev, priv, line);
    }
    fclose(f);
    return 0;
}

static int run_script_perform(DC_ProcedureCtx *ctx) {
    RunScriptPriv *priv = ctx->priv;
    int line_counter = 0;
    return run_script_file(ctx->dev, priv->script_file, &line_counter, priv);
}

static void run_script_close(DC_ProcedureCtx *ctx) {
    free(ctx->priv);
    ctx->priv = NULL;
}

DC_Procedure run_script_procedure = {
    .name = "RunScript",
    .display_name = "Run Script",
    .help = "Executes SmartScript.bin and all referenced sector files",
    .flags = 0,
    .options = NULL,
    .options_num = 0,
    .priv_data_size = sizeof(RunScriptPriv),
    .suggest_default_value = run_script_suggest_default,
    .open = run_script_open,
    .perform = run_script_perform,
    .close = run_script_close,
};

