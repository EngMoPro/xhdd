#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "procedure.h"
#include "device.h"
#include "ata.h"

#define SECTORS_AT_ONCE 256
#define BLK_SIZE (SECTORS_AT_ONCE * 512)

struct erase_priv {
    int fd;
    uint64_t start_lba;
    uint64_t current_lba;
    uint64_t end_lba;
    void *buf;
};
typedef struct erase_priv ErasePriv;

// Suggest default start LBA
static int SuggestDefaultValue(DC_Dev *dev, DC_OptionSetting *setting) {
    if (!strcmp(setting->name, "start_lba"))
        setting->value = strdup("0");
    return 0;
}

// Open procedure
static int Open(DC_ProcedureCtx *ctx) {
    ErasePriv *priv = ctx->priv;
    priv->current_lba = priv->start_lba;
    priv->end_lba = ctx->dev->capacity / 512;

    priv->buf = calloc(1, BLK_SIZE);
    if (!priv->buf)
        return 1;

    priv->fd = open(ctx->dev->dev_path, O_RDWR | O_LARGEFILE);
    if (priv->fd == -1) {
        perror("open device");
        free(priv->buf);
        return 1;
    }

    ctx->blk_size = BLK_SIZE;
    ctx->progress.num = 0;
    ctx->progress.den = (priv->end_lba - priv->start_lba + SECTORS_AT_ONCE - 1) / SECTORS_AT_ONCE;
    return 0;
}

// Perform procedure: erase only slow/bad sectors
static int Perform(DC_ProcedureCtx *ctx) {
    ErasePriv *priv = ctx->priv;
    size_t sectors_to_process = (priv->end_lba - priv->current_lba < SECTORS_AT_ONCE) ?
                                (size_t)(priv->end_lba - priv->current_lba) :
                                SECTORS_AT_ONCE;

    if (sectors_to_process == 0)
        return 1;

    ctx->report.lba = priv->current_lba;
    ctx->report.sectors_processed = sectors_to_process;
    ctx->report.blk_status = DC_BlockStatus_eOk;

    // Read sectors to check
    ssize_t r = pread(priv->fd, priv->buf, sectors_to_process * 512, priv->current_lba * 512);
    if (r != (ssize_t)(sectors_to_process * 512)) {
        // Mark slow/bad sectors
        ctx->report.blk_status = DC_BlockStatus_eWarning;

        // Write zeros to erase slow/bad sectors
        memset(priv->buf, 0, sectors_to_process * 512);
        ssize_t w = pwrite(priv->fd, priv->buf, sectors_to_process * 512, priv->current_lba * 512);
        if (w != (ssize_t)(sectors_to_process * 512)) {
            perror("erase write failed");
            ctx->report.blk_status = DC_BlockStatus_eError;
        }
    }

    priv->current_lba += sectors_to_process;
    ctx->progress.num++;

    return 0; // continue
}

// Close procedure
static void Close(DC_ProcedureCtx *ctx) {
    ErasePriv *priv = ctx->priv;
    if (priv->fd >= 0)
        close(priv->fd);
    free(priv->buf);
}

// Options
static DC_ProcedureOption options[] = {
    { "start_lba", "LBA to start erasing from", offsetof(ErasePriv, start_lba), DC_ProcedureOptionType_eInt64, NULL },
    { NULL }
};

// Procedure structure
DC_Procedure erase_procedure = {
    .name = "erase",
    .display_name = "Erase slow/bad sectors",
    .help = "Automatically erases only slow or bad sectors to fix them.",
    .flags = DC_PROC_FLAG_INVASIVE | DC_PROC_FLAG_REQUIRES_ATA,
    .options = options,
    .priv_data_size = sizeof(ErasePriv),
    .suggest_default_value = SuggestDefaultValue,
    .open = Open,
    .perform = Perform,
    .close = Close,
    .next = NULL,
};

