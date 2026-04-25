#include <dtcore/dterr.h>

#include <dtcore_tests.h>
#include <dtmc_base_tests.h>
#include <dtmc_zephyr_tests.h>

int
main(int argc, char* argv[])
{
    dtunittest_control_t unittest_control = { 0 };
    unittest_control.should_print_suites = true;
    unittest_control.should_print_tests = false;
    unittest_control.should_print_errors = true;

    if (argc > 1)
    {
        unittest_control.pattern = argv[1];
    }

    // if (unittest_control.pattern == NULL)
    //     unittest_control.pattern = "busywork";

    test_dtmc_zephyr_sensors_matching(&unittest_control);

    // print summary as final line of test output
    dtunittest_print_final(&unittest_control);

    int rc = 0;
    if (unittest_control.total_fail_count > 0)
        rc = 1;

    return rc;
}