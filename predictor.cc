#include "predictor.h"

/////////////////////////////////////////////////////////////
// 2bitsat
/////////////////////////////////////////////////////////////
unsigned char twobitsat_predictor_table[1024];

void InitPredictor_2bitsat() 
{
  // Initialize the predictor table
  int i;
  for(i = 0; i < 1024; i++)
  {
    twobitsat_predictor_table[i] = 0b01010101; // Weakly NT at start 
  }
}

bool GetPrediction_2bitsat(UINT32 PC) 
{
  unsigned char  bit_index = PC & 0b11;
  unsigned short arr_index = (PC >> 2) & 0b1111111111;
  // Get the counter from the table
  unsigned char  counter = (twobitsat_predictor_table[arr_index] >> (bit_index * 2)) & 0b11;
  // Determine taken or not
  bool taken = counter >> 1; // 00,01:NT, 10,11:T
    
  return taken;
}

void UpdatePredictor_2bitsat(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) 
{
  // Get all value first
  unsigned char  bit_index = PC & 0b11;
  unsigned short arr_index = (PC >> 2) & 0b1111111111;
  // Get the counter from the table
  unsigned char  counter = (twobitsat_predictor_table[arr_index] >> (bit_index * 2)) & 0b11;
  
  if(resolveDir == predDir) // Correct prediction
  {
    if (counter == 0b10) // weakly taken correct
    {
      counter++;
    }
    else if (counter == 0b01) // weakly not taken correct
    {
      counter--;
    }
  }
  else // Misprediction
  {
    if (predDir)  // 11 or 10
    {
      counter--;
    }
    else // 00 or 01
    {
      counter++;
    }
  }
  // Update counter into table
  twobitsat_predictor_table[arr_index] = (counter << (bit_index * 2)) 
      | (twobitsat_predictor_table[arr_index] & ~(0b11 << (bit_index * 2)));
}

/////////////////////////////////////////////////////////////
// 2level
/////////////////////////////////////////////////////////////

unsigned char twolevel_history_table[512] = {0}; // 8bits pointer (6 bits used), all 0 initialized
unsigned short twolevel_predictor_table[64]; // 16bits pointer (16 bits used)

void InitPredictor_2level() 
{
  // twolevel_history_table = malloc(512 * sizeof(char));
  // twolevel_predictor_table = malloc(64 * sizeof(short));
  int i;
  for(i = 0; i < 64; i++)
  {
    twolevel_predictor_table[i] = 0b0101010101010101; // All weakly not taken initialized
  }
}

bool GetPrediction_2level(UINT32 PC) 
{
  unsigned char  pht_index = PC & 0b111; // get first 3 bits
  unsigned short bht_index = (PC >> 3) & 0b111111111; // get next 9 bits
  // index histroy bits
  unsigned char  history_bit = twolevel_history_table[bht_index];
  // index private predictor table by history bit
  unsigned short pht_predictor_table = twolevel_predictor_table[history_bit];
  // find saturated counter by PHT_index
  unsigned char  saturated_counter = (pht_predictor_table >> (pht_index * 2)) & 0b11;
  // parse the saturated counter
  bool taken = saturated_counter >> 1;
  return taken;
}

void UpdatePredictor_2level(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) 
{
  // get all data in need first
  unsigned char  pht_index = PC & 0b111; // get first 3 bits
  unsigned short bht_index = (PC >> 3) & 0b111111111; // get next 9 bits
  // index histroy bits
  unsigned char  history_bit = twolevel_history_table[bht_index]; // range 0-63
  // index private predictor table by history bit
  unsigned short pht_predictor_table = twolevel_predictor_table[history_bit];
  // find saturated counter by PHT_index
  unsigned char  saturated_counter = (pht_predictor_table >> (pht_index * 2)) & 0b11;
  
  // Update history table
  twolevel_history_table[bht_index] = ((history_bit << 1) & 0b111111) | resolveDir;
  
  // Update saturated counter if needed
  if(resolveDir == predDir) // Correct prediction
  {
    if (saturated_counter == 0b10) // weakly taken correct
    {
      saturated_counter++;
    }
    else if (saturated_counter == 0b01) // weakly not taken correct
    {
      saturated_counter--;
    }
  }
  else // Misprediction
  {
    if (predDir)
    {
      saturated_counter--;
    }
    else
    {
      saturated_counter++;
    }
  }
  pht_predictor_table = (saturated_counter << (pht_index * 2)) | (pht_predictor_table & ~(0b11 << (pht_index * 2)));
  twolevel_predictor_table[history_bit] = pht_predictor_table;
}

