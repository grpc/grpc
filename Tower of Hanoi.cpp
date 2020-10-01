#include <iostream>
#include<stdio.h>
#include<stdlib.h>

using namespace std;

void towerOfHanoi(int n, char a, char b, char c)
{
    if (n == 1)
    {
        printf("\n Move disk from rod %c to rod %c", a, c);
        return;
    }
    towerOfHanoi(n-1, a, c, b);
    towerOfHanoi(1, a, b, c);
    towerOfHanoi(n-1, b, a, c);
}

int main()
{
    int n = 3;
    towerOfHanoi(n, 'A', 'B', 'C');
    return 0;
}
