#/bin/sh
# 
# Note: double hash comments indicates help documentation
#
## Syntax: setup.sh [-v|--verbose] [--local|--remote|ORG/REPO/BRANCH]
## 
## Setup the local system so you can run the `build.sh` script. When run
## standalone (non-local) this will download the setup procedure from the
## GridLAB-D repository.
## 
## Options:
## 
##   --local        Use the local files only (no downloads)
##
##   --remote       Use default origin (download from array-energy/gridlabd/master)
## 
##   -v|--verbose   Enable verbose command echo output
## 
##   -q|--quiet     Disable output to stdout
## 
##   -s|--silent    Disable output to stderr
## 
## Environment variables
## 
##   GRIDLABD_ORIGIN   Specifies the ORG/USER/BRANCH from which the downloads
##                     should obtained. Default is `arras-energy/gridlabd/master`
##
STDOUT=/dev/stdout
STDERR=/dev/stderr
DEFAULT_ORIGIN="https://raw.githubusercontent.com/arras-energy/gridlabd/master"
error () { echo "ERROR [setup.sh]: $*" > $STDERR ; exit 1; }
if [ -d $(dirname $0)/.git ]; then
	export GRIDLABD_ORIGIN="."
else
	export GRIDLABD_ORIGIN="$DEFAULT_ORIGIN"
fi
while [ $# -gt 0 ]; do
	case "$1" in
		-h|--help|help)
			cat $0 | grep '^## ' | cut -c4-
			exit 0
			;;
		-v|--verbose)
			set -x
			;;
		-q|--quiet)
			STDOUT=/dev/null
			;;
		-s|--silent)
			STDERR=/dev/null
			;;
		--local)
			export GRIDLABD_ORIGIN="."
			;;
		--remote)
			export GRIDLABD_ORIGIN="$DEFAULT_ORIGIN"
			;;
		*)
			export GRIDLABD_ORIGIN="$1"
			;;
	esac
	shift 1
done
export SYSTEMNAME=$(uname -s)
if [ "$GRIDLABD_ORIGIN" = "." ]; then
	cd $(dirname $0) 
	sh setup/$SYSTEMNAME.sh 1>$STDOUT 2>$STDERR || error "setup failed"
else
	curl --version 1>/dev/null 2>&1 || error "you must install curl first"
	test "$(echo $GRIDLABD_ORIGIN | cut -c-8)" != "https://" && GRIDLABD_ORIGIN=https://raw.githubusercontent.com/$GRIDLABD_ORIGIN
	echo GRIDLABD_ORIGIN=$GRIDLABD_ORIGIN
	if curl --retry 5 -H 'Cache-Control: no-cache' -fsL $GRIDLABD_ORIGIN/setup/$SYSTEMNAME.sh > /tmp/setup_$$.sh ; then
		trap "rm -f /tmp/setup_$$.sh" EXIT
		sh /tmp/setup_$$.sh 1>$STDOUT 2>$STDERR || error "setup failed"
	else
		error "unable to find $GRIDLABD_ORIGIN/setup/$SYSTEMNAME.sh. Manual setup required."
	fi		
fi
