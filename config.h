#ifndef configs
#define configs

#include <string>

extern int CPU_COUNT;
extern std::string scheduler;
extern int qCycles;
extern int processFrequency;
extern int MIN_INS;
extern int MAX_INS;
extern int delayPerExec;

extern int MAX_OVERALL_MEM;
extern int MEM_PER_FRAME;
extern int MIN_MEM_PER_PROC;
extern int MAX_MEM_PER_PROC;

extern int FRAME_COUNT;

extern unsigned short variable_a;
extern unsigned short variable_b;
extern unsigned short variable_c;

extern int cpuClocks;

extern bool process_maker_running;

#endif

// TODO: dont forget to include this where the vars are needed