#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <argp.h>

#include <parlib/parlib.h>
#include <parlib/vcore.h>

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
	{0, 0, 0, OPTION_DOC, "VAL for cores is a list, e.g. -v 1,3-5,9"},
	{0, 0, 0, OPTION_DOC, "To undo a core's provisioning, pass in pid=0."},
	{ 0 }
};

#define PROV_MODE_PID			1
#define PROV_MODE_CMD			2
#define PROV_MODE_SHOW			3

struct prog_args {
	char						**cmd_argv;
	int							cmd_argc;
	int							mode;		/* PROV_MODE_ETC */
	pid_t						pid;
	char						res_type;	/* cores (c), ram (m), etc */
	char						*res_val;	/* type-specific value, unparsed */
	struct core_set				cores;
	bool						max;
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

			pargs->cmd_argc = state->argc - state->next + 1;
			pargs->cmd_argv = malloc(sizeof(char*) * (pargs->cmd_argc + 1));
			assert(pargs->cmd_argv);
			pargs->cmd_argv[0] = arg;
			memcpy(&pargs->cmd_argv[1], &state->argv[state->next],
			       sizeof(char*) * (pargs->cmd_argc - 1));
			pargs->cmd_argv[pargs->cmd_argc] = NULL;
			state->next = state->argc;
			break;
		case 'v':
			/* could also check to make sure we're not -s (and vice versa) */
			if (pargs->dummy_val_flag) {
				printf("Provide only -v or -m, not both\n\n");
				argp_usage(state);
			}
			pargs->dummy_val_flag = 1;
			pargs->res_val = arg;
			break;
		case 'm':
			if (pargs->dummy_val_flag) {
				printf("Provide only -v or -m, not both\n\n");
				argp_usage(state);
			}
			pargs->dummy_val_flag = 1;
			pargs->max = true;
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
	switch (pargs->res_type) {
		case ('c'):
			if (pargs->max) {
				parlib_get_all_core_set(&pargs->cores);
			} else {
				if (!pargs->res_val) {
					printf("Need a list of cores to provision\n");
					return -1;
				}
				parlib_parse_cores(pargs->res_val, &pargs->cores);
			}
			provision_core_set(pid, &pargs->cores);
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
	return 0;
}

int main(int argc, char **argv, char **envp)
{
	struct prog_args pargs = {0};
	pid_t pid;

	argp_parse(&argp, argc, argv, 0, 0, &pargs);

	switch (pargs.mode) {
		case (PROV_MODE_PID):
			return prov_pid(pargs.pid, &pargs);
			break;
		case (PROV_MODE_CMD):
			pid = create_child_with_stdfds(pargs.cmd_argv[0], pargs.cmd_argc,
			                               pargs.cmd_argv, envp);
			if (pid < 0) {
				perror("Unable to spawn child");
				exit(-1);
			}
			if (prov_pid(pid, &pargs)) {
				perror("Unable to provision to child");
				sys_proc_destroy(pid, -1);
				exit(-1);
			}
			sys_proc_run(pid);
			waitpid(pid, NULL, 0);
			return 0;
		case (PROV_MODE_SHOW):
			printf("Show mode not supported yet, using ghetto interface\n\n");
			printf("Check 'dmesg' if you aren't on the console\n\n");
			sys_provision(-1, 0, 0);
			return 0;
			break;
		default:
			return -1;
	}
}
