# Cormac Long April 2017
#
# Simple webserver script
# to detect specified on-network JBHASD devices
# and present a crude web I/F for turning 
# them off/on as required
# It also uses a register of automated devices
# and time parameters to use in automating the on and
# off times for the devices.
# Finally analytics from switches and sensors are written to 
# analytics.csv after each probe

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

# Begin CSS ##################
# Used checkbox example from 
# https://www.w3schools.com/howto/tryit.asp?filename=tryhow_css_switch
web_page_css = """

* {font-family: arial}

.switch {
    position: relative;
    display: inline-block;
    width: 60px;
    height: 34px;
}

.switch input {display:none;}

.slider {
    position: absolute;
    cursor: pointer;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background-color: #ccc;
    -webkit-transition: .4s;
    transition: .4s;
}

.slider:before {
    position: absolute;
    content: "";
    height: 26px;
    width: 26px;
    left: 4px;
    bottom: 4px;
    background-color: white;
    -webkit-transition: .4s;
    transition: .4s;
}

input:checked + .slider {
    background-color: #2196F3;
}

input:focus + .slider {
    box-shadow: 0 0 1px #2196F3;
}

input:checked + .slider:before {
    -webkit-transform: translateX(26px);
    -ms-transform: translateX(26px);
    transform: translateX(26px);
}

/* Rounded sliders */
.slider.round {
    border-radius: 34px;
}

.slider.round:before {
    border-radius: 50%;
}

.dash-title {
    font-size: 20px;
    }

.dash-label {
    font: normal 16px/1 Verdana, Geneva, sans-serif;
    font-size: 15px;
    color: rgba(255,255,255,1);
}

.dash-box {
    display: inline;
    -webkit-box-sizing: content-box;
    -moz-box-sizing: content-box;
    box-sizing: content-box;
    float: left;
    margin: 5px 5px 5px;
    padding: 5px 5px 5px;
    border: none;
    -webkit-border-radius: 3px;
    border-radius: 3px;
    font: normal 16px/1 Verdana, Geneva, sans-serif;
    color: rgba(255,255,255,1);
    -o-text-overflow: ellipsis;
    text-overflow: ellipsis;
    background: -webkit-linear-gradient(-45deg, rgba(64,150,238,1) 0, rgba(14,90,255,1) 100%);
    background: -moz-linear-gradient(135deg, rgba(64,150,238,1) 0, rgba(14,90,255,1) 100%);
    background: linear-gradient(135deg, rgba(64,150,238,1) 0, rgba(14,90,255,1) 100%);
    background-position: 50% 50%;
    -webkit-background-origin: padding-box;
    background-origin: padding-box;
    -webkit-background-clip: border-box;
    background-clip: border-box;
    -webkit-background-size: auto auto;
    background-size: auto auto;
    -webkit-box-shadow: 3px 3px 4px 0 rgba(0,0,0,0.4) ;
    box-shadow: 3px 3px 4px 0 rgba(0,0,0,0.4) ;
    -webkit-transition: all 200ms cubic-bezier(0.42, 0, 0.58, 1) 10ms;
    -moz-transition: all 200ms cubic-bezier(0.42, 0, 0.58, 1) 10ms;
    -o-transition: all 200ms cubic-bezier(0.42, 0, 0.58, 1) 10ms;
    transition: all 200ms cubic-bezier(0.42, 0, 0.58, 1) 10ms;
}
"""
# END CSS ##################


# Discovery and probing of devices
zeroconf_refresh_interval = 300
probe_refresh_interval = 10
url_purge_timeout = 30
web_port = 8080

# Sunset config
# set with co-ords of Dublin Spire, Ireland
sunset_url = 'http://api.sunrise-sunset.org/json?lat=53.349809&lng=-6.2624431&formatted=0'
last_sunset_check = -1
sunset_lights_on_offset = -3600
sunset_on_time = "2000" # noddy default

# Init dict of discovered device URLs
# keyed on zeroconf name
jbhasd_zconf_url_set = set()

# dict of probed device json data
# keyed on url
jbhasd_device_status_dict = {}

# timestamp of last stored status
# keyed on url
jbhasd_device_ts_dict = {}

http_timeout_secs = 10

