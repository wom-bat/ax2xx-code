#include <stdio.h>
#include <errno.h>
#include <wordexp.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "sd.h"
#include "crc-16.h"

#define DBG_PROMPT "AX211> "

struct dbg_state {
    int                 should_quit;
    struct sd_state     *sd;
};

/* These are SD commands as they get sent to the card */
enum protocol_code {
    cmd_null = 0,
    cmd_hello = 1,
    cmd_peek = 2,
    cmd_poke = 3,
    cmd_jump = 4,
};

struct debug_command {
    char    *name;
    char    *desc;
    char    *help;
    int (*func)(struct dbg_state *dbg, int argc, char **argv);
};


static int dbg_do_hello(struct dbg_state *dbg, int argc, char **argv);
static int dbg_do_null(struct dbg_state *dbg, int argc, char **argv);
static int dbg_do_peek(struct dbg_state *dbg, int argc, char **argv);
static int dbg_do_poke(struct dbg_state *dbg, int argc, char **argv);
static int dbg_do_jump(struct dbg_state *dbg, int argc, char **argv);
static int dbg_do_help(struct dbg_state *dbg, int argc, char **argv);

static struct debug_command debug_commands[] = {
    {
        .name = "hello",
        .func = dbg_do_hello,
        .desc = "Make sure the card is there",
        .help = "Takes up to four arguments.  All four arguments will "
                "be echoed back by the card.\n",
    },
    {
        .name = "peek",
        .func = dbg_do_peek,
        .desc = "Read an area of memory",
        .help = "Accepts two arguments:\n"
                "\tR1: Byte to peek from\n"
                "\tR2: Number of bytes to peek (up to 255)\n"
    },
    {
        .name = "poke",
        .func = dbg_do_poke,
        .desc = "Write to an area of memory",
        .help = "Accepts two arguments:\n"
                "\tR1: Address to poke\n"
                "\tR2: Byte to poke\n"
    },
    {
        .name = "jump",
        .func = dbg_do_jump,
        .desc = "Jump to an area of memory",
    },
    {
        .name = "null",
        .func = dbg_do_null,
        .desc = "Do nothing and return all zeroes",
        .help = "Make sure the card is responding.  Takes no commands,\n"
                "and returns all zeroes.\n",
    },
    {
        .name = "help",
        .func = dbg_do_help,
        .desc = "Print this help",
    },
    { },
};


static int dbg_txrx(struct dbg_state *dbg, enum protocol_code code,
                    uint8_t args[4], uint8_t *out, int outsize) {
    uint8_t ret[outsize+1]; // Two extra bytes for start and crc7
    uint8_t bfr[6];
    int tries;

    memset(ret, 0, sizeof(ret));
    code = (code&0x3f)|0x40;
    bfr[0] = code;
    bfr[1] = args[0];
    bfr[2] = args[1];
    bfr[3] = args[2];
    bfr[4] = args[3];
    bfr[5] = (crc7(bfr, 5)<<1)|1;

    xmit_mmc_cmd(dbg->sd, bfr, sizeof(bfr));
    tries = rcvr_mmc_cmd_start(dbg->sd, 16384);
    if (tries == -1) {
        printf("Never got start!\n");
        return -1;
    }
    rcvr_mmc_cmd(dbg->sd, ret, sizeof(ret));
    memcpy(out, ret+1, outsize);
    return 0;
}



static int dbg_do_hello(struct dbg_state *dbg, int argc, char **argv) {
    uint8_t args[4];
    uint8_t response[5];
    memset(args, 0, sizeof(args));
    if (argc > 1)
        args[0] = strtoul(argv[1], NULL, 0);
    if (argc > 2)
        args[1] = strtoul(argv[2], NULL, 0);
    if (argc > 3)
        args[2] = strtoul(argv[3], NULL, 0);
    if (argc > 4)
        args[3] = strtoul(argv[4], NULL, 0);

    dbg_txrx(dbg, cmd_hello, args, response, sizeof(response));

    printf("CPU -> AX211: {%02x %02x %02x %02x}\n", args[0], args[1], args[2], args[3]);
    printf("CPU <- AX211: {%02x %02x %02x %02x}\n", response[0], response[1], response[2], response[3]);

    return 0;
}

static int dbg_do_null(struct dbg_state *dbg, int argc, char **argv) {
    uint8_t args[4];
    uint8_t bfr[5];
    memset(args, 0, sizeof(args));
    dbg_txrx(dbg, cmd_null, args, bfr, sizeof(bfr));
    printf("Card response: {%02x %02x %02x %02x}\n", bfr[0], bfr[1], bfr[2], bfr[3]);
    return 0;
}

