#ifndef ARGS_H_0VY7RIP5
#define ARGS_H_0VY7RIP5
#include "argtable3/argtable3.h"

void* arg_append(void* a);
void add_base_args();
void parse_all_args(int argc, const char* argv[], const char* desc);
void arg_freeall();
#endif /* end of include guard: ARGS_H_0VY7RIP5 */
