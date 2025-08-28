#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include "procedure.h"
#include "device.h"

#define SECTORS_AT_ONCE 256
#define BLK_SIZE (SECTORS_AT_ONCE * 512)

static volatile sig_atomic_t interrupt_flag = 0;
void handle_sigint(int sig) { interrupt_flag = 1; }

struct erase_priv {
    int fd;
    uint64_t start_lba;
    uint64_t current_lba;
    uint64_t end_lba;
    void *buf;
    uint64_t count_erased;
    uint64_t count_good;
    uint64_t count_yellow;
    uint64_t count_darkgreen;
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
    if (!priv->buf) return 1;

    priv->fd = open(ctx->dev->dev_path, O_RDWR | O_LARGEFILE);
    if (priv->fd == -1) {
        perror("open device");
        free(priv->buf);
        return 1;
    }

    ctx->blk_size = BLK_SIZE;
    ctx->progress.num = 0;
    ctx->progress.den = (priv->end_lba - priv->start_lba + SECTORS_AT_ONCE - 1) / SECTORS_AT_ONCE;

    signal(SIGINT, handle_sigint);
    interrupt_flag = 0;

    priv->count_erased = 0;
    priv->count_good = 0;
    priv->count_yellow = 0;
    priv->count_darkgreen = 0;

    return 0;
}

// Perform procedure: erase only slow/bad sectors
static int Perform(DC_ProcedureCtx *ctx) {
    if (interrupt_flag) return 1;

    ErasePriv *priv = ctx->priv;
    size_t sectors_to_process = (priv->end_lba - priv->current_lba < SECTORS_AT_ONCE) ?
                                (size_t)(priv->end_lba - priv->current_lba) :
                                SECTORS_AT_ONCE;
    if (sectors_to_process == 0) return 1;

    ctx->report.lba = priv->current_lba;
    ctx->report.sectors_processed = sectors_to_process;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    ssize_t r = pread(priv->fd, priv->buf, sectors_to_process * 512, priv->current_lba * 512);
    clock_gettime(CLOCK_MONOTONIC, &end);

    uint64_t elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                          (end.tv_nsec - start.tv_nsec) / 1000000;

    // Map sector status based on read speed or error
    if (r != (ssize_t)(sectors_to_process * 512)) {
        ctx->report.blk_status = DC_BlockStatus_eError; // red/error
        memset(priv->buf, 0, sectors_to_process * 512);
        pwrite(priv->fd, priv->buf, sectors_to_process * 512, priv->current_lba * 512);
    } else if (elapsed_ms >= 500) {
        ctx->report.blk_status = DC_BlockStatus_eError; // red
        memset(priv->buf, 0, sectors_to_process * 512);
        pwrite(priv->fd, priv->buf, sectors_to_process * 512, priv->current_lba * 512);
    } else if (elapsed_ms >= 150) {
        ctx->report.blk_status = DC_BlockStatus_eAmnf; // green (slow)
        memset(priv->buf, 0, sectors_to_process * 512);
        pwrite(priv->fd, priv->buf, sectors_to_process * 512, priv->current_lba * 512);
    } else if (elapsed_ms >= 50) {
        ctx->report.blk_status = DC_BlockStatus_eUnc; // dark green
    } else if (elapsed_ms >= 10) {
        ctx->report.blk_status = DC_BlockStatus_eIdnf; // yellow
    } else {
        ctx->report.blk_status = DC_BlockStatus_eOk; // blue (fast/good)
    }

    // Count sectors
    if (ctx->report.blk_status == DC_BlockStatus_eError || ctx->report.blk_status == DC_BlockStatus_eAmnf) {
        priv->count_erased += sectors_to_process;
    } else if (ctx->report.blk_status == DC_BlockStatus_eOk) {
        priv->count_good += sectors_to_process;
    } else if (ctx->report.blk_status == DC_BlockStatus_eIdnf) {
        priv->count_yellow += sectors_to_process;
    } else if (ctx->report.blk_status == DC_BlockStatus_eUnc) {
        priv->count_darkgreen += sectors_to_process;
    }

    // ✅ Progress line (updates in place, no spam)
    fprintf(stdout, "\rErase..LBA %llu / %llu", 
            priv->current_lba, priv->end_lba);
    fflush(stdout);

    priv->current_lba += sectors_to_process;
    ctx->progress.num++;

    return 0;
}

// Close procedure
static void Close(DC_ProcedureCtx *ctx) {
    ErasePriv *priv = ctx->priv;
    if (priv->fd >= 0) close(priv->fd);
    free(priv->buf);
    signal(SIGINT, SIG_DFL);

    printf("\n\nErase complete:\n");
    printf("Good sectors (blue): %llu\n", priv->count_good);
    printf("Yellow sectors: %llu\n", priv->count_yellow);
    printf("Dark green sectors: %llu\n", priv->count_darkgreen);
    printf("Erased sectors (green/red): %llu\n", priv->count_erased);
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
    .help = "Erase only green (150–499 ms) and red (≥500 ms or error) sectors. Ctrl+C to stop.",
    .flags = DC_PROC_FLAG_INVASIVE | DC_PROC_FLAG_REQUIRES_ATA,
    .options = options,
    .priv_data_size = sizeof(ErasePriv),
    .suggest_default_value = SuggestDefaultValue,
    .open = Open,
    .perform = Perform,
    .close = Close,
    .next = NULL,
};

