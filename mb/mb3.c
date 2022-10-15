#include <stdio.h>

#define bool _Bool

int main ()
{
  int index = 1, i;
  int result[10000] = {0};
  result[0] = 2;
  for (i = 3; i < 100000; i++)
  {
    int j = 0;
    
    bool breaked = 0;
    while (result[j]!=0)
    {
      // printf("%d", result[j]);
      if(i % result[j] == 0)
      {
        breaked = 1;
        break;
      }
      j++;
    }
    if(!breaked)
    {
      printf("%d ", i);
      result[index++] = i;
    }
  }
}
