#pragma once
#include <lvgl.h>

namespace ScreenManager {

  void showCaptivePortalScreen();  // QR code + instructions
  void showDashboard();            // Main multi-tab view
  void showSettings();             // Gear cog config overlay
  void showSplash();               // Loading/boot screen

  void applyTheme(uint8_t themeId);
  void updateTickerText(const char *text);
  void refreshAllWidgets();

} // namespace ScreenManager
