#include <stdlib.h>
#include "args.h"
#include "./argtable3.h"

static int argindex = 0;
static void* argtable[1000] = {0};

//static struct arg_lit *verb, *help, *version;
static struct arg_lit *help, *version;
static struct arg_end *end;

void* arg_append(void* a)
{
    return argtable[argindex++] = a;
}

void add_base_args() {
    arg_append(help = 
            arg_litn(NULL, "help", 0, 1, "display this help and exit"));
    arg_append(version =
            arg_litn(NULL, "version", 0, 1, "display version info and exit"));
}

void parse_all_args(int argc, const char* argv[], const char* desc)
{
    argtable[argindex++] = 
        end = arg_end(20);

    const char* progname = argv[0];

    int nerrors;
    nerrors = arg_parse(argc, (char**)argv, argtable);

    /* special case: '--help' takes precedence over error reporting */
    if (help->count > 0) {
        printf("Usage: %s", progname);
        arg_print_syntax(stdout, argtable, "\n");
        printf("%s\n\n", desc);
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        exit(0);
    }

    /* If the parser returned any errors then display them and exit */
    if (nerrors > 0) {
        /* Display the error details contained in the arg_end struct.*/
        arg_print_errors(stdout, end, progname);
        printf("Try '%s --help' for more information.\n", progname);
        exit(1);
        goto exit;
    }

exit:
    /* deallocate each non-null entry in argtable[] */
    return;
}

void arg_freeall()
{
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
}
