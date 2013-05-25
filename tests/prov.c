#include <stdio.h>
#include <stdlib.h>
#include <argp.h>

#include <parlib.h>
#include <vcore.h>

const char *argp_program_version = "prov v0.1475263";
const char *argp_program_bug_address = "<akaros@lists.eecs.berkeley.edu>";

static char doc[] = "prov -- control for provisioning resources";
static char args_doc[] = "-p PID\n-c PROGRAM [ARGS]\nPROGRAM [ARGS]\n"
                         "-- PROGRAM [ARGS]\n-s";

static struct argp_option options[] = {
	{"type",		't', "TYPE",0, "Type of resource to provision"},
	{"Possible types:", 0, 0, OPTION_DOC | OPTION_NO_USAGE, "c = cores\n"
	                                                        "m = ram"},
	{0, 0, 0, 0, "Call with exactly one of these, or with a program and args:"},
	{"pid",			'p', "PID",	OPTION_NO_USAGE, "Pid of process to provision "
	                                             "resources to"},
	{0, 0, 0, 0, ""},
	{"command",		'c', "PROG",OPTION_NO_USAGE, "Launch a program and "
	                                             "provision (alternate)"},
	{0, 0, 0, 0, ""},
	{"show",		's', 0,		OPTION_NO_USAGE, "Show current resource "
	                                             "provisioning"},
	{0, 0, 0, OPTION_DOC, "If your command has arguments that conflict with "
	                      "prov, then put them after -- to keep prov from "
	                      "interpretting them."},
	{0, 0, 0, 0, "Call with exactly one of these when changing a provision:"},
	{"value",		'v', "VAL",	0, "Type-specific value, passed to the kernel"},
	{"max",			'm', 0,		0, "Provision all resources of the given type"},
	{0, 0, 0, OPTION_DOC, "Cores are provisioned to processes, so the value is "
	                      "a specific pcore id.  To undo a core's "
	                      "provisioning, pass in pid=0."},
	{ 0 }
};

#define PROV_MODE_PID			1
#define PROV_MODE_CMD			2
#define PROV_MODE_SHOW			3

struct prog_args {
	char						*cmd;
	char						**cmd_args;
	int							mode;		/* PROV_MODE_ETC */
	pid_t						pid;
	char						res_type;	/* cores (c), ram (m), etc */
	long						res_val;	/* type-specific value */
	int							max;
	int							dummy_val_flag;
};

