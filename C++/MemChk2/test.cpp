// test.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"

#include "CrtAllocationHook.h"

#include <string>

void f()
{
    new int;
    new std::string("01234567890123467890123456798");
}

int main()
{
    malloc(3);
    free(0);
    delete new int;

    crtAllocation_beginHook(1);

    for (int i = 0; i < 10; ++i)
    f();
    new std::string("0123456");
    malloc(3);
    free(malloc(1));
    delete new int;

    crtAllocation_endHook();
    crtAllocation_dumpResults(1);
    crtAllocation_clearResults();
}

