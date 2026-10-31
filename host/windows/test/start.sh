#!/bin/bash
# starts the test setup; usage: start.sh [rate]
L=/tmp/claude-0/-home-claude/7367cbd2-30c3-523e-9a04-ebfb8c861652/scratchpad/ws; mkdir -p $L
for p in $(pgrep -f "python3 .*(gnubox_winshare|phone_sim|webserver)\.py"); do kill $p; done; sleep 0.5
ip link del tun9 2>/dev/null
cd "$(dirname "$0")"
nohup python3 webserver.py 8080 > $L/web.log 2>&1 &
nohup python3 ../gnubox_winshare.py --tcp 127.0.0.1:7777 --rewrite 10.99.0.50=127.0.0.1 -v > $L/srv.log 2>&1 &
sleep 1
nohup python3 phone_sim.py ${1:+--rate $1} > $L/phone.log 2>&1 &
sleep 3
cat $L/phone.log; echo ---; cat $L/srv.log
