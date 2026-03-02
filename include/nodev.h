#pragma once
#include <stdint.h>

// *****************************************************************************
//  nodev.h  —  NodeV scripting language for NodOS

//  Freestanding kernel port: no STL, no RTTI, no exceptions, no dynamic_cast.
//  All allocation is from a fixed 192 KB arena (reset on each nodev_exec call).
//
//  Supported syntax:
//    int x = 10;
//    float f = 3.14;
//    string s = "hello";
//    list nums;
//    if (x > 5) { ... } else { ... }
//    while (x > 0) { x = x - 1; }
//    for (int i = 0, i < 10, i = i + 1) { ... }
//    function add(a, b) { return a + b; }
//    pout("result: ", add(3, 4), "\n");
//    pin(varName);

//  OOP syntax (v2 — fully implemented):
//    class Animal {
//        public:
//        int age;
//        constructor(a) { self.age = a; }
//        function speak() { pout("..."); }
//    }
//    Animal dog = new Animal(3);
//    dog.speak();
//    pout(dog.age);
//    delete dog;

//  $import "other.nod"   — inline another VFS file before execution
// *****************************************************************************

enum NodeVResult { NV_OK = 0, NV_ERROR = 1 };

// Execute a source string directly.
NodeVResult nodev_exec(const char* source);

// Read a .nod file from VFS and execute it (handles $import directives).
NodeVResult nodev_run_file(const char* filename);

// Pre-fill the input queue for pin() calls when running in GUI mode.
// Each entry is consumed by one pin(varName) statement during execution.
// Call this BEFORE nodev_exec() / nodev_run_file().
static const int NODEV_MAX_INPUTS = 16;
void nodev_set_inputs(const char inputs[][64], int count);