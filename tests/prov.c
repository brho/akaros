#include <stdio.h>
#include <stdlib.h>
#include <argp.h>

#include <parlib.h>
#include <vcore.h>

const char *argp_program_version = "prov v0.1475263";
const char *argp_program_bug_address = "<akaros@lists.eecs.berkeley.edu>";

static char doc[] = "prov -- control for provisioning resources";
static char args_doc[] = "-p PID\nPROGRAM [ARGS]\n-- PROGRAM [ARGS]\n-s";

static struct argp_option options[] = {
	{"type",		't', "TYPE",0, "Type of resource to provision"},
	{"Possible types:", 0, 0, OPTION_DOC, "c = cores\nm = ram"},
	{0, 0, 0, 0, "Call with exactly one of these, or with a program and args:"},
	{"pid",			'p', "PID",	0, "Pid of process to provision resources to"},
	{"show",		's', 0,		0, "Show current resource provisioning"},
	{"command",		'c', "PROG",0, "Launch a program and provision (alternate)"},
	{0, 0, 0, OPTION_DOC, "If your command has arguments that conflict with prov, then put them after -- to keep prov from interpretting them."},
	{0, 0, 0, 0, "Call with exactly one of these when changing a provision:"},
	{"value",		'v', "VAL",	0, "Type-specific value, passed to the kernel"},
	{"max",			'm', 0,		0, "Provision all resources of the given type"},
	{0, 0, 0, OPTION_DOC, "Cores are provisioned to processes, so the value is a specific pcore id.  To undo a core's provisioning, pass in pid=0."},

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
	int retval = 0;
	switch (pargs->res_type) {
		case ('c'):
			if (pargs->max) {
				/* TODO: don't guess the LL/CG layout and num pcores */
				for (int i = 1; i < max_vcores() + 1; i++) {
					if (retval = sys_provision(pid, RES_CORES, i)) {
						perror("Failed max provisioning");
						return retval;
					}
				}
			} else {
				if (retval = sys_provision(pid, RES_CORES, pargs->res_val)) {
					perror("Failed single provision");
					return retval;
				}
			}
			break;
		case ('m'):
			printf("Provisioning memory is not supported yet\n");
			break;
		default:
			if (!pargs->res_type)
				printf("No resource type selected.  Use -t\n");
			else
				printf("Unsupported resource type %c\n", pargs->res_type);
			return -1;
	}
	return retval;
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
