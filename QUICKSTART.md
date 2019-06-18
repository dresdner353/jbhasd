# Quickstart Guide
Here is a quick start guide to getting the webserver and device simulator running and allow you 
explore the principle behind the discovery and capability exchange.

You can run these scripts on a Linux environment or even a Raspberry Pi. You may also be able 
to get it working on OSX and Windows with the related installs of python etc. 

Prerequisite installed components:

OS package for Python3
gcc (may be used by pip3 when installing packages)

Python3 packages:
python-dateutil
zeroconf
cherrypy

Then to run the web server:
```
cd <your work dir>
git clone https://github.com/dresdner353/jbhasd.git
python3 jbhasd/jbhasd_web_server.py
```

.. if it works, point your browser as localhost:8080 or your machines IP:8080. If its working, 
you should see a page with a gray gradient background with a timestamp top-right. The first run of this script will also write a defautl config file to ~/.jbhasd_web_server

Then start the simulator on a separate terminal:
python3 jbhasd/jbhasd_device_sim.py

You can run the simulator on the same machine as the webserver or on a separate machine on the same network. This will start registering a fake device per US state with randomly added switches and sensors to each device. 
It will then advertise the fake devices on MDNS and DNS-SD. The webserver script will detect these simulated devices via zeroconf and start probing them. The web console page should refresh with widget panels being added for each discovered device. Each device uses a webserver on port >= 9000 .. the ports are incremeneted as each new device is created. 

The console of each running script provides logging detail that should help understand 
what is then happening. 

How it Works:

The web server script is split into several separate threads that each perform a given function. 

Config:
Watches file ~/..jbhasd_web_server for changes and loads them into memory.
On the first run, if this file does not exist, a default is created. 

Discovery:
The script uses zeroconf to discover the devices by their common "JBHASD" type attribute. 
It then establishes the URL by combining IP, advertised port to form http://ip:port. 
This URL is added to a global set of discovered URLs.

Probe:
Every 10 seconds, the script iterates the set of discovered URLs and attempts to fetch that URL 
and capture the JSON status of the device. This is the capability discovery at work. That same probe 
behaviour gives the devices an IP and port to use for push requests in the opposite direction (see below).
If device contact is lost >= 30 seconds, the URL is removed from the set of discovered URLs.

Webserver:
When the main console page is accessed (IP:8080) the script iterates the dictionary of discovered 
devices and generates a list of zones. For each zone, it then rescans the discovered devices
and organises all controls and sensors into a set per zone. The end result is that we 
render a widget on the web page per zone. Each zone widget shows all switches and sensors for that 
zone. 

So this is our dashboard. The approach to organising controls/sensors 
per zone is probably the most logical way to do this as we deploy with zone names like 
Livingroom, Kitchen and then place as many devices as required in a given zone. The control 
and sensor naming can then be as specific as required to put sense on the whole thing once
rendered as a single widget per zone.

The dashboard is all generated code, CSS-based and templated. There is also jquery and 
Javascript code being generated to make the click and refresh magic do its thing.

If you click on a dashboard switch to toggle state, javascript code reacts to an onclick() 
event and issues a background GET passing the device name, switch name and desired state 
to the webserver. When the webserver detects the presence of these paramaters, it looks up 
the device to get its URL and then issues a GET request to the actual device performing the 
desired on/off action. It captures the JSON response from the device and updates its register 
of JSON details. 

Finally it does the normal refresh behaviour and rebuilds the dashboard web page with updated 
state details and returns it to the browser. 

The browser has called this background GET using a javascript call. So now it uses its 
jquery/Javascript to seamlessly update the dashboard. So you should see switches toggle on/off 
as clicked and what is really happening is that your dashboard is being redrawn from the 
updated JSON status that is captured after the given switch change is applied.

The rendered web page also uses a timed background refresh that refreshes the page seamlessly
every 10 seconds. It uses the same jquery javascript approach to this so content just 
updates itself with no old-style page reload.

