#ifndef SCALE_KEYS_H
#define SCALE_KEYS_H
#include <linux/input-event-codes.h>

static char char_from_keycode(int code)
{
    switch (code)
    {
      case KEY_1:
        return '1';

      case KEY_2:
        return '2';

      case KEY_3:
        return '3';

      case KEY_4:
        return '4';

      case KEY_5:
        return '5';

      case KEY_6:
        return '6';

      case KEY_7:
        return '7';

      case KEY_8:
        return '8';

      case KEY_9:
        return '9';

      case KEY_0:
        return '0';

      case KEY_MINUS:
        return '-';

      case KEY_Q:
        return 'q';

      case KEY_W:
        return 'w';

      case KEY_E:
        return 'e';

      case KEY_R:
        return 'r';

      case KEY_T:
        return 't';

      case KEY_Y:
        return 'y';

      case KEY_U:
        return 'u';

      case KEY_I:
        return 'i';

      case KEY_O:
        return 'o';

      case KEY_P:
        return 'p';

      case KEY_A:
        return 'a';

      case KEY_S:
        return 's';

      case KEY_D:
        return 'd';

      case KEY_F:
        return 'f';

      case KEY_G:
        return 'g';

      case KEY_H:
        return 'h';

      case KEY_J:
        return 'j';

      case KEY_K:
        return 'k';

      case KEY_L:
        return 'l';

      case KEY_Z:
        return 'z';

      case KEY_X:
        return 'x';

      case KEY_C:
        return 'c';

      case KEY_V:
        return 'v';

      case KEY_B:
        return 'b';

      case KEY_N:
        return 'n';

      case KEY_M:
        return 'm';

      default:
        return (char)-1;
    }
}

#endif
