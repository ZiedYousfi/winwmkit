#include <stdio.h>

#include "winwmkit/winwmkit.h"

int main(void) {
  printf("Hello, WinWMKit!\n");

  WWMK_Window windows[10];
  int count = wwmk_get_windows(windows, 0);
  printf("Found %d windows.\n", count);
  return 0;
}
