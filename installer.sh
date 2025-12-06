g++ -o focusd focusd.c
g++ -o focusctl focusctl.c

cp focusctl focusd /usr/local/bin/

chmod +x /usr/local/bin/focusctl
chmod +x /usr/local/bin/focusd
