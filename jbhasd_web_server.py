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

zeroconf_refresh_interval = 60
probe_refresh_interval = 10
url_purge_timeout = 30
web_port = 8080

# Init dict of discovered device URLs
# keyed on zeroconf name
jbhasd_zconf_url_set = set()

# dict of probed device status strings
# keyed on url
jbhasd_device_status_dict = {}

# timestamp of last stored status
# keyed on url
jbhasd_device_ts_dict = {}

http_timeout_secs = 5

last_sunset_check = -1
last_device_poll = -1

class MyZeroConfListener(object):  
    def remove_service(self, zeroconf, type, name):

        return

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        if info:
            address = socket.inet_ntoa(info.address)
            port = info.port
            url_str = "http://%s:%d/json" % (address, port)
            jbhasd_zconf_url_set.add(url_str)

        return


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
    print("Probe started")
    # loop forever
    while (1):
        # iterate set as snapshot list
        # avoids issues if the set is updated mid-way
        device_url_list = list(jbhasd_zconf_url_set)
        for url in device_url_list:
            response = None
            #print("Probing %s" % (url))
            try:
                response = urllib.request.urlopen(url, 
                                                  timeout = http_timeout_secs)
            except:
                print("\nError in urlopen (status check).. "
                      "URL:%s" % (url))

            if (response is not None):
                json_resp_str = response.read()
                jbhasd_device_status_dict[url] = json_resp_str
                jbhasd_device_ts_dict[url] = int(time.time())
        
        # Purge dead URLs
        now = int(time.time())
        # iterate the timestamps from same 
        # snapshot list to avoid issues with parallel 
        # access to the dicts
        for url in device_url_list:
            if url in jbhasd_device_ts_dict:
                last_updated = now - jbhasd_device_ts_dict[url]
                if last_updated >= url_purge_timeout:
                    print("Device URL:%s last updated %d seconds ago.. purging" % (url,
                                                                                   last_updated))
                    del jbhasd_device_ts_dict[url]
                    del jbhasd_device_status_dict[url]
                    jbhasd_zconf_url_set.remove(url)

        time.sleep(probe_refresh_interval)

    return


def build_web_page():

    # webpage header and title
    # CSS thrown in 
    # and a little refresh timer matched 
    # to the same probe timer
    # of importance here is the refresh uses a directed
    # URL of / to ensure that ant GET args present from a button click
    # do not become part of the refresh effectively repeating the ON/OFF
    # click over and over
    web_page_str = ('<head>'
                    '  <title>JBHASD Console</title>'
                    '  <meta http-equiv="refresh" content="%d url=/">'
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

    # safe snapshot pf dict keys into list
    url_list = list(jbhasd_device_status_dict)
    for url in url_list:
        json_resp_str = jbhasd_device_status_dict[url]
        json_data = json.loads(json_resp_str.decode('utf-8'))
        device_name = json_data['name']
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
            # prep args for transport
            url_safe_url = urllib.parse.quote_plus(url)
            url_safe_control = urllib.parse.quote_plus(control_name)

            web_page_str += ('<td><a href="/?url=%s&control=%s'
                             '&state=1"><button>ON</button></a></td>') % (url_safe_url,
                                                                          url_safe_control)

            web_page_str += ('<td><a href="/?url=%s&control=%s'
                             '&state=0"><button>OFF</button></a></td>') % (url_safe_url,
                                                                           url_safe_control)
            web_page_str += '</tr>'

    # white space
    web_page_str += '<tr><td></td></tr>'
    web_page_str += '<tr><td></td></tr>'
    web_page_str += '<tr><td></td></tr>'

    # Sensors
    web_page_str += ('<tr>'
                     '<td><b>Zone</b></td><td><b>Sensor</b></td>'
                     '<td><b>Temp</b></td><td><b>Humidity</b></td>'
                     '</tr>')

    for url in url_list:
        json_resp_str = jbhasd_device_status_dict[url]
        json_data = json.loads(json_resp_str.decode('utf-8'))
        device_name = json_data['name']
        zone_name = json_data['zone']

        for sensor in json_data['sensors']:
            sensor_name = sensor['name']
            sensor_type = sensor['type']

            if (sensor_type == 'temp/humidity' and 
                sensor_name == 'Temp'):
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
        #print(args_dict)
        if 'url' in args_dict:
            # get args. but first instances only
            # as parse_qs gives us a dict of lists
            url = args_dict['url'][0]

            # control + state combination
            # for switch ON/OFF
            if 'control' in args_dict:
                control = args_dict['control'][0]
                state = args_dict['state'][0]

                # Format URL and pass control name through quoting function
                # Will handle any special character formatting for spaces
                # etc
                control_safe = urllib.parse.quote_plus(control)
                command_url = '%s?control=%s&state=%s' % (url,
                                                          control_safe,
                                                          state)
            #print("Formatted command url:%s" % (command_url))
            response = None
            try:
                response = urllib.request.urlopen(command_url, 
                                                  timeout = http_timeout_secs)
            except:
                print("\nError in urlopen (command).. URL:%s" % (command_url))
            if (response is not None):
                # update the status and ts as returned
                json_resp_str = response.read()
                jbhasd_device_status_dict[url] = json_resp_str
                jbhasd_device_ts_dict[url] = int(time.time())
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
