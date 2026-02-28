#pragma once

// Try to power off the machine (works in most emulators)
void system_shutdown();

// Restart the machine using the 8042 PS/2 controller (works everywhere)
void system_reboot();