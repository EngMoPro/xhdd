#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stddef.h>
#include "procedure.h"
#include "device.h"
#include "erase.h"

#define SECTOR_SIZE 512

struct erase_priv {
    int fd;
    uint64_t current_lba;
    uint64_t end_lba;
    void *buf;
    char *pattern;  // store user pattern
};
typedef struct erase_priv ErasePriv;

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
    ErasePriv *priv = ctx->priv;
    ctx->blk_size = SECTOR_SIZE;
    priv->current_lba = 0;
    priv->end_lba = ctx->dev->capacity / SECTOR_SIZE;

    priv->buf = malloc(SECTOR_SIZE);
    if (!priv->buf)
        return 1;

    priv->fd = open(ctx->dev->dev_path, O_RDWR | O_DIRECT);
    if (priv->fd < 0) {
        perror("open device");
        free(priv->buf);
        return 1;
    }

    ctx->progress.num = 0;
    ctx->progress.den = priv->end_lba;

    return 0;
}

// Perform procedure (erase one sector)
static int erase_perform(DC_ProcedureCtx *ctx) {
    ErasePriv *priv = ctx->priv;
    ssize_t wr;
    uint8_t val = 0x00;

    // parse user pattern if given
    if (priv->pattern) {
        if (priv->pattern[0] == '0' && priv->pattern[1] == 'x')
            val = (uint8_t)strtoul(priv->pattern, NULL, 16);
    }
    memset(priv->buf, val, SECTOR_SIZE);

    wr = pwrite(priv->fd, priv->buf, SECTOR_SIZE, priv->current_lba * SECTOR_SIZE);
    if (wr != SECTOR_SIZE) {
        ctx->report.blk_status = DC_BlockStatus_eError;
    } else {
        ctx->report.blk_status = DC_BlockStatus_eOk;
    }

    ctx->report.lba = priv->current_lba;
    ctx->report.sectors_processed = 1;

    priv->current_lba++;
    ctx->progress.num++;

    return (priv->current_lba >= priv->end_lba) ? 1 : 0;
}

// Close procedure (cleanup)
static void erase_close(DC_ProcedureCtx *ctx) {
    ErasePriv *priv = ctx->priv;
    if (priv->fd >= 0)
        close(priv->fd);
    free(priv->buf);
    if (priv->pattern)
        free(priv->pattern);
}

// Options
static DC_ProcedureOption options[] = {
    { .name = "pattern", .help = "Erase pattern (hex, e.g., 0x00)", .type = DC_ProcedureOptionType_eString, .offset = offsetof(ErasePriv, pattern) },
    { NULL }
};

// Global erase procedure
DC_Procedure erase_procedure = {
    .name = "erase",
    .display_name = "Erase/Remap Bad Sectors",
    .help = "Non-destructively erases and attempts to remap bad sectors",
    .flags = DC_PROC_FLAG_INVASIVE | DC_PROC_FLAG_REQUIRES_ATA,
    .priv_data_size = sizeof(ErasePriv),
    .options = options,
    .suggest_default_value = erase_suggest_default,
    .open = erase_open,
    .perform = erase_perform,
    .close = erase_close,
    .next = NULL,
};

