#include <stdio.h>

int main ()
{
  int a = 0, i;
  for (i = 0; i < 1000000; i++) // Always Taken
  {
    if (i % 4 == 0) // T T T NT
    {
      a += i;
    }
  }
  printf("%d", a);
}
