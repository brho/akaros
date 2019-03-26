#!/bin/bash
# global-subd.sh
# barret rhoden 2012-03-07

# this is a wrapper for 'global' that allows limiting the search to a
# subdirectory of the pwd.  set the last argument to be the subdirectory, like
# with git grep.  due to some limitations with global's formatting options, we
# don't have the output sorted (by whatever mechanism they use), but it does
# still retain its relative path.
#
# one nice side effect of the directory argument is that you can use this
# script from outside of a gtags-managed directory, since we cd into the path.
# for example, you can be in ~you/ and call global-subd.sh PATH_TO_REPO
# ARGUMENTS, and it will run global within that directory.  not a big deal one
# way or another.
#
# as a side note, if you call global with a wildcard, you need to quote-escape
# it (single or double), to prevent the bash expansion.  e.g., cd user/parlib,
# global vcore* isn't what you want, you want global "vcore*" (or 'vcore*').  
# we still need to "" the args we pass to global (even for plain old global),
# so that the caller's quotes make it through.  also note that if there is bash
# expansion, it happens at the calling dir, not the cd'd dir

FIRST_ARG=$1
LAST_ARG=${BASH_ARGV[0]}

# Helper: we needed to drop our dir's trailing /, if there was one, and escape
# all intermediate ones for sed later.  This will make whatever path you send
# in correct for sed later.
set_sed_arg()
{
	SED_ARG="$1"
	# remove trailing /
	SED_ARG=`echo "$SED_ARG" | sed 's/\/$//'`
	# need 4 \ to get two \ in the sed, which outputs one \ (something to do
	# with the nested shell calls (``).  just need \/ to output \
	SED_ARG=`echo "$SED_ARG" | sed 's/\//\\\\\//g'`
}

# We try for the first arg being the dir, and then the last arg.  In either
# case, we call global with -l for local (this dir and children), and -n to
# stop stripping paths.  unfortunately, -n also stops the (decent?) sorting of
# the results...  sed finds our path and strips the leading "./".
# Note, we need the quotes on input, o/w the test will pass for empty input...
if [ -d "$FIRST_ARG" ]
then
	cd "$FIRST_ARG"
	set_sed_arg $FIRST_ARG
	# call global with the first arg taken off
	shift # have $@ forget arg1
	global -ln "$@" | sed "s/\.\/\($SED_ARG\)/\1/g"
elif [ -d "$LAST_ARG" ]
then
	cd "$LAST_ARG"
	set_sed_arg $LAST_ARG
	# call global with the last arg taken off.  need to do it in one line,
	# since bash sucks and it won't keep any quoted wildcards intact
	# otherwise (or will still muck with the arguments in some weird way).
	# this is similar to doing NEW_ARGS=${@:1:$LAST_MINUS_ONE}, and passing
	# NEW_ARGS
	LAST_MINUS_ONE=$(($#-1))
	global -ln "${@:1:$LAST_MINUS_ONE}" | sed "s/\.\/\($SED_ARG\)/\1/g"
else # plain old global
	global "$@"
fi
