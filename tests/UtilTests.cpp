//
// Created by Annaliese McDermond on 5/5/20.
//

#include "gtest/gtest.h"
#include "utils.h"

TEST(UtilityTestSuite, TokenizerTest){
   char* argv[3] = {
           "slice",
           "status",
           "mode=USB"
   };
   struct kwarg *args;

   args = parse_kwargs(argv, 3, 0);
   EXPECT_STREQ(find_kwarg(args, "mode"), "USB");
}
