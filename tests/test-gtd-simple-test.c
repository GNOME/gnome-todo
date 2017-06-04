#include <stdio.h>
#include <glib.h>

static void
simple_test_case (void)
{
  // a suitable test
  g_assert (g_bit_storage (1) == 1);
  // a test with verbose error message
  g_assert_cmpint (g_bit_storage (1), ==, 1);
}

int
main (int   argc,
      char *argv[])
{
  // initialize test program
  gtk_test_init (&argc, &argv);
  // hook up your test functions
  g_test_add_func ("/Simple Test Case", simple_test_case);
  // run tests from the suite
  return g_test_run();
}


