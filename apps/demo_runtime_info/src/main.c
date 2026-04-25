#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base_demos/demo_runtime_info.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
static void
_each_error_log(dterr_t* dterr, void* context)
{
    const char* tag = (const char*)context;
    dtlog_error(tag, "%s@%ld in %s: %s", dterr->source_file, (long)dterr->line_number, dterr->source_function, dterr->message);
}

// --------------------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    dterr_t* dterr = NULL;
    dtmc_base_demo_runtime_info_t* demo = NULL;

    // === create and configure the demo instance ===
    {
        DTERR_C(dtmc_base_demo_runtime_info_create(&demo));
        dtmc_base_demo_runtime_info_config_t c = { 0 };
        DTERR_C(dtmc_base_demo_runtime_info_configure(demo, &c));
    }

    // === start the demo ===
    DTERR_C(dtmc_base_demo_runtime_info_start(demo));

cleanup:
    if (dterr != NULL)
    {
        dterr_each(dterr, _each_error_log, (void*)TAG);
        dterr_dispose(dterr);
    }

    // dispose because start may have left things running
    dtmc_base_demo_runtime_info_dispose(demo);

    return 0;
}
