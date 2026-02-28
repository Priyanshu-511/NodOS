#pragma once

// NodeV scripting language for NodOS.
// Grammar defined in grammar.txt (uploaded by user).
// Adapted for freestanding kernel: no STL, no RTTI, no exceptions.
//
// Supported syntax (matches uploaded grammar.txt):
//   int x = 10;
//   float f = 3.14;
//   string s = "hello";
//   list nums;
//   if (x > 5) { ... } else { ... }
//   while (x > 0) { x = x - 1; }
//   for (int i = 0, i < 10, i = i + 1) { ... }
//   function add(a, b) { return a + b; }
//   pout("result: ", add(3, 4), "\n");
//   pin(varName);

enum NodeVResult { NV_OK = 0, NV_ERROR = 1 };

NodeVResult nodev_exec(const char* source);
NodeVResult nodev_run_file(const char* filename);
