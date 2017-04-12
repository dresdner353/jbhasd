# Cormac Long April 2017
#
# Simple basic python3 script
# to detect specified on-network JBHASD devices
# by zone and switch name and turn them on
# 30 mins after sunset and off at 2AM
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
from zeroconf import ServiceBrowser, Zeroconf

# set with co-ords of Dublin Spire, Ireland
sunset_url = 'http://api.sunrise-sunset.org/json?lat=53.349809&lng=-6.2624431&formatted=0'

# Response from web api
# {"results":{"sunrise":"2017-04-11T05:33:38+00:00",
# "sunset":"2017-04-11T19:20:13+00:00",
# "solar_noon":"2017-04-11T12:26:55+00:00","day_length":49595,
# "civil_twilight_begin":"2017-04-11T04:56:58+00:00",
# "civil_twilight_end":"2017-04-11T19:56:52+00:00",
# "nautical_twilight_begin":"2017-04-11T04:11:20+00:00",
# "nautical_twilight_end":"2017-04-11T20:42:31+00:00",
# "astronomical_twilight_begin":"2017-04-11T03:19:41+00:00",
# "astronomical_twilight_end":"2017-04-11T21:34:10+00:00"},"status":"OK"}

def sunset_api_time_to_epoch(time_str):
    # decode UTC time from string, strip last 6 chars first
    ts_datetime = datetime.datetime.strptime(time_str[:-6], 
                                             '%Y-%m-%dT%H:%M:%S')
    # Epoch extraction
    epoch_time = time.mktime(ts_datetime.timetuple())

    return epoch_time

# Zone Switchname
switch_tlist = [
        ("Livingroom", "Uplighter"),
        ("Playroom", "Uplighter"),
        ]

def check_switch(zone_name, switch_name):
    # returns 1 for matches in switch_tlist
    for zone, switch in switch_tlist:
        if zone == zone_name and switch == switch_name:
                return 1
    return 0

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


http_timeout_secs = 2
zeroconf_delay_secs = 60

last_sunset_check = -1
sunset_epoch = 0

# Lights on 1 hour after sunset
sunset_lights_on_offset = 1800

# Lights off at given AM hour
lights_off_hour = 2

while (1):

    # Sunset check
    now = time.time()
    current_hour = datetime.datetime.today().hour
    if (now - last_sunset_check >= 24*60*60):
        # Re-calculate
        print("Getting Sunset times..")
        response = None
        try:
            response = urllib.request.urlopen(sunset_url,
                                              timeout = http_timeout_secs)
        except:
            print("Error in urlopen (sunrise/sunset check)")

        if response is not None:
            response_str = response.read()
            json_data = json.loads(response_str.decode('utf-8'))
            sunset_str = json_data['results']['sunset']
            sunset_ts = sunset_api_time_to_epoch(sunset_str)
            print("Sunset: %s (%d)" % (time.ctime(sunset_ts),
                                       sunset_ts))

        last_sunset_check = now


    print("\n\nService Discovery.. (%d seconds)" % (zeroconf_delay_secs))
    jbhasd_url_dict = {}
    zeroconf = Zeroconf()
    listener = MyZeroConfListener()  
    browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  

    # loop interval
    time.sleep(zeroconf_delay_secs)
    zeroconf.close()

    print("Lights on at: %s (%d)" % (time.ctime(sunset_ts + sunset_lights_on_offset),
                                     sunset_ts + sunset_lights_on_offset))
    print("Lights off at hour: %d" % (lights_off_hour))
    
    print ("Service List:")
    for key in jbhasd_url_dict.keys():
        print("\nName:%s URL:%s" % (key, jbhasd_url_dict[key]))

        response = None
        try:
            response = urllib.request.urlopen(jbhasd_url_dict[key], 
                                              timeout = http_timeout_secs)
        except:
            print("Error in urlopen (status check)")

        if (response is not None):
            response_str = response.read()
            json_data = json.loads(response_str.decode('utf-8'))
            device_name = json_data['name']
            zone_name = json_data['zone']
            print("JSON.. Name:%s Zone:%s" % (device_name, 
                                              zone_name))
            for control in json_data['controls']:
                control_name = control['name']
                control_type = control['type']
                control_state = int(control['state'])
                print("  Control Name:%s Type:%s State:%s" % (control_name,
                                                              control_type,
                                                              control_state))
    
                if (check_switch(zone_name, control_name)):
                    print("  Marked for Sunrise/Sunset automation")

                    # Determine desired state
                    # If the hour is the lights off hour then 
                    # its off
                    # else if the curent time has passed sunset plus
                    # offset then we're on
                    desired_state = 0
                    if (current_hour == lights_off_hour):
                        desired_state = 0
                        print("  Reached lights off hour")
                    else:
                        if (now >= (sunset_ts + sunset_lights_on_offset)):
                            print("  Time is past sunset offset")
                            desired_state = 1

                    if (desired_state != control_state):
                        print("==========> Changing state from %d to %d" % (control_state, desired_state))
                        data = urllib.parse.urlencode({'control' : control_name, 'state'  : desired_state})
                        post_data = data.encode('utf-8')
                        req = urllib.request.Request(jbhasd_url_dict[key], post_data)
                        try:
                            response = urllib.request.urlopen(req,
                                                              timeout = http_timeout_secs)
                        except:
                            print("Error in urlopen (switch state change)")
