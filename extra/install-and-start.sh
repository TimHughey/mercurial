#!/bin/zsh

	save_cwd=`pwd`

	function run_cmd {
			"$@"
			local rc=$?
			if [ $rc -ne 0 ]; then
					echo "error with $1" >&2
					cd $save_cwd
					exit 1
			fi
			return $rc
	}

	if [[ $1 == "--clean" ]]; then
		clean=1
	fi


	setopt local_options rm_star_silent

	# configuration variables
	mcp_base=${HOME}/devel/janice/mcp
	jan_base=/usr/local/janice
	jan_bin=$jan_base/bin
	release=/run/janice/mcp.tar.gz

	if [[ ! -f $release ]]; then
		print "deploy tar $release doesn't exist, doing nothing."
		return 1
	fi

	$jan_bin/mcp ping 1> /dev/null 2>&1
	if [[ $? -eq 0 ]]; then
		print -n "stopping jan before install..."
		run_cmd $jan_base/bin/mcp stop 1> /dev/null 2>&1 && print " done."
	fi

	cd $mcp_base
	run_cmd env MIX_ENV=prod mix ecto.migrate
	cd $save_cwd

	if [[ $clean -eq 1 ]]; then
		print -n "cleaning up $jan_base..." && rm -rf $jan_base/* && print " done."
	else
		print "won't clean existing release, use --clean to do so"
	fi

	print -n "untarring $release..."
	run_cmd tar -C $jan_base -xf $release && print " done."

	print -n "removing deploy tar..." && rm -f $release && print " done."
	print "starting jan then running tail. (use CTRL+C to stop)"

	env PORT=4009 mcp start
	sleep 2
	exec tail --lines=100 -f $jan_base/var/log/erlang.*(om[1])