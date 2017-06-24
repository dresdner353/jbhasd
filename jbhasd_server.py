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

zeroconf_delay_secs = 60

# Init dict of discovered device URLs
jbhasd_url_dict = {}

# dict to count failed URL requests
failed_url_dict = {}

http_timeout_secs = 5

last_sunset_check = -1
last_device_poll = -1
sunset_epoch = 0

# Lights on N seconds after/before sunset
sunset_lights_on_offset = -3600

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

# Zone Switchname
switch_tlist = [
#        Zone           Switch         On          Off       Override
        ("Livingroom",  "Uplighter",   "sunset",   "0200",   "1200" ),
        ("Playroom",    "Uplighter",   "sunset",   "0200",   "1200" ),

        ("Attic",       "A",           "1200",     "1205",   "2359" ),
        ("Attic",       "A",           "1230",     "1232",   "1200" ),
        ("Attic",       "A",           "1330",     "1400",   "2359" ),
        ("Attic",       "A",           "1500",     "1501",   "2359" ),

        ("S20T1",       "Socket",      "1200",     "1202",   "2359" ),
        ("S20T1",       "Green LED",   "1200",     "1210",   "2359" ),

        ("S20T2",       "Socket",      "1120",     "1150",   "2359" ),
        ("S20T2",       "Green LED",   "1120",     "1150",   "2359" ),

        ("S20T3",       "Socket",      "1500",     "1600",   "1200" ),
        ("S20T3",       "Green LED",   "1505",     "1510",   "1200" ),

        ("S20T4",       "Socket",      "sunset",   "0200",   "1600" ),
        ("S20T4",       "Green LED",   "sunset",   "0200",   "1600" ),
        ]

def check_switch(zone_name, 
                 switch_name, 
                 current_time, 
                 control_state):

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
        del jbhasd_url_dict[name]

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        if info:
            address = socket.inet_ntoa(info.address)
            port = info.port
            url_str = "http://%s:%d/json" % (address, port)
            jbhasd_url_dict[name] = url_str

def discover_devices():
    # loop forever 
    while (1):
        zeroconf = Zeroconf()
        listener = MyZeroConfListener()  
        browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  

        # loop interval
        time.sleep(zeroconf_delay_secs)
        zeroconf.close()




# open for append
analytics_file = open("analytics.csv", "a")

# device discovery thread
t = threading.Thread(target = discover_devices)
t.daemon = True
t.start()

# initial grace before main loop
time.sleep(5)

while (1):

    # Calculate the epoch for the most recent
    # minute. That is our sample time and measurement
    # for elapsed time between sample intervals
    now = time.time()
    sample_min_epoch = int(now / 60) * 60
    current_time = int(time.strftime("%H%M", time.localtime()))

    # recalc every 6 hours
    if ((sample_min_epoch - last_sunset_check) >= 6*60*60):
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
            sunset_local_time = time.localtime(sunset_ts + sunset_lights_on_offset)
            sunset_on_time = int(time.strftime("%H%M", sunset_local_time))

        last_sunset_check = sample_min_epoch

    # Device poll controls
    # Looking to poll each discovered device
    # once every minute
    # We need to measure 60+ seconds gap between the current epoch 
    # sample minute and last. This will keep us from slipping
    time_since_last_poll = sample_min_epoch - last_device_poll
    if time_since_last_poll < 60:
        print(".", end = '', flush = True)
        time.sleep(5)
        continue

    os.system('clear')
    last_device_poll = sample_min_epoch

    print("Current Time: (%04d)" % (current_time))
    print("Sample Time: %s" % (time.ctime(sample_min_epoch)))

    print ("Discovered Devices:(%d)" % (len(jbhasd_url_dict)))

    # Iterate list of keys in dict
    # take as a snapshot due to threaded nature
    # .keys() iterator not suitable here
    device_url_list = list(jbhasd_url_dict)
    for key in device_url_list:
        response = None
        url = jbhasd_url_dict[key]
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
            response_str = response.read()
            #print("Raw JSON data..\n%s" % (response_str))
            json_data = json.loads(response_str.decode('utf-8'))
            device_name = json_data['name']
            zone_name = json_data['zone']
            print("\nName:%s Zone:%s URL:%s" % (device_name, 
                                              zone_name,
                                              url))
            for control in json_data['controls']:
                control_name = control['name']
                control_type = control['type']
                control_state = int(control['state'])
                desired_state = check_switch(zone_name, 
                                             control_name,
                                             current_time,
                                             control_state)
                print("  Control Name:%s Type:%s Current State:%s Desired State:%s" % (control_name,
                                                                                       control_type,
                                                                                       control_state,
                                                                                       desired_state))
                csv_row = "%d,%d,%s,%s,%s,%s,%s,%d" % (2,
                                                       sample_min_epoch, 
                                                       zone_name, 
                                                       device_name, 
                                                       control_name, 
                                                       "", 
                                                       "",
                                                       control_state)
                analytics_file.write("%s\n" % (csv_row)) 
                analytics_file.flush()
    
                if (desired_state != -1 and control_state != desired_state):
                    print("  ==> Changing state from %d to %d" % (control_state, desired_state))
                    data = urllib.parse.urlencode({'control' : control_name, 'state'  : desired_state})
                    post_data = data.encode('utf-8')
                    req = urllib.request.Request(url, post_data)
                    try:
                        response = urllib.request.urlopen(req,
                                                          timeout = http_timeout_secs)
                    except:
                        print("  Error in urlopen (switch state change)")

            for sensor in json_data['sensors']:
                sensor_name = sensor['name']
                sensor_type = sensor['type']
                if sensor_type == "temp/humidity":
                    temp = sensor['temp']
                    humidity = sensor['humidity']
                    print("  Sensor Name:%s Type:%s Temp:%s Humidity:%s" % (sensor_name,
                                                                            sensor_type,
                                                                            temp,
                                                                            humidity))
                    csv_row = "%d,%d,%s,%s,%s,%s,%s,%d" % (1,
                                                           sample_min_epoch, 
                                                           zone_name, 
                                                           device_name, 
                                                           sensor_name, 
                                                           temp, 
                                                           humidity,
                                                           0)
                    analytics_file.write("%s\n" % (csv_row)) 
                    analytics_file.flush()

    print("\n\n")
