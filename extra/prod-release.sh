#!/bin/zsh

SAVE=`pwd`

function run_cmd {
    "$@"
    local rc=$?
    if [ $rc -ne 0 ]; then
        echo "error with $1" >&2
				cd $SAVE
				exit 1
    fi
    return $rc
}

BASE=${HOME}/devel/janice
MCP=${BASE}/mcp
MCP_STATIC=${MCP}/assets/static
MCR=${BASE}/mcr_esp
ANGULAR=${MCP}/assets/angular
BUNDLES=${MCP_STATIC}/bundles

[[ ! -x $BASE ]] && exit 1

rm -f $BASE/erl_crash.dump

cd $MCP
[[ ! -f $MCP/mix.exs ]] && exit 1
run_cmd mix clean --only=prod
run_cmd env MIX_ENV=prod mix deps.get
run_cmd env MIX_ENV=prod mix deps.clean --unused
run_cmd env MIX_ENV=prod mix compile

cd $ANGULAR
print "building angular ui..."
rm -rf ${BUNDLES} 1> /dev/null 2>&1
run_cmd mkdir -p ${BUNDLES} 1> /dev/null 2>&1
run_cmd ng build --verbose --prod --env=prod --build-optimizer

cd $MCP/assets
print "building integrated ui..."
run_cmd brunch build --debug --production

cd $MCP
run_cmd env MIX_ENV=prod mix phx.digest

cd $MCR
print -n "building mcr_esp..."
run_cmd make app-clean 1> /dev/null
run_cmd make -j9 deploy-to-mcp 1> /dev/null && print " done"

cd $MCP
run_cmd env MIX_ENV=prod mix release --env=prod

release=($HOME/devel/janice/mcp/_build/prod/rel/mcp/releases/0.*(om[1]))

PKG="mcp.tar.gz"
STAGE=/run/janice
if [[ -e $release && -d $STAGE ]]; then
	cp $release/$PKG $STAGE

	if [[ -f $STAGE/$PKG ]]; then
		echo "build complete, release staged in $STAGE"
	fi
fi

cd $SAVE