static int dbg_do_peek(struct dbg_state *dbg, int argc, char **argv) {
    int src_lo, src_hi, count;
    uint8_t args[4];

    if (argc < 3) {
        printf("Not enough arguments!  Usage: %s [addr] [count]\n",
                argv[0]);
        return -EINVAL;
    }

    src_lo = strtoul(argv[1], NULL, 0);
    count  = strtoul(argv[2], NULL, 0);

    if (count <= 0) {
        printf("Must specify at least 1 byte\n");
        return -ERANGE;
    }
    if (count > 256) {
        printf("Can read at most 255 bytes at a time\n");
    }

    src_hi = (src_lo>>8)&0xff;
    src_lo = (src_lo)&0xff;
    printf("Reading %d bytes from address 0x%02x%02x\n", count, src_lo, src_hi);
    args[0] = src_hi;
    args[1] = src_lo;
    args[2] = count;
    uint8_t bfr[count+1];
    memset(bfr, 0, sizeof(bfr));
    dbg_txrx(dbg, cmd_peek, args, bfr, sizeof(bfr));
    print_hex(bfr, sizeof(bfr)-1);
    return 0;
}

static int dbg_do_poke(struct dbg_state *dbg, int argc, char **argv) {
    int src_lo, src_hi, byte;
    uint8_t args[4];
    uint8_t bfr[5];

    if (argc < 3) {
        printf("Not enough arguments!  Usage: %s [addr] [count]\n",
                argv[0]);
        return -EINVAL;
    }

    src_lo = strtoul(argv[1], NULL, 0);
    byte  = strtoul(argv[2], NULL, 0);
    src_hi = (src_lo>>8)&0xff;
    src_lo = (src_lo)&0xff;
    args[0] = src_hi;
    args[1] = src_lo;
    args[2] = byte;
    dbg_txrx(dbg, cmd_poke, args, bfr, sizeof(bfr));
    printf("Offset 0x%02x%02x 0x%02x -> 0x%02x\n", bfr[0], bfr[1], bfr[2], byte);
    print_hex(bfr, sizeof(bfr)-1);
    return 0;
}

static int dbg_do_jump(struct dbg_state *dbg, int argc, char **argv) {
    printf("Jumping...\n");
    return 0;
}

static int dbg_do_help(struct dbg_state *dbg, int argc, char **argv) {
    struct debug_command *cmd = debug_commands;

    if (argc > 1) {
        int found = 0;
        while (cmd->func) {
            if (!strcmp(argv[1], cmd->name)) {
                found = 1;

                if (cmd->help) {
                    printf("Help for %s:\n", cmd->name);
                    printf(cmd->help);
                    printf("\n");
                }
                else {
                    printf("No help for \"%s\"\n", cmd->name);
                }
            }
            cmd++;
        }

        if (!found) {
            printf("Help not found for \"%s\"\n", argv[1]);
        }
    }

    else {
        printf("List of available commands:\n");
        while (cmd->func) {
            printf("%8s  %s\n", cmd->name, cmd->desc);
            cmd++;
        }
        printf("For more information on a specific command, type 'help [command]'\n");
    }


    return 0;
}




static char *cmd_completion_generator(const char *text, int state) {
    static int index;
    static int text_len;
    if (!state) {
        index = 0;
        text_len = strlen(text);
    }

    while (debug_commands[index].func) {
        if (!strncmp(debug_commands[index].name, text, text_len))
            return strdup(debug_commands[index++].name);
        index++;
    }
    return NULL;
}

char **cmd_completion(const char *text, int start, int end) {
    if (start != 0)
        return NULL;
    return rl_completion_matches(text, cmd_completion_generator);
}


int dbg_main(struct sd_state *sd) {
    struct dbg_state dbg;
    memset(&dbg, 0, sizeof(dbg));

    dbg.sd = sd;
    rl_attempted_completion_function = cmd_completion;
    rl_bind_key('\t', rl_complete);

    while (!dbg.should_quit) {
        char *cmd = readline(DBG_PROMPT);
        wordexp_t cmdline;
        int i;
        int ret = -ENOENT;
        int exp_res;

        // EOF (or ^D)
        if (!cmd) {
            printf("\n");
            return 0;
        }

        if (!*cmd)
            continue;

        add_history(cmd);
        exp_res = wordexp(cmd, &cmdline, 0);
        if (exp_res == WRDE_BADCHAR) {
            printf("Illegal character found in command\n");
            continue;
        }
        else if (exp_res == WRDE_BADVAL) {
            printf("Undefined variable in commaind\n");
            continue;
        }
        else if (exp_res == WRDE_CMDSUB) {
            printf("Did command substitution, and we weren't supposed to\n");
            continue;
        }
        else if (exp_res == WRDE_NOSPACE) {
            printf("Out of memory\n");
            continue;
        }
        else if (exp_res == WRDE_SYNTAX) {
            printf("Syntax error in command\n");
            continue;
        }
        else if (exp_res) {
            printf("Unrecognized error occurred in command parsing\n");
            continue;
        }


        for (i=0; debug_commands[i].func; i++)
            if (!strcmp(cmdline.we_wordv[0], debug_commands[i].name))
                ret = debug_commands[i].func(&dbg,
                        cmdline.we_wordc,
                        cmdline.we_wordv);

        if (ret == -ENOENT)
            printf("Command not found.  Type 'help' for a list of commands.\n");

        wordfree(&cmdline);
    }
    return 0;
}