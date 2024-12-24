#include <string>
#include <iostream>
#include "fmt/format.h"

using namespace std;

template<typename... Args>
void TestFunc1(Args&&... args) {
    TestFunc2(to_string(args)...);
}

template<typename... Args>
void TestFunc2(Args&&... args) {
    ((cout << args << endl), ...);
    TestFunc3(string("hello"), fmt::format("str{}", args)...);
}

template<typename... Args>
void TestFunc3(Args&&... args) {
    ((cout << args << endl), ...);
}

int main() {
    int a = 1, b = 2;
    TestFunc1(a, b);

    return 0;
}