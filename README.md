Raw TP-Link (Kasa) command sender/receiver

Each invocation sends a single command and receives a single packet in response.

This program is a minimal substitute for the tplink-lightbulb Node package.
Because it's C, it's about 40x faster than the Node package.  On a Raspberry
Pi, that matters.

Building: 
  `make`

Usage: 
  `./kasa <ip-address> <json-blob>`

There's a good list of JSON blobs to try here: 
  https://github.com/softScheck/tplink-smartplug/blob/master/tplink-smarthome-commands.txt 
Especially:
  - get bulb info: `./kasa <ip> '{"system":{"get_sysinfo":null}}'`
  - turn bulb on:  `./kasa <ip> '{"system":{"set_relay_state":{"state":1}}}'`
  - turn bulb off: `./kasa <ip> '{"system":{"set_relay_state":{"state":0}}}'`

Written by Patrick Reynolds <dukepiki@gmail.com>

Released into the public domain, or Creative Commons CC0, your choice.