/////////////////////////////////////////////////////////////
// openend
/////////////////////////////////////////////////////////////

unsigned long openend_history = 0; // This value is not stable (will change between getpred() and updatepred())
unsigned long get_history;         // So I will read this value from get_history at start of updatepred()
unsigned short openend_global_table[1024]; // 12bit PC-index 3-bit saturated counter table


unsigned short T1[1024] = {0}; //ttttttttcccuuuuu
unsigned short T2[1024] = {0};
unsigned short T3[1024] = {0};
unsigned short T4[1024] = {0};

unsigned short* openend_tables[4];
unsigned long mask[4] = {0xFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF};

char provider; // The provider of last prediction.

// Get 10bit index from history and pc
unsigned short IndexHash(unsigned long history, unsigned int pc) // 10 bit return
{
  // printf("%ld", history);
  unsigned long h = history;
  unsigned short tmp = (pc ^ (pc >> 10)) & 0b1111111111;
  for (int i = 0; i < 7; i++)
  {
    tmp = (tmp ^ h) & 0b1111111111;
    h = h >> 10;
  }
  return tmp;
}

// Get 8bit index from history and pc
unsigned char TagHash(unsigned long history, unsigned int pc) // 8 bit return
{
  unsigned long h = history;
  unsigned short tmp = (pc ^ (pc >> 8)) & 0b11111111;
  for (int i = 0; i < 8; i++)
  {
    tmp = (tmp ^ h) & 0b11111111;
    h = h >> 8;
  }
  return tmp;
}

// Get the prediction from tagged prediction table (return value will be in [pred, valid] 2-bit form)
unsigned char GetTablePrediction(unsigned short* table, unsigned short index, unsigned char tag)
{
  unsigned short element = table[index];
  unsigned char u = element & 0b11111;
  // unsigned char c = (element >> 5) & 0b111;
  unsigned char t = (element >> 8);
  if (t == tag && u > 0) 
  {
    return 0b1 | ((element >> 6) & 0b10); // T/NT, valid
  }
  return 0; // not valid output
}

// Update the tagged prediction table's u value
void UpdatePredictor(unsigned short* table, unsigned short index, unsigned char tag, bool resolveDir, bool predDir)
{
  unsigned short element = table[index];
  unsigned char u = element & 0b11111;
  unsigned char c = (element >> 5) & 0b111;
  unsigned char t = (element >> 8);
  if (u == 0 || t != tag) return;
  if (resolveDir == (c >> 2))
  {
    u = u == 31 ? u : u + 1;
  } 
  else
  {
    u--;
  }
  element = u | c << 5 | t << 8;
  table[index] = element;
}


// Update saturated counter of history provider
void UpdateProvider(unsigned short* table, unsigned short index, unsigned char tag, bool resolveDir, bool predDir)
{
  unsigned short element = table[index];
  unsigned char u = element & 0b11111;
  unsigned char c = (element >> 5) & 0b111;
  unsigned char t = (element >> 8);
  if (u == 0 || t != tag) return;
  if(resolveDir == predDir) // Correct prediction
  {
    if (c >= 0b100 && c != 0b111) // weakly taken correct
    {
      c++;
    }
    else if (c <= 0b011 && c != 0b000) // weakly not taken correct
    {
      c--;
    }
  }
  else // Misprediction
  {
    if (predDir)
    {
      c--;
    }
    else
    {
      c++;
    }
  }
  element = u | c << 5 | t << 8;
  table[index] = element;
}

// Try to create new entry in the table. If created, return true. If the entry is used or tale is full, return false
bool TryCreateNewHistory(unsigned short* table, unsigned short index, unsigned char tag, bool resolveDir, bool predDir) // success true else false
{
  unsigned short element = table[index];
  unsigned char u = element & 0b11111;
  unsigned char c = (element >> 5) & 0b111;
  unsigned char t = (element >> 8);
  if (u != 0)
  {
    if (t != tag) return false;
    printf("Exception! Matched history! %d, %d, %5d, %3d\n", provider, (int)((table - openend_tables[0])/1024), t, tag);
    return false;
  }
  u = 20; // Initial u value, optimal value.
  if (resolveDir)
  {
    c = 0b100;
  }
  else
  {
    c = 0b011;
  }
  t = tag;
  element = u | c << 5 | t << 8;
  table[index] = element;
  return true;
}

