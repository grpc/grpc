#include <stdio.h>

#define COMMENT(x) printf("\n----" x "----\n");
#define STR(x) #x
#define STR2(x) STR(x)
#define TEST(x) \
    if (!(x)) { \
        fprintf(stderr, "\033[31;1mFAILED:\033[22;39m " __FILE__ ":" STR2(__LINE__) " " #x "\n"); \
        status = 1; \
    } else { \
        printf("\033[32;1mOK:\033[22;39m " #x "\n"); \
    }


