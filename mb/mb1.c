#include <stdio.h>

int main ()
{
  int a = 0, i;
  for (i = 0; i < 1000000; i++)
  {
    a += i;
  }
  printf("%d", a);
}