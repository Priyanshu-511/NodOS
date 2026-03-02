#pragma once

//  NodOS Boot Splash Screen
//  Call splash_show() once from gui_run() before the main event loop.
//  It draws the splash, animates loading steps, then returns.
void splash_show();