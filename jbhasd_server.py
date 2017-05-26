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

def discover_devices():
    # loop forever 
    while (1):
        print("\n\nService Discovery.. (%d seconds)" % (zeroconf_delay_secs))
        zeroconf = Zeroconf()
        listener = MyZeroConfListener()  
        browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  

        # loop interval
        time.sleep(zeroconf_delay_secs)
        zeroconf.close()


# Init dict of discovered device URLs
jbhasd_url_dict = {}

http_timeout_secs = 2

last_sunset_check = -1
last_device_poll = -1
sunset_epoch = 0

# Lights on N seconds after/before sunset
sunset_lights_on_offset = -1200

# Lights off at given HHMM time
# Time here is a 4 digit decimal number
# Good for numerical comparisons
lights_off_time = int("0200")

# open for append
sensor_file = open("sensors.csv", "a")

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

    # recalc every 12 hours or at midnight
    if ((sample_min_epoch - last_sunset_check) >= 12*60*60 or
        (current_time == 0)):
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
            local_time = time.localtime(sunset_ts + sunset_lights_on_offset)
            lights_on_time = int(time.strftime("%H%M", local_time))
            print("Lights on at: (%04d)" % (lights_on_time))

        last_sunset_check = sample_min_epoch

    # Device poll controls
    # Looking to poll each discovered device
    # once every minute
    # We need to measure 60+ seconds gap between the current epoch 
    # sample minute and last. This will keep us from slipping
    time_since_last_poll = sample_min_epoch - last_device_poll
    if time_since_last_poll < 60:
        print("Waiting to pass next minute interval.. sleeping for 5")
        time.sleep(5)
        continue

    last_device_poll = sample_min_epoch

    # Calculate desired state
    desired_state = 0
    if (lights_on_time <= lights_off_time):
        if (current_time >= lights_on_time and 
            current_time < lights_off_time):
            desired_state = 1
    else:
        if (current_time > lights_on_time):
            desired_state = 1
        else:
            if (current_time < lights_on_time and
                current_time < lights_off_time):
                desired_state = 1

    print("Current Time: (%04d)" % (current_time))
    print("Lights on at: (%04d)" % (lights_on_time))
    print("Lights off at hour: %04d" % (lights_off_time))
    print("Desired State for Lights: %d" % (desired_state))
    print("Sample Time: %s" % (time.ctime(sample_min_epoch)))

    print ("Discovered Devices:(%d)" % (len(jbhasd_url_dict)))

    # Iterate list of keys in dict
    # take as a snapshot due to threaded nature
    # .keys() iterator not suitable here
    device_url_list = list(jbhasd_url_dict)
    for key in device_url_list:
        print("\nHostname:%s URL:%s" % (key, jbhasd_url_dict[key]))

        response = None
        try:
            response = urllib.request.urlopen(jbhasd_url_dict[key], 
                                              timeout = http_timeout_secs)
        except:
            print("Error in urlopen (status check)")

        if (response is not None):
            response_str = response.read()
            print("Raw JSON data..\n%s" % (response_str))
            json_data = json.loads(response_str.decode('utf-8'))
            device_name = json_data['name']
            zone_name = json_data['zone']
            print("Name:%s Zone:%s" % (device_name, 
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
                    if (desired_state != control_state):
                        print("  ==========> Changing state from %d to %d" % (control_state, desired_state))
                        data = urllib.parse.urlencode({'control' : control_name, 'state'  : desired_state})
                        post_data = data.encode('utf-8')
                        req = urllib.request.Request(jbhasd_url_dict[key], post_data)
                        try:
                            response = urllib.request.urlopen(req,
                                                              timeout = http_timeout_secs)
                        except:
                            print("Error in urlopen (switch state change)")

            for sensor in json_data['sensors']:
                sensor_name = sensor['name']
                sensor_type = sensor['type']
                if sensor_type == "temp/humidity":
                    temp = sensor['temp']
                    humidity = sensor['humidity']
                    print("Sensor.. %d,%s,%s,%s,%s,%s" % (sample_min_epoch, 
                                                          zone_name, 
                                                          device_name, 
                                                          sensor_name, 
                                                          temp, 
                                                          humidity))
                    sensor_file.write("%d,%s,%s,%s,%s,%s\n" % (sample_min_epoch, 
                                                               zone_name, 
                                                               device_name, 
                                                               sensor_name, 
                                                               temp, 
                                                               humidity))
                    sensor_file.flush()
