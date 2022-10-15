#include <stdio.h>

#define bool _Bool

int main ()
{
  int index = 1, i;
  int result[10000] = {0};
  result[0] = 2;
  int bit_count = 0;
  unsigned int mask = ~0;
  for (i = 3; i < 100000; i++)
  {
    while (mask & i)
    {
      mask = mask << 1;
      bit_count++;
      // printf("(%d)", ~mask);
    }
    int j = 0;
    bool breaked = 0;
    while (result[j] <= (~mask >> (bit_count/2)) && result[j] != 0)
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