# Zone Switchname
switch_tlist = [
#        Zone           Switch         On          Off       Override
        ("Livingroom",  "Uplighter",   "sunset",   "0200",   "1200" ),
        ("Playroom",    "Uplighter",   "sunset",   "0200",   "1200" ),

        ("Attic",       "Sonoff Switch",           "1200",     "1205",   "2359" ),
        ("Attic",       "Sonoff Switch",           "1230",     "1232",   "1200" ),
        ("Attic",       "Sonoff Switch",           "1330",     "1400",   "2359" ),
        ("Attic",       "Sonoff Switch",           "1500",     "1501",   "2359" ),

        ("Attic",      "Socket S2",      "1120",     "1150",   "2359" ),
        ("Attic",      "Green LED 2",   "1125",     "1145",   "2359" ),

        ("Attic",      "Socket S3",      "1500",     "1600",   "1200" ),
        ("Attic",      "Green LED 3",   "1505",     "1510",   "1200" ),

        ("Attic",      "Socket S4",      "sunset",   "0200",   "1600" ),
        ("Attic",      "Green LED 4",   "sunset",   "0200",   "1600" ),
        ]


def sunset_api_time_to_epoch(time_str):
    # decode UTC time from string, strip last 6 chars first
    ts_datetime = datetime.datetime.strptime(time_str[:-6], 
                                             '%Y-%m-%dT%H:%M:%S')
    # Adjust for UTC source timezone (strp is local based)
    from_zone = tz.tzutc()
    ts_datetime = ts_datetime.replace(tzinfo=from_zone)

    # Epoch extraction
    epoch_time = time.mktime(ts_datetime.timetuple())

    return epoch_time


def check_switch(zone_name, 
                 switch_name, 
                 current_time, 
                 control_state):

    global sunset_on_time

    # represents state of switch
    # -1 do nothing.. not matched
    # 0 off
    # 1 on
    desired_state = -1

    for zone, switch, on_time, off_time, override_time in switch_tlist:
        if zone == zone_name and switch == switch_name:
            # have a match
            # desired state will have a value now
            # so we assume off initially
            # but this only applies to the first 
            # encounter
            # that allows us to straddle several 
            # events on the same switch and leave
            # an overall on state fall-through
            # in fact the logic of the decisions below are
            # all about setting desired_state to 1 and 
            # never to 0 for this very reason
            if (desired_state == -1):
                desired_state = 0

            # sunset keyword replacemenet with
            # dynamic sunset offset time
            if (on_time == "sunset"):
                on_time = sunset_on_time
            else:
                on_time = int(on_time)

            off_time = int(off_time)
            override_time = int(override_time)

            if (on_time <= off_time):
                if (current_time >= on_time and 
                    current_time < off_time):
                    desired_state = 1
            else:
                if (current_time > on_time):
                    desired_state = 1
                else:
                    if (current_time < on_time and
                        current_time < off_time):
                        desired_state = 1

            # override scenario turning on 
            # before the scheduled on time
            # stopping us turning off a switch
            if (control_state == 1 and 
                desired_state == 0 and
                current_time < on_time and
                current_time >= override_time):
                desired_state = 1

    return desired_state


class MyZeroConfListener(object):  
    def remove_service(self, zeroconf, type, name):
        return

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        if info is not None:
            address = socket.inet_ntoa(info.address)
            port = info.port
            url = "http://%s:%d/json" % (address, port)
            if not url in jbhasd_zconf_url_set:
                jbhasd_zconf_url_set.add(url)
                print("%s Discovered device..\n  name:%s \n  URL:%s" % (time.asctime(),
                                                                        name, 
                                                                        url))

        return


def discover_devices():
    # loop forever 
    while (1):
        zeroconf = Zeroconf()
        listener = MyZeroConfListener()  
        browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  

        # loop interval sleep then 
        # close zeroconf object
        time.sleep(zeroconf_refresh_interval)
        zeroconf.close()


def fetch_url(url, url_timeout, parse_json):
    response_str = None

    #print("%s Fetching URL:%s, timeout:%d" % (time.asctime(), url, url_timeout)) 

    response = None
    try:
        response = urllib.request.urlopen(url,
                                          timeout = url_timeout)
    except:
        print("%s Error in urlopen URL:%s" % (time.asctime(), url))
 
    if response is not None:
        response_str = response.read()
        #print("%s Got response:\n%s" % (time.asctime(), response_str))

        # parse and return json dict if requested
        if parse_json:
            try:
                json_data = json.loads(response_str.decode('utf-8'))
            except:
                print("%s Error in JSON parse.. URL:%s Data:%s" % (time.asctime(),
                                                                   url, 
                                                                   response_str))
                return None
            return json_data

    return response_str
     

