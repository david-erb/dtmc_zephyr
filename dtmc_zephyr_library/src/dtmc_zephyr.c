#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#if IS_ENABLED(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif

#include <dtcore/dterr.h>
#include <dtmc/dtmc_zephyr.h>

#define TAG "dtmc_zephyr"

// Zephyr: CONFIG_* is 1 or undefined; IS_ENABLED() -> 1 or 0
#define APPEND_CONFIG_FLAG(SYM)                                                                                                \
    do                                                                                                                         \
    {                                                                                                                          \
        const char* v = IS_ENABLED(SYM) ? "y" : "n";                                                                           \
        s = dtstr_concat_format(s, t, item_format_str, p, #SYM, v);                                                            \
    } while (0)

// --------------------------------------------------------------------------------------
const char*
dtmc_flavor(void)
{
    return DTMC_ZEPHYR_FLAVOR;
}

// --------------------------------------------------------------------------------------
const char*
dtmc_version(void)
{
    return DTMC_ZEPHYR_VERSION;
}

#if IS_ENABLED(CONFIG_SHELL)

// --------------------------------------------------------------------------------------
static int
dtmc_zephyr__shellcmd_reboot(const struct shell* shell, size_t argc, char** argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(shell, "Rebooting...");
    k_sleep(K_MSEC(100));
    sys_reboot(SYS_REBOOT_COLD);
    return 0;
}

// Must be at file scope (compile-time registration)
SHELL_CMD_REGISTER(reboot, NULL, "Reboot the system", dtmc_zephyr__shellcmd_reboot);

#endif // CONFIG_SHELL

// --------------------------------------------------------------------------------------
dterr_t*
dtmc_zephyr_shell_initialize(void)
{
    dterr_t* dterr = NULL;

#if !IS_ENABLED(CONFIG_SHELL)
    dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "shell is not enabled in this build");
#endif

    return dterr;
}