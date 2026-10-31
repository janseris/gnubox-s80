#!/bin/bash
# server alone, then the hang-up test, then the full fake phone again (reconnect after EOF)
L=/tmp/claude-0/-home-claude/7367cbd2-30c3-523e-9a04-ebfb8c861652/scratchpad/ws
for p in $(pgrep -f "python3 .*(gnubox_winshare|phone_sim|webserver)\.py"); do kill $p; done; sleep 0.5
ip link del tun9 2>/dev/null
cd "$(dirname "$0")"
nohup python3 webserver.py 8080 > $L/web.log 2>&1 &
nohup python3 ../gnubox_winshare.py --tcp 127.0.0.1:7777 --rewrite 10.99.0.50=127.0.0.1 > $L/srv.log 2>&1 &
sleep 1
python3 reconnect_test.py
sleep 1
sleep 1
nohup python3 phone_sim.py --rate 40000 > $L/phone.log 2>&1 &
sleep 3
curl -s --max-time 60 -o $L/re.bin -w "after reconnect: %{size_download} B at %{speed_download} B/s\n" http://10.99.0.50:8080/mid.bin
cmp $L/re.bin www/mid.bin && echo "  identical"
echo "--- server log"; cat $L/srv.log
