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
  unsigned int   pc = PC >> 2; // Eliminate 2 most insignificant bits (always 0b00)
  unsigned char  bit_index = pc & 0b11;
  unsigned short arr_index = (pc >> 2) & 0b1111111111;
  // Get the counter from the table
  unsigned char  counter = (twobitsat_predictor_table[arr_index] >> (bit_index * 2)) & 0b11;
  // Determine taken or not
  bool taken = counter >> 1; // 00,01:NT, 10,11:T
    
  return taken;
}

void UpdatePredictor_2bitsat(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) 
{
  // Get all value first
  unsigned int   pc = PC >> 2; // Eliminate 2 most insignificant bits (always 0b00)
  unsigned char  bit_index = pc & 0b11;
  unsigned short arr_index = (pc >> 2) & 0b1111111111;
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
  unsigned int   pc = PC >> 2; // Eliminate 2 most insignificant bits (always 0b00)
  unsigned char  pht_index = pc & 0b111; // get first 3 bits
  unsigned short bht_index = (pc >> 3) & 0b111111111; // get next 9 bits
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
  unsigned int   pc = PC >> 2; // Eliminate 2 most insignificant bits (always 0b00)
  unsigned char  pht_index = pc & 0b111; // get first 3 bits
  unsigned short bht_index = (pc >> 3) & 0b111111111; // get next 9 bits
  // index histroy bits
  unsigned char  history_bit = twolevel_history_table[bht_index];
  // index private predictor table by history bit
  unsigned short pht_predictor_table = twolevel_predictor_table[history_bit];
  // find saturated counter by PHT_index
  unsigned char  saturated_counter = (pht_predictor_table >> (pht_index * 2)) & 0b11;
  
  // Update history table
  twolevel_history_table[bht_index] = ((history_bit << 1) & 0b111111111) | resolveDir;
  
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

unsigned long openend_history = 0;
unsigned char openend_global_table[1024];

unsigned char T1[256];
unsigned char T2[256];
unsigned char T3[256];
unsigned char T4[256];


unsigned char GetPredictorResult(unsigned char* table, unsigned char mapper)
{
  unsigned char element = *(table + mapper);
  bool result = (element & 0b1) | ((element >> 1) & 0b10);
  
  return result; // T/NT, U/NU
}

unsigned char TagMapper(unsigned long history, unsigned int pc)
{
  unsigned long repeat_pc = (long) pc | ((long) pc << 32);
  unsigned long unforded = repeat_pc ^ history;
  unsigned char ford = 0;

  for (int i = 0; i < 8; i++)
  {
    ford = ford ^ *(&unforded + i);
  }
  return ford;
}

void InitPredictor_openend() 
{
  // printf("%lu", sizeof(long));
  // Initialize all tables
  for (int i = 0; i < 256; i++)
  {
    T1[i] = 0b10;
    T2[i] = 0b10;
    T3[i] = 0b10;
    T4[i] = 0b10;
  }

  for (int i = 0; i < 1024; i++)
  {
    openend_global_table[i] = 0b01010101;
  }
}

bool GetPrediction_openend(UINT32 PC) 
{
  unsigned char bit_index = (PC >> 2) & 0b11;
  unsigned short arr_index = (PC >> 4) & 0b1111111111; // Get next 10 bits
  unsigned char T0_result = (openend_global_table[arr_index] >> (2 * bit_index)) & 0b11;

  bool result = T0_result >> 1;
  
  unsigned char* openend_tables[4];
  openend_tables[0] = T1;
  openend_tables[1] = T2;
  openend_tables[2] = T3;
  openend_tables[3] = T4;
  unsigned long mask[4] = {0xFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF};

  for (int i = 0; i < 4; i++)
  {
    unsigned char tmp = GetPredictorResult(openend_tables[i], TagMapper(openend_history & mask[i], PC));
    if (tmp & 0b1) // u = 1
    {
      result = tmp >> 1;
    }
  }
  return result;
}

void UpdatePredictor_openend(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget) 
{
  // Update global predictor
  unsigned char bit_index = (PC >> 2) & 0b11;
  unsigned short arr_index = (PC >> 4) & 0b1111111111; // Get next 10 bits
  unsigned char T0_result = (openend_global_table[arr_index] >> (2 * bit_index)) & 0b11;
  if(resolveDir == predDir) // Correct prediction
  {
    if (T0_result == 0b10) // weakly taken correct
    {
      T0_result++;
    }
    else if (T0_result == 0b01) // weakly not taken correct
    {
      T0_result--;
    }
  }
  else // Misprediction
  {
    if (predDir)  // 11 or 10
    {
      T0_result--;
    }
    else // 00 or 01
    {
      T0_result++;
    }
  }
  // Update counter into table
  openend_global_table[arr_index] = (T0_result << (bit_index * 2)) 
      | (openend_global_table[arr_index] & ~(0b11 << (bit_index * 2)));

  // Update history table
  openend_history = (openend_history << 1) | resolveDir;
}

