# Cormac Long April 2017
#
# Simple basic webserver script
# to detect specified on-network JBHASD devices
# and present a crude web I/F for turning 
# them off/on as required


import time
import socket
import struct
import urllib
import urllib.parse
import urllib.request
import urllib.error
import json
import random
import datetime
import threading
import os
from dateutil import tz
from zeroconf import ServiceBrowser, Zeroconf
from http.server import BaseHTTPRequestHandler,HTTPServer

zeroconf_refresh_interval = 30
probe_refresh_interval = 10
web_port = 8080

# Init dict of discovered device URLs
# keyed on zeroconf name
jbhasd_url_dict = {}

# dict to count failed URL requests
failed_url_dict = {}

# dict of probed device status strings
# keyed on device name (not same as zeroconf name)
jbhasd_device_status_dict = {}
jbhasd_device_url_dict = {}

http_timeout_secs = 5

last_sunset_check = -1
last_device_poll = -1

class MyZeroConfListener(object):  
    def remove_service(self, zeroconf, type, name):
        del jbhasd_url_dict[name]

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        if info:
            address = socket.inet_ntoa(info.address)
            port = info.port
            url_str = "http://%s:%d/json" % (address, port)
            jbhasd_url_dict[name] = url_str


def discover_devices():
    print("Discovery started")
    # loop forever 
    while (1):
        zeroconf = Zeroconf()
        listener = MyZeroConfListener()  
        browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  

        # loop interval
        time.sleep(zeroconf_refresh_interval)
        zeroconf.close()


def probe_devices():
    time.sleep(10)
    print("Probe started")
    # loop forever
    while (1):
        device_url_list = list(jbhasd_url_dict)
        for key in device_url_list:
            response = None
            url = jbhasd_url_dict[key]
            #print("Probing %s (%s)" % (key, url))
            try:
                response = urllib.request.urlopen(url, 
                                                  timeout = http_timeout_secs)
            except:
                if (url in failed_url_dict):
                    failed_url_dict[url] += 1
                else:
                    failed_url_dict[url] = 1

                print("\nError in urlopen (status check).. Name:%s URL:%s Failed Attempts:%d" % (key, 
                                                                                                 url, 
                                                                                                 failed_url_dict[url]))

                if (failed_url_dict[url] >= 5):
                    # take out URL
                    print("Failed access to %s [%s] has reached 5 attempts.. deleting" % (key, url))
                    del jbhasd_url_dict[key]
                    del failed_url_dict[url]

            if (response is not None):
                failed_url_dict[url] = 0 # reset any failed count
                json_resp_str = response.read()
                json_data = json.loads(json_resp_str.decode('utf-8'))
                device_name = json_data['name']
                jbhasd_device_status_dict[device_name] = json_resp_str
                jbhasd_device_url_dict[device_name] = url
                #print("Raw JSON data..\n%s" % (response_str))
        
        time.sleep(probe_refresh_interval)


