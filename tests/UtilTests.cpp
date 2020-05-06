//
// Created by Annaliese McDermond on 5/5/20.
//

#include "gtest/gtest.h"

extern "C" {
#include "utils.h"
}

TEST(UtilityTestSuite, TokenizerTest){
   char* argv[3] = {
           strdup("slice"),
           strdup("status"),
           strdup("mode=USB")
   };
   struct kwarg *args;

   args = parse_kwargs(argv, ARRAY_SIZE(argv), 0);
   EXPECT_STREQ(find_kwarg(args, "mode"), "USB");
}
