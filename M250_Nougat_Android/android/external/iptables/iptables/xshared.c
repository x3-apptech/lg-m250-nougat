#include <getopt.h>
#include <libgen.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#include <xtables.h>
#include <math.h>
#include "xshared.h"

#define XT_SOCKET_NAME "xtables"
#define XT_SOCKET_LEN 8
#define BASE_MICROSECONDS	100000

/*
 * Print out any special helps. A user might like to be able to add a --help
 * to the commandline, and see expected results. So we call help for all
 * specified matches and targets.
 */
void print_extension_helps(const struct xtables_target *t,
    const struct xtables_rule_match *m)
{
	for (; t != NULL; t = t->next) {
		if (t->used) {
			printf("\n");
			if (t->help == NULL)
				printf("%s does not take any options\n",
				       t->name);
			else
				t->help();
		}
	}
	for (; m != NULL; m = m->next) {
		printf("\n");
		if (m->match->help == NULL)
			printf("%s does not take any options\n",
			       m->match->name);
		else
			m->match->help();
	}
}

const char *
proto_to_name(uint8_t proto, int nolookup)
{
	unsigned int i;

	if (proto && !nolookup) {
		struct protoent *pent = getprotobynumber(proto);
		if (pent)
			return pent->p_name;
	}

	for (i = 0; xtables_chain_protos[i].name != NULL; ++i)
		if (xtables_chain_protos[i].num == proto)
			return xtables_chain_protos[i].name;

	return NULL;
}

static struct xtables_match *
find_proto(const char *pname, enum xtables_tryload tryload,
	   int nolookup, struct xtables_rule_match **matches)
{
	unsigned int proto;

	if (xtables_strtoui(pname, NULL, &proto, 0, UINT8_MAX)) {
		const char *protoname = proto_to_name(proto, nolookup);

		if (protoname)
			return xtables_find_match(protoname, tryload, matches);
	} else
		return xtables_find_match(pname, tryload, matches);

	return NULL;
}

/*
 * Some explanations (after four different bugs in 3 different releases): If
 * we encounter a parameter, that has not been parsed yet, it's not an option
 * of an explicitly loaded match or a target. However, we support implicit
 * loading of the protocol match extension. '-p tcp' means 'l4 proto 6' and at
 * the same time 'load tcp protocol match on demand if we specify --dport'.
 *
 * To make this work, we need to make sure:
 * - the parameter has not been parsed by a match (m above)
 * - a protocol has been specified
 * - the protocol extension has not been loaded yet, or is loaded and unused
 *   [think of ip6tables-restore!]
 * - the protocol extension can be successively loaded
 */
static bool should_load_proto(struct iptables_command_state *cs)
{
	if (cs->protocol == NULL)
		return false;
	if (find_proto(cs->protocol, XTF_DONT_LOAD,
	    cs->options & OPT_NUMERIC, NULL) == NULL)
		return true;
	return !cs->proto_used;
}

struct xtables_match *load_proto(struct iptables_command_state *cs)
{
	if (!should_load_proto(cs))
		return NULL;
	return find_proto(cs->protocol, XTF_TRY_LOAD,
			  cs->options & OPT_NUMERIC, &cs->matches);
}

int command_default(struct iptables_command_state *cs,
		    struct xtables_globals *gl)
{
	struct xtables_rule_match *matchp;
	struct xtables_match *m;

	if (cs->target != NULL &&
	    (cs->target->parse != NULL || cs->target->x6_parse != NULL) &&
	    cs->c >= cs->target->option_offset &&
	    cs->c < cs->target->option_offset + XT_OPTION_OFFSET_SCALE) {
		xtables_option_tpcall(cs->c, cs->argv, cs->invert,
				      cs->target, &cs->fw);
		return 0;
	}

	for (matchp = cs->matches; matchp; matchp = matchp->next) {
		m = matchp->match;

		if (matchp->completed ||
		    (m->x6_parse == NULL && m->parse == NULL))
			continue;
		if (cs->c < matchp->match->option_offset ||
		    cs->c >= matchp->match->option_offset + XT_OPTION_OFFSET_SCALE)
			continue;
		xtables_option_mpcall(cs->c, cs->argv, cs->invert, m, &cs->fw);
		return 0;
	}

	/* Try loading protocol */
	m = load_proto(cs);
	if (m != NULL) {
		size_t size;

		cs->proto_used = 1;

		size = XT_ALIGN(sizeof(struct xt_entry_match)) + m->size;

		m->m = xtables_calloc(1, size);
		m->m->u.match_size = size;
		strcpy(m->m->u.user.name, m->name);
		m->m->u.user.revision = m->revision;
		xs_init_match(m);

		if (m->x6_options != NULL)
			gl->opts = xtables_options_xfrm(gl->orig_opts,
							gl->opts,
							m->x6_options,
							&m->option_offset);
		else
			gl->opts = xtables_merge_options(gl->orig_opts,
							 gl->opts,
							 m->extra_opts,
							 &m->option_offset);
		if (gl->opts == NULL)
			xtables_error(OTHER_PROBLEM, "can't alloc memory!");
		optind--;
		/* Indicate to rerun getopt *immediately* */
 		return 1;
	}

	if (cs->c == ':')
		xtables_error(PARAMETER_PROBLEM, "option \"%s\" "
		              "requires an argument", cs->argv[optind-1]);
	if (cs->c == '?')
		xtables_error(PARAMETER_PROBLEM, "unknown option "
			      "\"%s\"", cs->argv[optind-1]);
	xtables_error(PARAMETER_PROBLEM, "Unknown arg \"%s\"", optarg);
	return 0;
}

