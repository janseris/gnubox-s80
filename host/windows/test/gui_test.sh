#!/bin/bash
L=/tmp/claude-0/-home-claude/7367cbd2-30c3-523e-9a04-ebfb8c861652/scratchpad/ws; mkdir -p $L/appdata
for p in $(pgrep -f "python3.* .*(gnubox_winshare|gnubox_share|phone_sim|webserver)\.pyw?"); do kill $p; done; pkill Xvfb; sleep 0.5
ip link del tun9 2>/dev/null
cd "$(dirname "$0")"
Xvfb :99 -screen 0 1024x700x24 >/dev/null 2>&1 &
sleep 1
export DISPLAY=:99
nohup python3 webserver.py 8080 > $L/web.log 2>&1 &
APPDATA=$L/appdata GNUBOX_TEST_TCP=127.0.0.1:7777 GNUBOX_TEST_REWRITE=10.99.0.50=127.0.0.1 nohup python3.12 ../gnubox_share.pyw > $L/gui.log 2>&1 &
sleep 3
import -window root $L/gui1.png
WID=$(xdotool search --name "GnuBox Share" | head -1); echo "window $WID"
# press "Start sharing" (keyboard: focus the button via its position)
eval $(xdotool getwindowgeometry --shell $WID)
xdotool mousemove 633 101 click 1
sleep 2
import -window root $L/gui2.png
nohup python3 phone_sim.py --rate 40000 > $L/phone.log 2>&1 &
sleep 4
curl -s --max-time 60 -o $L/g.bin http://10.99.0.50:8080/onemb.bin &
sleep 9
import -window root $L/gui3.png
while pgrep -f "curl -s --max-time 60" >/dev/null; do sleep 1; done
cmp $L/g.bin www/onemb.bin && echo "download through the GUI-run server: identical"
xdotool mousemove 633 101 click 1
sleep 2
import -window root $L/gui4.png
cat $L/gui.log | tail -5
echo "--- done"
