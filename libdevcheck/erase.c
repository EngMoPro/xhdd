#include "procedure.h"
#include "device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Suggest default value for erase options
static int erase_suggest_default(DC_Dev *dev, DC_OptionSetting *setting) {
    (void)dev;
    if (!strcmp(setting->name, "pattern")) {
        setting->value = strdup("0x00"); // default erase pattern
        return 0;
    }
    return 1;
}

// Open procedure (initialize context)
static int erase_open(DC_ProcedureCtx *ctx) {
    ctx->blk_size = 512; // default block size
    ctx->progress.num = 0;
    ctx->progress.den = 1; // initialize progress
    return 0;
}

// Perform procedure (erase device)
static int erase_perform(DC_ProcedureCtx *ctx) {
    printf("Erasing device %s with pattern...\n", ctx->dev->dev_fs_name);
    // For simplicity, just mark as complete
    ctx->progress.num = ctx->progress.den;
    return 0;
}

// Close procedure (cleanup)
static void erase_close(DC_ProcedureCtx *ctx) {
    // Nothing to clean up for now
}

// Global erase procedure
DC_Procedure erase_procedure = {
    .name = "erase",
    .display_name = "Erase Device",
    .help = "Erase the device with a specified pattern",
    .flags = DC_PROC_FLAG_INVASIVE | DC_PROC_FLAG_REQUIRES_ATA,
    .options = (DC_ProcedureOption[]) {
        { .name = "pattern", .help = "Pattern to write", .type = DC_ProcedureOptionType_eString, .offset = 0 },
        { 0 } // terminator
    },
    .priv_data_size = 0,
    .suggest_default_value = erase_suggest_default,
    .open = erase_open,
    .perform = erase_perform,
    .close = erase_close,
    .next = NULL,
};