static mainfunc_t subcmd_get(const char *cmd, const struct subcommand *cb)
{
	for (; cb->name != NULL; ++cb)
		if (strcmp(cb->name, cmd) == 0)
			return cb->main;
	return NULL;
}

int subcmd_main(int argc, char **argv, const struct subcommand *cb)
{
	const char *cmd = basename(*argv);
	mainfunc_t f = subcmd_get(cmd, cb);

	if (f == NULL && argc > 1) {
		/*
		 * Unable to find a main method for our command name?
		 * Let's try again with the first argument!
		 */
		++argv;
		--argc;
		f = subcmd_get(*argv, cb);
	}

	/* now we should have a valid function pointer */
	if (f != NULL)
		return f(argc, argv);

	fprintf(stderr, "ERROR: No valid subcommand given.\nValid subcommands:\n");
	for (; cb->name != NULL; ++cb)
		fprintf(stderr, " * %s\n", cb->name);
	exit(EXIT_FAILURE);
}

void xs_init_target(struct xtables_target *target)
{
	if (target->udata_size != 0) {
		free(target->udata);
		target->udata = calloc(1, target->udata_size);
		if (target->udata == NULL)
			xtables_error(RESOURCE_PROBLEM, "malloc");
	}
	if (target->init != NULL)
		target->init(target->t);
}

void xs_init_match(struct xtables_match *match)
{
	if (match->udata_size != 0) {
		/*
		 * As soon as a subsequent instance of the same match
		 * is used, e.g. "-m time -m time", the first instance
		 * is no longer reachable anyway, so we can free udata.
		 * Same goes for target.
		 */
		free(match->udata);
		match->udata = calloc(1, match->udata_size);
		if (match->udata == NULL)
			xtables_error(RESOURCE_PROBLEM, "malloc");
	}
	if (match->init != NULL)
		match->init(match->m);
}

bool xtables_lock(int wait, struct timeval *wait_interval)
{
	int i = 0, ret, xt_socket;
	struct sockaddr_un xt_addr;
	struct timeval time_left, wait_time, waited_time;

	time_left.tv_sec = wait;
	time_left.tv_usec = 0;
	waited_time.tv_sec = 0;
	waited_time.tv_usec = 0;

/* 2014-12-18 kyungsu.mok@lge.com LGP_DATA_TOOLS_IPTABLES_FORCE_WAIT_OPTION [START] */
#ifdef LGP_DATA_TOOLS_IPTABLES_FORCE_WAIT_OPTION
    int retry_cnt = 0;
#endif
/* 2014-12-18 kyungsu.mok@lge.com LGP_DATA_TOOLS_IPTABLES_FORCE_WAIT_OPTION [END] */

	memset(&xt_addr, 0, sizeof(xt_addr));
	xt_addr.sun_family = AF_UNIX;
	strcpy(xt_addr.sun_path+1, XT_SOCKET_NAME);
	xt_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	/* If we can't even create a socket, fall back to prior (lockless) behavior */
	if (xt_socket < 0)
		return true;

	while (1) {
		ret = bind(xt_socket, (struct sockaddr*)&xt_addr,
			   offsetof(struct sockaddr_un, sun_path)+XT_SOCKET_LEN);
		if (ret == 0)
			return true;
		if (++i % 10 == 0) {
			if (wait != -1)
				fprintf(stderr, "Another app is currently holding the xtables lock; "
					"still %lds %ldus time ahead to have a chance to grab the lock...\n",
					time_left.tv_sec, time_left.tv_usec);
			else
				fprintf(stderr, "Another app is currently holding the xtables lock; "
						"waiting for it to exit...\n");
		}

		wait_time = *wait_interval;
		select(0, NULL, NULL, NULL, &wait_time);
		if (wait == -1)
			continue;

		timeradd(&waited_time, wait_interval, &waited_time);
		timersub(&time_left, wait_interval, &time_left);
		if (!timerisset(&time_left))
			return false;

/* 2014-12-18 kyungsu.mok@lge.com LGP_DATA_TOOLS_IPTABLES_FORCE_WAIT_OPTION [START] */
#ifdef LGP_DATA_TOOLS_IPTABLES_FORCE_WAIT_OPTION
        if (retry_cnt == 15) {	/* max wait 3sec */
            fprintf(stderr, "Another app is currently holding reach max error(%d)", retry_cnt);
            return false;
        }

        retry_cnt++;

        fprintf(stderr, "Another app is currently holding the xtables lock; retry_cnt(%d)"
                "waiting for it to exit...\n", retry_cnt);
        usleep(200000); // 200ms
#else
		if (++i % 2 == 0)
			fprintf(stderr, "Another app is currently holding the xtables lock; "
				"waiting for it to exit...\n");
		sleep(1);
#endif
/* 2014-12-18 kyungsu.mok@lge.com LGP_DATA_TOOLS_IPTABLES_FORCE_WAIT_OPTION [END] */
	}
}

void parse_wait_interval(const char *str, struct timeval *wait_interval)
{
	unsigned int usec;
	int ret;

	ret = sscanf(str, "%u", &usec);
	if (ret == 1) {
		if (usec > 999999)
			xtables_error(PARAMETER_PROBLEM,
				      "too long usec wait %u > 999999 usec",
				      usec);

		wait_interval->tv_sec = 0;
		wait_interval->tv_usec = usec;
		return;
	}
	xtables_error(PARAMETER_PROBLEM, "wait interval not numeric");
}
