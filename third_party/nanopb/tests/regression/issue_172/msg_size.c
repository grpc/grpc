#include "test.pb.h"

PB_STATIC_ASSERT(testmessage_size >= 1+1+1+1+16, TESTMESSAGE_SIZE_IS_WRONG)

int main()
{
    return 0;
}

