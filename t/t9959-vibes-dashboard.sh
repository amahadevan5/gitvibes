#!/bin/sh

test_description='gitvibes: dashboard (vibes-dashboard)'

. ./test-lib.sh

test_expect_success 'setup repo for dashboard tests' '
	git init vibes-dashboard-test &&
	cd vibes-dashboard-test &&
	git vibes-init &&
	echo "init" >file.txt &&
	git add . &&
	git commit -m "initial commit"
'

test_expect_success 'vibes-dashboard --web starts server' '
	{
		git vibes-dashboard --web --port 3798 &
	} &&
	test_when_finished "kill $! 2>/dev/null || :" &&
	sleep 1 &&
	curl -s http://localhost:3798/ >dash.out &&
	grep -q "gitvibes" dash.out
'

test_expect_success 'vibes-dashboard -h shows usage' '
	test_expect_code 129 git vibes-dashboard -h 2>&1
'

test_done
