/// \file UtilTests.cpp
/// \brief *Unit tests for the utility functions*
///
/// \copyright Unpublished software of FlexRadio Systems (c) 2020 FlexRadio Systems
///
/// Unauthorized use, duplication or distribution of this software is
/// strictly prohibited by law.
///
/// Provide unit tests using GoogleTest for the utility functions of
/// the library.  These should test as much as possible to make sure
/// these are reliable and resilient to however a user may abuse them.
///
///
// ****************************************
// System Includes
// ****************************************
#include "gtest/gtest.h"

// ****************************************
// Project Includes
// ****************************************
extern "C" {
#include "utils.h"
}


// ****************************************
// Global Functions
// ****************************************

///
/// \brief *Test the Keyword Arguments Parser*
///
///
TEST(UtilityTestSuite, KWArgs)
{
   EXPECT_EQ(find_kwarg(NULL, NULL), nullptr);

   char* argv[3] = {
      strdup("slice"),
      strdup("status"),
      strdup("mode=USB")
   };
   EXPECT_EQ(parse_kwargs(argv, ARRAY_SIZE(argv), 255), nullptr);
   struct kwarg* args = parse_kwargs(argv, ARRAY_SIZE(argv), 0);

   EXPECT_EQ(find_kwarg(args, NULL), nullptr);
   EXPECT_STREQ(find_kwarg(args, "mode"), "USB");
   EXPECT_EQ(find_kwarg(args, "level"), nullptr);
}

TEST(UtilityTestSuite, ArgvParser)
{
   char* testme = strdup("slice status mode=USB\n");
   char* argv[128];  //  XXX Should fix magic here too
   EXPECT_EQ(parse_argv(testme, argv, 128), 3); // XXX Should fix magic here
   EXPECT_STREQ(argv[0], "slice");
   EXPECT_STREQ(argv[1], "status");
   EXPECT_STREQ(argv[2], "mode=USB");
}

TEST(UtilityTestSuite, ArgsParser)
{
    char* testme = strdup("slice status mode=USB foo=bar baz=128 junk=0l\n");
    char* argv[128];  //  XXX Should fix magic here too
    waveform_args_t *args;

    args = parse_args(testme);
    ASSERT_NE(args, nullptr);
    ASSERT_EQ(args->argc, 2);
    EXPECT_STREQ(argv[0], "slice");
    EXPECT_STREQ(argv[1], "status");

    EXPECT_EQ(find_kwarg(args->kwargs, NULL), nullptr);
    EXPECT_STREQ(find_kwarg(args->kwargs, "mode"), "USB");
    EXPECT_STREQ(find_kwarg(args->kwargs, "foo"), "bar");
    EXPECT_STREQ(find_kwarg(args->kwargs, "baz"), "128");
    EXPECT_STREQ(find_kwarg(args->kwargs, "junk"), "0l");
    EXPECT_EQ(find_kwarg(args->kwargs, "level"), nullptr);

}

TEST(UtilityTestSuite, Output)
{

}