def probe_devices():
    global last_sunset_check, sunset_lights_on_offset, sunset_on_time

    # loop forever
    while (1):
        # Sunset calculations
        now = time.time()
        if ((now - last_sunset_check) >= 6*60*60): # every 6 hours
            # Re-calculate
            print("%s Getting Sunset times.." % (time.asctime()))
            json_data = fetch_url(sunset_url, 20, 1)
            if json_data is not None:
                sunset_str = json_data['results']['sunset']
                sunset_ts = sunset_api_time_to_epoch(sunset_str)
                sunset_local_time = time.localtime(sunset_ts + sunset_lights_on_offset)
                sunset_on_time = int(time.strftime("%H%M", sunset_local_time))
                print("%s Sunset on-time is %s (with offset of %d seconds)" % (time.asctime(),
                                                                               sunset_on_time,
                                                                               sunset_lights_on_offset))

            last_sunset_check = now

        # iterate set of discovered device URLs as snapshot list
        # avoids issues if the set is updated mid-way
        device_url_list = list(jbhasd_zconf_url_set)
        for url in device_url_list:
            json_data = fetch_url(url, http_timeout_secs, 1)
            if (json_data is not None):
                jbhasd_device_status_dict[url] = json_data
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
                    print("%s Purging URL:%s.. last updated %d seconds ago" % (time.asctime(),
                                                                               url,
                                                                               last_updated))
                    del jbhasd_device_ts_dict[url]
                    del jbhasd_device_status_dict[url]
                    jbhasd_zconf_url_set.remove(url)

        # Check for automated devices
        # safe snapshot of dict keys into list
        url_list = list(jbhasd_device_status_dict)
        # get time in hhmm format
        current_time = int(time.strftime("%H%M", time.localtime()))
        for url in url_list:
            json_data = jbhasd_device_status_dict[url]
            device_name = json_data['name']
            zone_name = json_data['zone']

            # use timestamp from ts dict as sample time
            # for analytics
            status_ts = jbhasd_device_ts_dict[url]

            # Switch status check 
            for control in json_data['controls']:
                control_name = control['name']
                control_type = control['type']
                control_state = int(control['state'])
                desired_state = check_switch(zone_name, 
                                             control_name,
                                             current_time,
                                             control_state)
 
                # Record analytics
                csv_row = "%d,%d,%s,%s,%s,%s,%s,%d" % (2,
                                                       status_ts, 
                                                       zone_name, 
                                                       device_name, 
                                                       control_name, 
                                                       "", 
                                                       "",
                                                       control_state)
                analytics_file.write("%s\n" % (csv_row)) 
                analytics_file.flush()

                # If switch state not in desired state
                # update and recache the status
                if (desired_state != -1 and control_state != desired_state):
                    print("%s Automatically setting zone:%s control:%s"
                          "to state:%s on URL:%s" % (time.asctime(),
                                                     zone_name,
                                                     control_name,
                                                     desired_state,
                                                     url))
                    data = urllib.parse.urlencode({'control' : control_name, 'state'  : desired_state})
                    post_data = data.encode('utf-8')
                    req = urllib.request.Request(url, post_data)
                    json_data = fetch_url(req, http_timeout_secs, 1)
                    if (json_data is not None):
                        jbhasd_device_status_dict[url] = json_data
                        jbhasd_device_ts_dict[url] = int(time.time())

            # Iterate sensors and record analytics
            for sensor in json_data['sensors']:
                sensor_name = sensor['name']
                sensor_type = sensor['type']
                if sensor_type == "temp/humidity":
                    temp = sensor['temp']
                    humidity = sensor['humidity']
                    csv_row = "%d,%d,%s,%s,%s,%s,%s,%d" % (1,
                                                           status_ts, 
                                                           zone_name, 
                                                           device_name, 
                                                           sensor_name, 
                                                           temp, 
                                                           humidity,
                                                           0)
                    analytics_file.write("%s\n" % (csv_row)) 
                    analytics_file.flush()

        # loop sleep interval
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
    # Ideally this needs an Ajax or jQuery/Angular type solution
    # But for now a crude GET and refresh model will do
    web_page_str = ('<head>'
                    '  <title>JBHASD Console</title>'
                    '  <meta http-equiv="refresh" content="%d url=/">'
                    '  <meta id="META" name="viewport" content="width=device-width; initial-scale=1.0" >'
                    '  <style type="text/css">%s</style>'
                    '</head>') % (probe_refresh_interval, web_page_css)

    # safe snapshot of dict keys into list
    url_list = list(jbhasd_device_status_dict)

    # Build a set of zones
    zone_set = set()
    for url in url_list:
        json_data = jbhasd_device_status_dict[url]
        zone_name = json_data['zone']
        zone_set.add(zone_name)

    # Iterate zones
    for zone in zone_set:
        web_page_str += ('<div class="dash-box">'
                        '<p class="dash-title">%s</p>'
                        '<table border="0" padding="3">') % (zone)

        # Controls
        for url in url_list:
            json_data = jbhasd_device_status_dict[url]
            zone_name = json_data['zone']

            if (zone == zone_name):
                # Controls
                for control in json_data['controls']:
                    control_name = control['name']
                    control_type = control['type']
                    control_state = int(control['state'])
                    alternate_state = (control_state + 1) % 2

                    # prep args for transport
                    url_safe_url = urllib.parse.quote_plus(url)
                    url_safe_zone = urllib.parse.quote_plus(zone_name)
                    url_safe_control = urllib.parse.quote_plus(control_name)

                    # href URL for generated html
                    href_url = ('/?url=%s'
                                '&zone=%s'
                                '&control=%s'
                                '&state=%d') % (url_safe_url,
                                                url_safe_zone,
                                                url_safe_control,
                                                alternate_state)

                    if (alternate_state == 1):
                        checked_str = ""
                    else:
                        checked_str = "checked"

                    # format checkbox css slider in table cell
                    # with onclick action of the href url
                    # the checked_str also ensures the checkbox is 
                    # rendered in the current state
                    web_page_str += ('<tr>'
                                     '<td class="dash-label">%s</td>'
                                     '<td align="center">'
                                     '<label class="switch">'
                                     '<input type="checkbox" onclick=\'window.location.assign("%s")\' %s>'
                                     '<div class="slider round"></div>'
                                     '</label>'
                                     '</td>'
                                     '</tr>') % (control_name,
                                                 href_url,
                                                 checked_str)

        web_page_str += '<tr><td></td></tr>'
        web_page_str += '<tr><td></td></tr>'
        web_page_str += '<tr><td></td></tr>'

        for url in url_list:
            json_data = jbhasd_device_status_dict[url]
            zone_name = json_data['zone']

            if (zone == zone_name):
                # Sensors
                for sensor in json_data['sensors']:
                    sensor_name = sensor['name']
                    sensor_type = sensor['type']

                    if (sensor_type == 'temp/humidity'):
                        temp = sensor['temp']
                        humidity = sensor['humidity']

                        web_page_str += ('<tr>'
                                         '<td class="dash-label">%s</td>'
                                         '<td class="dash-label">%sC %s%%</td>'
                                         '</tr>') % (sensor_name,
                                                     temp,
                                                     humidity)
                        web_page_str += '<tr><td></td></tr>'
                        web_page_str += '<tr><td></td></tr>'

        # terminate the zone
        web_page_str += '</table></div>'

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
                zone = args_dict['zone'][0]
                control = args_dict['control'][0]
                state = args_dict['state'][0]
                print("%s Manually setting zone:%s control:%s"
                      "to state:%s on URL:%s" % (time.asctime(),
                                                 zone,
                                                 control,
                                                 state,
                                                 url))

                # Format URL and pass control name through quoting function
                # Will handle any special character formatting for spaces
                # etc
                control_safe = urllib.parse.quote_plus(control)
                command_url = '%s?control=%s&state=%s' % (url,
                                                          control_safe,
                                                          state)

            #print("Formatted command url:%s" % (command_url))
            json_data = fetch_url(command_url, http_timeout_secs, 1)
            if (json_data is not None):
                # update the status and ts as returned
                jbhasd_device_status_dict[url] = json_data
                jbhasd_device_ts_dict[url] = int(time.time())
    return

#This class will handles any incoming request from
#the browser 
class myHandler(BaseHTTPRequestHandler):
        
    def log_message(self, format, *args):
        # over-ridden to supress stderr 
        # logging of activity
        pass

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
    
    try: 
        server.serve_forever()
    except:
        sys.exit()

# main()

# Analytics
analytics_file = open("analytics.csv", "a")

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