def build_web_page():

    # webpage header and title
    # CSS thrown in 
    # and a little refresh timer matched 
    # to the same probe timer
    web_page_str = ('<head>'
                    '  <title>JBHASD Console</title>'
                    '  <meta http-equiv="refresh" content="%d">'
                    '  <meta id="META" name="viewport" content="width=device-width; initial-scale=1.0" >'
                    '  <style type="text/css">'
                    '    * {font-family: arial}'
                    '  </style>'
                    '</head>') % (probe_refresh_interval)

    web_page_str += '<table border="0" padding="15">'

    # Controls
    web_page_str += ('<tr>'
                     '<td><b>Zone</b></td>'
                     '<td><b>Switch</b></td>'
                     '<td><b>State</b></td>'
                     '<td></td>'
                     '<td></td>'
                     '</tr>')

    for device_name in jbhasd_device_status_dict:
        json_resp_str = jbhasd_device_status_dict[device_name]
        json_data = json.loads(json_resp_str.decode('utf-8'))
        zone_name = json_data['zone']

        for control in json_data['controls']:
            control_name = control['name']
            control_type = control['type']
            control_state = int(control['state'])

            web_page_str += '<tr>'
            web_page_str += ('<td>%s</td>'
                             '<td>%s</td>'
                             '<td>%s</td>') % (zone_name,
                                               control_name,
                                               control_state)
            web_page_str += ('<td><a href="/?device=%s&control=%s'
                             '&state=1"><button>ON</button></a></td>') % (device_name,
                                                                          control_name)

            web_page_str += ('<td><a href="/?device=%s&control=%s'
                             '&state=0"><button>OFF</button></a></td>') % (device_name,
                                                                           control_name)
            web_page_str += '</tr>'

    # white space
    web_page_str += '<tr><td></td><td></td><td></td><td></td></tr>'
    web_page_str += '<tr><td></td><td></td><td></td><td></td></tr>'
    web_page_str += '<tr><td></td><td></td><td></td><td></td></tr>'

    # Sensors
    web_page_str += ('<tr>'
                     '<td><b>Zone</b></td><td><b>Sensor</b></td>'
                     '<td><b>Temp</b></td><td><b>Humidity</b></td>'
                     '</tr>')

    for device_name in jbhasd_device_status_dict:
        json_resp_str = jbhasd_device_status_dict[device_name]
        json_data = json.loads(json_resp_str.decode('utf-8'))
        zone_name = json_data['zone']

        for sensor in json_data['sensors']:
            sensor_name = sensor['name']
            sensor_type = sensor['type']

            if sensor_type == 'temp/humidity':
                temp = sensor['temp']
                humidity = sensor['humidity']

                web_page_str += '<tr>'
                web_page_str += '<td>%s</td><td>%s</td>' % (zone_name,
                                                            sensor_name)
                web_page_str += '<td>%sC</td><td>%s%%</td>' % (temp,
                                                               humidity)
                web_page_str += '</tr>'
       
    web_page_str += '</table>'

    return web_page_str


def process_get_params(path):
    if (len(path) > 2):
        # skip /? from path before parsing
        args_dict = urllib.parse.parse_qs(path[2:])
        print(args_dict)
        if ('device' in args_dict and
            'control' in args_dict and
            'state' in args_dict):
            # get args. but first instances only
            # as parse_qs gives us a dict of lists
            device = args_dict['device'][0]
            control = args_dict['control'][0]
            state = args_dict['state'][0]

            if not device in jbhasd_device_url_dict:
                print("Can't find device %s in URL dict" % (device))
                return

            url = jbhasd_device_url_dict[device]

            # Format URL and pass control name  through quoting function
            # Will handle any special character formatting for spaces
            # etc
            command_url = '%s?control=%s&state=%s' % (url,
                                                      urllib.parse.quote_plus(control),
                                                      state)


            print("Formatted command url:%s" % (command_url))
            
            try:
                response = urllib.request.urlopen(command_url, 
                                                  timeout = http_timeout_secs)
            except:
                print("\nError in urlopen (command).. Name:%s URL:%s" % (device, 
                                                                         command_url))
            if (response is not None):
                # update the state as returned
                json_resp_str = response.read()
                jbhasd_device_status_dict[device] = json_resp_str
    return

#This class will handles any incoming request from
#the browser 
class myHandler(BaseHTTPRequestHandler):
        
    #Handler for the GET requests
    def do_GET(self):
        process_get_params(self.path)
        self.send_response(200)
        self.send_header('Content-type','text/html')
        self.end_headers()
        # Send the html message
        self.wfile.write(bytes(build_web_page(), 
                               "utf-8"))
        return

def web_server():
    server = HTTPServer(('', web_port), myHandler)
    print("Started httpserver on port %d" % (web_port))
    
    server.serve_forever()

# main()

# device discovery thread
dicover_t = threading.Thread(target = discover_devices)
dicover_t.daemon = True
dicover_t.start()

# device probe thread
probe_t = threading.Thread(target = probe_devices)
probe_t.daemon = True
probe_t.start()

# Web server
web_server_t = threading.Thread(target = web_server)
web_server_t.daemon = True
web_server_t.start()

while (1):
    time.sleep(5)
