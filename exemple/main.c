#include <stdio.h>
#include <stdlib.h>

#include "winwmkit/winwmkit.h"

int main(void) {
  printf("Hello, WinWMKit!\n");

  WWMK_Window *windows = NULL;
  int numberOfWindows = 0;

  numberOfWindows = wwmk_get_windows(&windows, 0);
  if (numberOfWindows < 0) {
    fprintf(stderr, "Failed to get windows. error=%d\n", numberOfWindows);
    free(windows);
    return 1;
  }

  printf("Found %d windows.\n", numberOfWindows);

  for (int i = 0; i < numberOfWindows; i++) {
    printf("Window %s: is_visible : %d\n", windows[i].title,
           windows[i].is_visible);
  }

  free(windows);
  return 0;
}
