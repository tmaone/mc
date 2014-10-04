#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "parse.h"
#include "opt.h"
#include "asm.h"

#include "../config.h"

/* FIXME: move into one place...? */
Node *file;
char debugopt[128];
int writeasm;
char *outfile;
char **incpaths;
size_t nincpaths;

static void usage(char *prog)
{
    printf("%s [-h] [-o outfile] [-d[dbgopts]] inputs\n", prog);
    printf("\t-h\tPrint this help\n");
    printf("\t-S\tWrite out `input.s` when compiling\n");
    printf("\t-I path\tAdd 'path' to use search path\n");
    printf("\t-d\tPrint debug dumps. Recognized options: f r p i\n");
    printf("\t\t\tf: log folded trees\n");
    printf("\t\t\tl: log lowered pre-cfg trees\n");
    printf("\t\t\tT: log tree immediately\n");
    printf("\t\t\tr: log register allocation activity\n");
    printf("\t\t\ti: log instruction selection activity\n");
    printf("\t\t\tu: log type unifications\n");
    printf("\t-o\tOutput to outfile\n");
    printf("\t-S\tGenerate assembly instead of object code\n");
}

static void assem(char *asmsrc, char *path)
{
    char *asmcmd[] = Asmcmd;
    char objfile[1024];
    char *psuffix;
    char **p, **cmd;
    size_t ncmd;
    int pid, status;

    psuffix = strrchr(path, '+');
    if (psuffix != NULL)
        swapsuffix(objfile, 1024, path, psuffix, ".o");
    else
        swapsuffix(objfile, 1024, path, ".myr", ".o");
    cmd = NULL;
    ncmd = 0;
    for (p = asmcmd; *p != NULL; p++)
        lappend(&cmd, &ncmd, *p);
    lappend(&cmd, &ncmd, objfile);
    lappend(&cmd, &ncmd, asmsrc);
    lappend(&cmd, &ncmd, NULL);

    pid = fork();
    if (pid == -1) {
        die("couldn't fork");
    } else if (pid == 0) {
        if (execvp(cmd[0], cmd) == -1)
            die("Couldn't exec assembler\n");
    } else {
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            die("Couldn't run assembler");
    }
}

static char *gentemp(char *buf, size_t bufsz, char *path, char *suffix)
{
    char *tmpdir;
    char *base;
    struct timeval tv;

    tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";
    base = strrchr(path, '/');
    if (base)
        base++;
    else
        base = path;
    gettimeofday(&tv, NULL);
    srand(tv.tv_usec);
    snprintf(buf, bufsz, "%s/tmp%lx%lx-%s%s", tmpdir, (long)rand(), (long)tv.tv_usec, base, suffix);
    return buf;
}

static void genuse(char *path)
{
    FILE *f;
    char buf[1024];
    char *psuffix;

    psuffix = strrchr(path, '+');
    if (psuffix != NULL)
        swapsuffix(buf, 1024, path, psuffix, ".use");
    else
        swapsuffix(buf, 1024, path, ".myr", ".use");
    f = fopen(buf, "w");
    if (!f) {
        fprintf(stderr, "Could not open path %s\n", buf);
        exit(1);
    }
    writeuse(f, file);
    fclose(f);
}

int main(int argc, char **argv)
{
    int opt;
    int i;
    Stab *globls;
    char buf[1024];
    while ((opt = getopt(argc, argv, "d:hSo:I:")) != -1) {
        switch (opt) {
            case 'o':
                outfile = optarg;
                break;
            case 'S':
                writeasm = 1;
                break;
            case 'h':
                usage(argv[0]);
                exit(0);
                break;
            case 'd':
                while (optarg && *optarg) {
                    if (*optarg == 'y')
                        yydebug = 1;
                    debugopt[*optarg++ & 0x7f]++;
                }
                break;
            case 'I':
                lappend(&incpaths, &nincpaths, optarg);
                break;
            default:
                usage(argv[0]);
                exit(0);
                break;
        }
    }

    lappend(&incpaths, &nincpaths, Instroot "/lib/myr");
    for (i = optind; i < argc; i++) {
        globls = mkstab();
        tyinit(globls);
        tokinit(argv[i]);
        file = mkfile(argv[i]);
        file->file.exports = mkstab();
        file->file.globls = globls;
        yyparse();

        /* before we do anything to the parse */
        if (debugopt['T'])
            dump(file, stdout);
        infer(file);
        tagexports(file->file.exports, 0);
        /* after all type inference */
        if (debugopt['t'])
            dump(file, stdout);

        if (writeasm) {
            swapsuffix(buf, sizeof buf, argv[i], ".myr", ".s");
        } else {
            gentemp(buf, sizeof buf, argv[i], ".s");
        }
        gen(file, buf);
        assem(buf, argv[i]);
        genuse(argv[i]);
    }

    return 0;
}
