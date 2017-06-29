# Cormac Long April 2017
#
# Simple basic python3 script
# to detect specified on-network JBHASD devices
# by zone and switch name and turn them on
# 10 mins after sunset and off at 2AM
# Its more a proof of concept for now
# until I build something more substantial
# The script loops every 60secs, detecting all devices
# and for those in the switch_tlist, it makes sure they
# are set on or off as required 


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

zeroconf_delay_secs = 60
probe_delay_secs = 30
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
        time.sleep(zeroconf_delay_secs)
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
        
        time.sleep(probe_delay_secs)


def build_web_page():

    web_page_str = '<table border="0" padding="5">'

    for device_name in jbhasd_device_status_dict:
        json_resp_str = jbhasd_device_status_dict[device_name]
        json_data = json.loads(json_resp_str.decode('utf-8'))
        zone_name = json_data['zone']
        
        for control in json_data['controls']:
            control_name = control['name']
            control_type = control['type']
            control_state = int(control['state'])

            web_page_str += '<tr>'
            web_page_str += '<td>%s</td><td>%s</td>' % (zone_name,
                                                        control_name)
            web_page_str += '<td><a href="/?device=%s&control=%s&state=1"><button>ON</button></a></td>' % (device_name,
                                                                                                           control_name)
            web_page_str += '<td><a href="/?device=%s&control=%s&state=0"><button>OFF</button></a></td>' % (device_name,
                                                                                                            control_name)
            web_page_str += '</tr>'

    web_page_str += '</table>'

    return web_page_str

def process_get_params(path):
    if (len(path) > 2):
        # skip /? from path before parsing
        args_dict = urllib.parse.parse_qs(path[2:])
        print(args_dict)
        if 'device' in args_dict:
            # get args. but first instances only
            # as parse_qs gives us a dict of lists
            device = args_dict['device'][0]
            control = args_dict['control'][0]
            state = args_dict['state'][0]
            print(jbhasd_url_dict)
            url = jbhasd_device_url_dict[device]

            command_url = '%s?control=%s&state=%s' % (url,
                                                      control,
                                                      state)
            print("Formatted command url:%s" % (command_url))
            
            try:
                response = urllib.request.urlopen(command_url, 
                                                  timeout = http_timeout_secs)
            except:
                print("\nError in urlopen (command).. Name:%s URL:%s" % (key, 
                                                                         command_url))
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
