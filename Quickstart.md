Here is a quick start guide to getting the webserver and device simulator running and allow you explore the principle behind the discovery and capability exchange.

You can run these scripts on a Linux environment or even a Raspberry Pi. You may also be able to get it working on OSX and Windows with the related installs of python etc. 

Prerequisites installed components:

OS package for Python3
gcc (may be used by pip3 when installing packages)

Python packages:
python-dateutil
zeroconf
cherrypy

Then to run the web server:
cd <your work dir>
git clone https://github.com/dresdner353/jbhasd.git
python3 jbhasd/jbhasd_web_server.py

.. if it works, point your browser as localhost:8080 or your machines IP:8080. If its working, you should see a page with a gray gradient background.

Then start the simulator:
python3 jbhasd/jbhasd_device_sim.py

You can run the simulator on the same machine as the webserver or on a separate machine.

This will start registering a fake device per US state with randomly added switches and sensors to each device. It will then advertise the fake devices on MDNS and DNS-SD. Your webserver page should refresh with widget panels displayed for each discovered device. Each device uses a webserver on port >= 9000 .. the ports are incremeneted as each new device is created. 

The console of each run script provides logging detail that should help understand what is then happening. 

The basic synopsis:

The web server script is split into several separate threads that each perform a given function. 

Discovery:
The script uses zeroconf to discover the devices by their common "JBHASD" type attribute. It then establishes the URL by compining IP, advertised port and /json to form http://ip:port/json. This URL is added a global set of discovered URLs

Probe:
Every 10 seconds, the script iterates the set of discovered URLs and attempts to fetch that URL and capture the JSON status of the device. This is the capability discovery at work. That same probe behaviour gives the devices an IP and port to use for push requests in the opposite direction (see below).

Manage:
The web server used by the script (cherrypy) then iterates the dictionary of discovered devices and renders a web page of CSS widge panels, one per device with toggle switches for each switch and also temp/humidity details on the device sensors.

If you click on a switch to toggle, a background GET passes the device name, switch name and desired state to the webserver which looks up the device URL and then issues a GET request to the actual device performing the desired on/off action. It captures the JSON response and updates its register of JSON details. Finally it rebuilds the web page with updated state details and returns it to the browser which uses its jquery/Javascript to seamlessly update the dashboard. So you should see switches toggle on/off as clicked and what is really happening is that your dashboard is being redrawn from the updated JSON status that is captured after the given switch change is applied.

If devices change their own state by means of physical button pushes, they will issue push messages to the webserver. Thats achieved by the probe stage where the webserver gave the device an IP and port to use for push notifications. When the device issues the push, it merely gives its device name and the webserver then looks that up in the register and performs an immediate fetch of the status, updating the internal register. This will be reworked a bit more eventually to use long polling on the client web side and have instant dahboard updates based on these push requests.