static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
	struct prog_args *pargs = state->input;
	switch (key) {
		case 't':
			pargs->res_type = arg[0];
			if (arg[1] != '\0')
				printf("Warning, extra letters detected for -t's argument\n");
			break;
		case 'p':
			if (pargs->mode) {
				printf("Too many modes given (-p, -s, COMMAND, etc)\n\n");
				argp_usage(state);
			}
			pargs->mode = PROV_MODE_PID;
			pargs->pid = atoi(arg);
			break;
		case 's':
			if (pargs->mode) {
				printf("Too many modes given (-p, -s, COMMAND, etc)\n\n");
				argp_usage(state);
			}
			pargs->mode = PROV_MODE_SHOW;
			break;
		case 'c':
		case ARGP_KEY_ARG:
			if (pargs->mode) {
				printf("Too many modes given (-p, -s, COMMAND, etc)\n\n");
				argp_usage(state);
			}
			pargs->mode = PROV_MODE_CMD;
			pargs->cmd = arg;
			/* Point to the next arg.  We can also check state->arg_num, which
			 * is how many non-arpg arguments there are */
			pargs->cmd_args = &state->argv[state->next];
			/* Consume all args (it's done when next == argc) */
			state->next = state->argc;
			break;
		case 'v':
			/* could also check to make sure we're not -s (and vice versa) */
			if (pargs->dummy_val_flag) {
				printf("Provide only -v or -m, not both\n\n");
				argp_usage(state);
			}
			pargs->dummy_val_flag = 1;
			pargs->res_val = atol(arg);
			break;
		case 'm':
			if (pargs->dummy_val_flag) {
				printf("Provide only -v or -m, not both\n\n");
				argp_usage(state);
			}
			pargs->dummy_val_flag = 1;
			pargs->max = 1;
			break;
		case ARGP_KEY_END:
			/* Make sure we selected a mode */
			if (!pargs->mode) {
				printf("No mode selected (-p, -s, etc).\n\n");
				argp_usage(state);
			}
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = {options, parse_opt, args_doc, doc};

/* Used by both -p and -c modes (-c will use it after creating pid) */
static int prov_pid(pid_t pid, struct prog_args *pargs)
{
	unsigned int kernel_res_type;
	int retval;
	switch (pargs->res_type) {
		case ('c'):
			if (pargs->max) {
				/* TODO: don't guess the LL/CG layout and num pcores */
				#if 1
				for (int i = 1; i < max_vcores() + 1; i++) {
					if ((retval = sys_provision(pid, RES_CORES, i))) {
						perror("Failed max provisioning");
						return retval;
					}
				}
				#else
				/* To force a vcore shuffle / least optimal ordering, change
				 * the if 1 to 0.  Normally, we provision out in a predictable,
				 * VCn->PCn+1 ordering.  This splits the odd and even VCs
				 * across sockets on a 32 PC machine (c89).  This is only for
				 * perf debugging, when using the lockprov.sh script. */
				retval = 0;
				retval |= sys_provision(pid, RES_CORES,  1);
				retval |= sys_provision(pid, RES_CORES, 16);
				retval |= sys_provision(pid, RES_CORES,  2);
				retval |= sys_provision(pid, RES_CORES, 17);
				retval |= sys_provision(pid, RES_CORES,  3);
				retval |= sys_provision(pid, RES_CORES, 18);
				retval |= sys_provision(pid, RES_CORES,  4);
				retval |= sys_provision(pid, RES_CORES, 19);
				retval |= sys_provision(pid, RES_CORES,  5);
				retval |= sys_provision(pid, RES_CORES, 20);
				retval |= sys_provision(pid, RES_CORES,  6);
				retval |= sys_provision(pid, RES_CORES, 21);
				retval |= sys_provision(pid, RES_CORES,  7);
				retval |= sys_provision(pid, RES_CORES, 22);
				retval |= sys_provision(pid, RES_CORES,  8);
				retval |= sys_provision(pid, RES_CORES, 23);
				retval |= sys_provision(pid, RES_CORES,  9);
				retval |= sys_provision(pid, RES_CORES, 24);
				retval |= sys_provision(pid, RES_CORES, 10);
				retval |= sys_provision(pid, RES_CORES, 25);
				retval |= sys_provision(pid, RES_CORES, 11);
				retval |= sys_provision(pid, RES_CORES, 26);
				retval |= sys_provision(pid, RES_CORES, 12);
				retval |= sys_provision(pid, RES_CORES, 27);
				retval |= sys_provision(pid, RES_CORES, 13);
				retval |= sys_provision(pid, RES_CORES, 28);
				retval |= sys_provision(pid, RES_CORES, 14);
				retval |= sys_provision(pid, RES_CORES, 29);
				retval |= sys_provision(pid, RES_CORES, 15);
				retval |= sys_provision(pid, RES_CORES, 31);
				retval |= sys_provision(pid, RES_CORES, 30);
				return retval;
				#endif
			} else {
				if ((retval = sys_provision(pid, RES_CORES, pargs->res_val))) {
					perror("Failed single provision");
					return retval;
				}
			}
			kernel_res_type = RES_CORES;
			break;
		case ('m'):
			printf("Provisioning memory is not supported yet\n");
			return -1;
			break;
		default:
			if (!pargs->res_type)
				printf("No resource type selected.  Use -t\n");
			else
				printf("Unsupported resource type %c\n", pargs->res_type);
			return -1;
	}
	sys_poke_ksched(pid, kernel_res_type);
	return 0;
}

int main(int argc, char **argv)
{

	struct prog_args pargs = {0};

	argp_parse(&argp, argc, argv, 0, 0, &pargs);

	switch (pargs.mode) {
		case (PROV_MODE_PID):
			return prov_pid(pargs.pid, &pargs);
			break;
		case (PROV_MODE_CMD):
			printf("Launching programs not supported yet\n");
			printf("Would have launched %s with args:", pargs.cmd);
			if (pargs.cmd_args)
				for (int i = 0; pargs.cmd_args[i]; i++)
					printf(" %s", pargs.cmd_args[i]);
			printf("\n");
			return 0;
			break;
		case (PROV_MODE_SHOW):
			printf("Show mode not supported yet, using ghetto interface\n\n");
			sys_provision(-1, 0, 0);
			return 0;
			break;
		default:
			return -1;
	}
}