// Decrease u for all entries the matched index and tag.
void HistoryMatchDecrease(unsigned short* table, unsigned short index)
{
  unsigned short element = table[index];
  unsigned char u = element & 0b11111;
  unsigned char c = (element >> 5) & 0b111;
  unsigned char t = (element >> 8);
  u--;
  if (u < 0)
  {
    printf("Exception! History decrease u < 0.\n");
    u = 0;
  }
  element = u | c << 5 | t << 8;
  table[index] = element;
}


void InitPredictor_openend() 
{
  // Initialize predictor table

  for (int i = 0; i < 1024; i++)
  {
    openend_global_table[i] = 0b0011001100110011; // Initially, all weakly NT.
  }

  openend_tables[0] = T1;
  openend_tables[1] = T2;
  openend_tables[2] = T3;
  openend_tables[3] = T4;
}

bool GetPrediction_openend(UINT32 PC) 
{
  get_history = openend_history; // openend_history will change outside the loop
  unsigned char bit_index = PC & 0b11;
  unsigned short arr_index = (PC >> 2) & 0b1111111111; // Get next 10 bits
  unsigned char T0_result = (openend_global_table[arr_index] >> (4 * bit_index)) & 0b111;
  bool result = T0_result >> 2;
  provider = -1; // Indicate global PC predictor used
  for (int i = 0; i < 4; i++)
  {
    unsigned short index = IndexHash(openend_history & mask[i], PC);
    unsigned char tag = TagHash(openend_history & mask[i], PC);
    unsigned char tmp = GetTablePrediction(openend_tables[i], index, tag); 
    if (tmp & 0b1) // result is valid
    {
      result = tmp >> 1;
      provider = i;
    }
  }
  return result;
}

void UpdatePredictor_openend(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) 
{
  openend_history = get_history;
  // Update global predictor
  unsigned char bit_index = (PC >> 2) & 0b11;
  unsigned short arr_index = (PC >> 4) & 0b1111111111; // Get next 10 bits
  unsigned char T0_result = (openend_global_table[arr_index] >> (4 * bit_index)) & 0b111;
  if(resolveDir == T0_result>>2) // Correct prediction
  {
    if (T0_result >= 0b100 && T0_result != 0b111) // weakly taken correct
    {
      T0_result++;
    }
    else if (T0_result <= 0b011 && T0_result != 0b000) // weakly not taken correct
    {
      T0_result--;
    }
  }
  else // Misprediction
  {
    if (predDir)
    {
      T0_result--;
    }
    else
    {
      T0_result++;
    }
  }
  // Update counter into base predictor table
  openend_global_table[arr_index] = (T0_result << (bit_index * 2)) 
      | (openend_global_table[arr_index] & ~(0b11 << (bit_index * 2)));

  // Try to update tagged tables
  for (int i = 0; i < 4; i++)
  {
    unsigned short index = IndexHash(openend_history & mask[i], PC);
    unsigned char tag = TagHash(openend_history & mask[i], PC);
    UpdatePredictor(openend_tables[i], index, tag, resolveDir, predDir);
    if (provider == i)
    {
      UpdateProvider(openend_tables[i], index, tag, resolveDir, predDir);
      break;
    }
  }
  if (predDir != resolveDir)
  {
    if (provider != 3) // last table
    {
      int next_table = (int)provider + 1;
      while (next_table <= 3)
      {
        unsigned short index = IndexHash(openend_history & mask[next_table], PC);
        unsigned char tag = TagHash(openend_history & mask[next_table], PC);
        if (TryCreateNewHistory(openend_tables[next_table], index, tag, resolveDir, predDir)) break; // Create sucess
        next_table++;
      }
      if (next_table == 4) // Indicate create new history failed
      {
        for (int i = 0; i < 4; i++)
        {
          unsigned short index = IndexHash(openend_history & mask[i], PC);
          HistoryMatchDecrease(openend_tables[i], index);
        }
      }
    }
  }

  // Update history table
  openend_history = (get_history << 1) | resolveDir;
}

