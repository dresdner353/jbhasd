#!/usr/bin/python3 

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
import urllib.parse
import urllib.request
import urllib.error
import json
import random
import datetime
import threading
import os
import sys
import copy
from dateutil import tz
from zeroconf import ServiceBrowser, Zeroconf
import requests
import cherrypy

### Begin Web page template
# Templated web page as the header section with CSS and jquery generated
# code
# The body section is a single div with the generated dashboard HTML
web_page_template = """
<head>
    <title>__TITLE__</title>
    <meta id="META" name="viewport" content="width=device-width, initial-scale=1.0" >
    <style type="text/css">__CSS__</style>
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js"></script>
    <script>

    $(document).ready(function(){
        __SWITCH_FUNCTIONS__
    });

    // Window focus awareness
    // for refresh page behaviour
    var window_focus = true;

    $(window).focus(function() {
        window_focus = true;
    }).blur(function() {
        window_focus = false;
    });

    // refresh timer and function
    // We use an interval refresh which will be called
    // every __RELOAD__ msecs and will invoke a reload of 
    // data into the dashboard div if the window is in focus
    // If not, we will skip the reload.
    // We also cancel the timer ahead of calling the reload to 
    // avoid the reload stacking more timers on itself and 
    // causing major issues
    var refresh_timer = setInterval(refreshPage, __RELOAD__);
    
    var window_width;

    function refreshPage() {
        if (window_focus == true) {
            $.get("__REFRESH_URL__?width=" + window.innerWidth, function(data, status){
                clearInterval(refresh_timer);
                $("#dashboard").html(data);
            });
        }
    }

    </script>
</head>
<body>
    <div id="dashboard">__DASHBOARD__</div>
</body>
"""
### End Web page template

# Begin switch on click function
# The switch click template is the jquery code
# used to drive the action taken when we click on a given 
# switch checkbox. The action gets a given URL to perform 
# the desired action and then gets a refresh URL and 
# replaces the dashboard content with the result
# Its a tidier alternative to page refreshes
click_get_reload_template = """
        $("#__ID__").click(function(){
            $.get("__ACTION_URL__", function(){
                $.get("__REFRESH_URL__?width=" + window.innerWidth, function(data, status){
                    // clear refresh timer before reload
                    clearInterval(refresh_timer);
                    $("#dashboard").html(data);
                });
            });
        });
"""
### End switch on click template

### Begin Web page reload template
web_page_reload_template = """
<head>
    <meta id="META" name="viewport" content="width=device-width, initial-scale=1.0" >
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js"></script>
    <script>

    $(document).ready(function(){
        reload_w_width();
    });

    function reload_w_width() {
        $.get("__REFRESH_URL__?width=" + window.innerWidth, function(data, status){
            $("#dashboard").html(data);
        });
    }

    </script>
</head>
<body>
    <div id="dashboard"></div>
</body>
"""
### End Web page template



# Begin CSS ##################
# Used checkbox example from 
# https://www.w3schools.com/howto/tryit.asp?filename=tryhow_css_switch
web_page_css = """

* {font-family: verdana}

body {
    background: -webkit-linear-gradient(-225deg, rgba(165,164,164,1) 0, rgba(53,53,53,1) 100%);
    background: -moz-linear-gradient(315deg, rgba(165,164,164,1) 0, rgba(53,53,53,1) 100%);
    background: linear-gradient(315deg, rgba(165,164,164,1) 0, rgba(53,53,53,1) 100%);
    background-position: 50% 50%;
}

.timestamp {
    font: normal 16px/1 Verdana, Geneva, sans-serif;
    font-size: 15px;
    color: rgba(255,255,255,1);
}

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
    background-color: #40bc34;
}

input:focus + .slider {
    box-shadow: 0 0 1px #40bc34;
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

a {
    color: rgba(255,255,255,1);
    text-decoration: none;
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
    width: __DASHBOX_WIDTH__px;
    -webkit-border-radius: 3px;
    border-radius: 3px;
    font: normal 16px/1 Verdana, Geneva, sans-serif;
    color: rgba(255,255,255,1);
    -o-text-overflow: ellipsis;
    text-overflow: ellipsis;
    background: -webkit-linear-gradient(0deg, rgba(64,150,238,1) 0, rgba(8,51,142,1) 100%);
    background: -moz-linear-gradient(90deg, rgba(64,150,238,1) 0, rgba(8,51,142,1) 100%);
    background: linear-gradient(90deg, rgba(64,150,238,1) 0, rgba(8,51,142,1) 100%);
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

# Config
gv_home_dir = os.path.expanduser('~')
gv_config_file = gv_home_dir + '/.jbhasd_web_server'
gv_json_config = {}

def set_default_config():
    global gv_config_file

    print("%s Setting config defaults" % (time.asctime()))
    json_config = {}
    # discovery
    json_config['discovery'] = {}
    json_config['discovery']['zeroconf_refersh_interval'] = 60
    json_config['discovery']['device_probe_interval'] = 10
    json_config['discovery']['device_purge_timeout'] = 30

    # web
    json_config['web'] = {}
    json_config['web']['port'] = 8080
    json_config['web']['users'] = {}

    # dashboard
    json_config['dashboard'] = {}
    json_config['dashboard']['initial_num_columns'] = 1
    json_config['dashboard']['box_width'] = 210
    json_config['dashboard']['col_division_offset'] = 20

    # sunset
    json_config['sunset'] = {}
    json_config['sunset']['url'] = 'http://api.sunrise-sunset.org/json?lat=53.349809&lng=-6.2624431&formatted=0'
    json_config['sunset']['offset'] = 1800

    # Timed switches
    json_config['switch_timers'] = []

    # Device config
    json_config['device_profiles'] = []
    json_config['devices'] = []

    # Timezone
    json_config['timezone'] = 'Europe/Dublin'

    return json_config


def load_config(config_file):

    print("%s Loading config from %s" % (time.asctime(),
                                         config_file))
    try:
        config_data = open(config_file).read()
        json_config = json.loads(config_data)
    except Exception as ex: 
        print("%s load config failed: %s" % (
            time.asctime(),
            ex))
        json_config = None

    return json_config


def save_config(json_config, config_file):
    print("%s Saving config to %s" % (time.asctime(),
                                      config_file))
    with open(gv_config_file, 'w') as outfile:
        indented_json_str = json.dumps(json_config, 
                                       indent=4, 
                                       sort_keys=True)
        outfile.write(indented_json_str)
        outfile.close()


def manage_config():
    global gv_json_config
    last_check = 0

    # Default config in case it does not exist
    if (not os.path.isfile(gv_config_file)):
        gv_json_config  = set_default_config()
        save_config(gv_json_config, gv_config_file)

    # 5-second check for config changes
    while (1):
        config_last_modified = os.path.getmtime(gv_config_file)

        if config_last_modified > last_check:
            json_config = load_config(gv_config_file)
            if json_config is not None:
                gv_json_config = json_config
                last_check = config_last_modified

        time.sleep(5)


def get_timer_time(timer_time):

    global gv_sunset_time
    global gv_sunrise_time

    if (timer_time == "sunset"):
        time_ival = gv_sunset_time
    elif (timer_time == "sunrise"):
        time_ival = gv_sunrise_time
    else:
        time_ival = int(timer_time.replace(':', ''))

    #print('%s -> %d' % (timer_time, time_ival))
    return time_ival

# Sunset globals
gv_last_sunset_check = -1
gv_actual_sunset_time = "20:00"
gv_actual_sunrise_time = "05:00"
gv_sunset_time = get_timer_time(gv_actual_sunset_time)
gv_sunrise_time = get_timer_time(gv_actual_sunrise_time)

# device global dictionaries

# Init dict of discovered device URLs
# keyed on zeroconf name
gv_jbhasd_zconf_url_set = set()

# dict of urls
#keyed on name
gv_jbhasd_device_url_dict = {}

# dict of probed device json data
# keyed on name
gv_jbhasd_device_status_dict = {}

# timestamp of last stored status
# keyed on name
gv_jbhasd_device_ts_dict = {}

# timeout for all fetch calls
gv_http_timeout_secs = 5

# Dict to track manual switch scenarios
gv_manual_switch_dict = {}


def reset_all_dicts():
    # wipe all dicts for tracked devices, states etc

    global gv_jbhasd_device_url_dict
    global gv_jbhasd_device_status_dict
    global gv_manual_switch_dict
    global gv_jbhasd_zconf_url_set

    print("%s Resetting all dictionaries" % (time.asctime()))

    gv_jbhasd_device_url_dict = {}
    gv_jbhasd_device_status_dict = {}
    gv_manual_switch_dict = {}
    gv_jbhasd_zconf_url_set = set()


def get_ip():
    # determine my default LAN IP
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # doesn't even have to be reachable
        s.connect(('10.255.255.255', 1))
        ip = s.getsockname()[0]
    except:
        ip = '127.0.0.1'
    finally:
        s.close()
    return ip


def sunset_api_time_to_epoch(time_str, local_timezone):
    print(time_str)
    # decode UTC time from string, strip last 6 chars first
    ts_datetime = datetime.datetime.strptime(time_str[:-6], 
                                             '%Y-%m-%dT%H:%M:%S')
    # Adjust for UTC source timezone and local timezone
    # Some odd stuff observed here in that tz.tzlocal() did
    # not seem to react to actual local timezone on raspberry
    # pi. So I used tz.gettz to explicitly get UTC and a parm
    # string for local timezone and throw that into config
    from_zone = tz.gettz('UTC')
    to_zone = tz.gettz(local_timezone)
    ts_datetime = ts_datetime.replace(tzinfo=from_zone)
    ts_datetime = ts_datetime.astimezone(to_zone)

    # Epoch extraction
    epoch_time = int(time.mktime(ts_datetime.timetuple()))

    return epoch_time


def check_switch(zone_name, 
                 control_name, 
                 current_time):

    global gv_sunset_time
    global gv_sunrise_time
    global gv_manual_switch_dict

    desired_state = -1 # implies no desired state
    motion_interval = 0 # no implied motion

    now = time.time()

    for timer in gv_json_config['switch_timers']:
        if timer['zone'] == zone_name and timer['control'] == control_name:
            # Motion interval (optional)
            if 'motion_interval' in timer:
                motion_on_time = get_timer_time(timer['motion_on'])
                motion_off_time = get_timer_time(timer['motion_off'])

                if (motion_on_time <= motion_off_time):
                    if (current_time >= motion_on_time and 
                        current_time < motion_off_time):
                        motion_interval = timer['motion_interval']
                else:
                    if (current_time > motion_on_time):
                        motion_interval = timer['motion_interval']
                    else:
                        if (current_time < motion_on_time and
                            current_time < motion_off_time):
                            motion_interval = timer['motion_interval']

            # On/Off Start assuming its off
            desired_state = 0

            # sunset/sunrise keyword replacemenet with
            # dynamic values
            on_time = get_timer_time(timer['on'])
            off_time = get_timer_time(timer['off'])

            # Test time ranges and break out on 
            # first hit for the on state
            if (on_time <= off_time):
                if (current_time >= on_time and 
                    current_time < off_time):
                    desired_state = 1
                    break
            else:
                if (current_time > on_time):
                    desired_state = 1
                    break
                else:
                    if (current_time < on_time and
                        current_time < off_time):
                        desired_state = 1
                        break

    #print("return zone:%s control:%s state:%d motion:%d" % (zone_name, 
    #                                                        control_name,
    #                                                        desired_state,
    #                                                        motion_interval))
    return desired_state, motion_interval


def check_rgb(zone_name, 
              control_name, 
              current_time, 
              control_program):

    global gv_sunset_time
    global gv_sunrise_time

    desired_program = ""

    for timer in gv_json_config['rgb_timers']:
        #print(timer)
        if timer['zone'] == zone_name and timer['control'] == control_name:

            # we can now assert a default of off
            desired_program = timer['off_program']

            # sunset keyword replacemenet with
            # dynamic sunset offset time
            if (timer['on'] == "sunset"):
                on_time = gv_sunset_time
            else:
                on_time = int(timer['on'].replace(':', ''))

            off_time = int(timer['off'].replace(':', ''))

            if (on_time <= off_time):
                if (current_time >= on_time and 
                    current_time < off_time):
                    desired_program = timer['on_program']
                    break
            else:
                if (current_time > on_time):
                    desired_program = timer['on_program']
                    break
                else:
                    if (current_time < on_time and
                        current_time < off_time):
                        desired_program = timer['on_program']
                        break

    #print("return rgb program for %s/%s is %s" % (
    #    zone_name,
    #    control_name,
    #    desired_program))
    return desired_program


class MyZeroConfListener(object):  
    def remove_service(self, zeroconf, type, name):
        return

    def add_service(self, zeroconf, type, name):
        # extract dns-sd info, build URL
        # and store in set
        info = zeroconf.get_service_info(type, name)
        if info is not None:
            address = socket.inet_ntoa(info.address)
            port = info.port
            url = "http://%s:%d" % (address, port)
            if not url in gv_jbhasd_zconf_url_set:
                gv_jbhasd_zconf_url_set.add(url)
                print("%s Discovered %s (%s)" % (
                    time.asctime(),
                    name,
                    url))

        return


def json_is_the_same(a, b):
    a_str = json.dumps(a, sort_keys=True)
    b_str = json.dumps(b, sort_keys=True)
    rc = 0

    if (a_str == b_str):
        rc = 1

    return rc


def discover_devices():
    # loop forever 
    while (1):
        zeroconf = Zeroconf()
        listener = MyZeroConfListener()  
        browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  

        # loop interval sleep then 
        # close zeroconf object
        time.sleep(gv_json_config['discovery']['zeroconf_refersh_interval'])
        zeroconf.close()


def get_url(url, url_timeout, parse_json):
    # General purpose URL GETer
    # return contents of page and parsed as json
    # if the parse_json arg is 1
    #print("GET %s\n" % (url))

    response_str = None

    response = None
    try:
        response = requests.get(url,
                                timeout = url_timeout)
    except:
        print("%s Error in GET URL:%s" % (time.asctime(), url))

    if response is not None:
        response_str = response.text

        if parse_json:
            try:
                json_data = response.json()
            except:
                print("%s Error in JSON parse.. URL:%s Data:%s" % (time.asctime(),
                                                                   url, 
                                                                   response_str))
                return None
            return json_data

    return response_str
     

def post_url(url, json_data, url_timeout):
    # General purpose URL POSTer
    # for JSON payloads
    # return contents parsed as json

    #print("POST %s \n%s\n" % (url, json_data))

    response = None
    try:
        response = requests.post(url,
                                 json = json_data,
                                 timeout = url_timeout)
    except:
        print("%s Error in POST URL:%s" % (time.asctime(), url))

    if response is not None:
        try:
            json_data = response.json()
        except:
            print("%s Error in JSON parse.. URL:%s Data:%s" % (time.asctime(),
                                                               url, 
                                                               response.text))
            return None

        return json_data

    return response


def format_control_request(
        control_name,
        property,
        value):

    json_req = {}
    json_req['controls'] = []

    control = {}
    control['name'] = control_name

    # Set value and this will auti-type to 
    # string or int
    control[property] = value

    json_req['controls'].append(control)

    return json_req



def check_automated_devices():
    # Check for automated devices
    # safe snapshot of dict keys into list
    device_list = list(gv_jbhasd_device_status_dict)
    # get time in hhmm format
    current_time = int(time.strftime("%H%M", time.localtime()))
    for device_name in device_list:
        json_data = gv_jbhasd_device_status_dict[device_name]
        zone_name = json_data['zone']
        url = gv_jbhasd_device_url_dict[device_name]

        # use timestamp from ts dict as sample time
        # for analytics
        status_ts = gv_jbhasd_device_ts_dict[device_name]

        # Control status check 
        for control in json_data['controls']:
            control_name = control['name']
            control_type = control['type']

            if control_type == 'switch':
                control_state = int(control['state'])

                # optional motion interval
                if 'motion_interval' in control:
                    motion_interval = int(control['motion_interval'])
                else:
                    motion_interval = 0

                desired_state, desired_motion_interval = check_switch(zone_name, 
                                                                      control_name,
                                                                      current_time)
 
                # If switch state not in desired state
                # update and recache the status
                if (desired_state != -1 and control_state != desired_state):
                    print("%s Automatically setting %s/%s to state:%s" % (
                        time.asctime(),
                        zone_name,
                        control_name,
                        desired_state))

                    json_req = format_control_request(control_name, 
                                                      'state',                          
                                                      desired_state)
                    json_data = post_url(url + '/control', 
                                         json_req,
                                         gv_http_timeout_secs)
                    if (json_data is not None):
                        gv_jbhasd_device_status_dict[device_name] = json_data
                        gv_jbhasd_device_ts_dict[device_name] = int(time.time())


                if (motion_interval != desired_motion_interval):
                    print("%s Automatically setting %s/%s motion_interval:%d" % (
                        time.asctime(),
                        zone_name,
                        control_name,
                        desired_motion_interval))
                    json_req = format_control_request(control_name, 
                                                      'motion_interval',                          
                                                      desired_motion_interval)
                    json_data = post_url(url + '/control', 
                                         json_req,
                                         gv_http_timeout_secs)
                    if (json_data is not None):
                        gv_jbhasd_device_status_dict[device_name] = json_data
                        gv_jbhasd_device_ts_dict[device_name] = int(time.time())


            if control_type in ['rgb', 'argb']:
                control_program = control['program']

                desired_program = check_rgb(
                        zone_name, 
                        control_name,
                        current_time,
                        control_program)
 
                # If switch state not in desired state
                # update and recache the status
                if (desired_program != "" and 
                    control_program != desired_program):
                    print("%s Automatically setting %s/%s to program:%s" % (time.asctime(),
                                                                            zone_name,
                                                                            control_name,
                                                                            desired_program))
                    json_req = format_control_request(control_name, 
                                                      'program',                          
                                                      desired_program)
                    json_data = post_url(url + '/control', 
                                         json_req,
                                         gv_http_timeout_secs)
                    if (json_data is not None):
                        gv_jbhasd_device_status_dict[device_name] = json_data
                        gv_jbhasd_device_ts_dict[device_name] = int(time.time())

    return


def configure_device(url, device_name):

    print("%s Configure device %s" % (time.asctime(), device_name))
    if (device_name in gv_json_config['devices']): 
        # matched to stored profile
        print("%s Matched device %s to stored profile.. configuring" % (time.asctime(), device_name))
        # Extract JSON config for device and profile
        # This is based on taking the profile as the baseline
        # and updating as defined by the device dict
        # Pythons deepcopy and update dict calls play a stormer
        # here for us
        profile_name = gv_json_config['devices'][device_name]['profile']
        config_dict = copy.deepcopy(gv_json_config['device_profiles'][profile_name])
        device_specific_dict = copy.deepcopy(gv_json_config['devices'][device_name])

        # Indicate the origin profile
        config_dict['profile'] = profile_name

        # Set mandatory zone, wifi creds
        config_dict['zone'] = device_specific_dict['zone']
        config_dict['wifi_ssid'] = device_specific_dict['wifi_ssid']
        config_dict['wifi_password'] = device_specific_dict['wifi_password']

        # Optional over-rides
        # protect in try as they keys may not exist in 
        # device dict
        try:
            config_dict['telnet_enabled'] = device_specific_dict['telnet_enabled']
            config_dict['ota_enabled'] = device_specific_dict['ota_enabled']
            config_dict['mdns_enabled'] = device_specific_dict['mdns_enabled']
            config_dict['manual_switches_enabled'] = device_specific_dict['manual_switches_enabled']
        except:
            pass

        # Scan through device specific controls
        # and update defaults taken from profile
        # Given this is a list, we can't do any kind 
        # of update as one list will clobber the other.
        # So for each control in the device specific list
        # we determine its custom name and then locate it 
        # in the main config and update the contents
        # That lets the device config over-ride any or all 
        # of the attributes in the original profile.
        # The only special treatment is the custom_name
        # which lets us rename the control to a desired
        # alternative but we have to match on the original profile
        # name to start
        for control in device_specific_dict['controls']:
            control_name = control['name']

            # Custom name is optional but we just 
            # default to existing name if not present
            # also remove that custom_name field
            # from dict as it will not be sent to the device
            if 'custom_name' in control:
                custom_name = control['custom_name']
                del control['custom_name']
            else:
                custom_name = control_name

            # Iterate the device profile controls
            # match on name, update with the device specific
            # values and over-ride name
            for config_control in config_dict['controls']:
                if config_control['name'] == control_name:
                    config_control.update(control)
                    config_control['name'] = custom_name


        # Format Config
        device_config = json.dumps(
                config_dict, 
                indent=4, 
                sort_keys=True)
        print("%s Sending config (%d bytes):\n%s\n" % (
            time.asctime(), 
            len(device_config),
            device_config))

        # POST to /configure function of URL
        requests.post(url + '/configure', 
                      json = config_dict, 
                      timeout = gv_http_timeout_secs)

        # Purge URL from discovered URL set in case its 
        # already present
        # We want to leave it resurface again 
        try:
            del gv_jbhasd_device_url_dict[device_name]
            del gv_jbhasd_device_ts_dict[device_name]
            del gv_jbhasd_device_status_dict[device_name]
            gv_jbhasd_zconf_url_set.remove(url)
        except:
            pass

    else:
        print("%s ERROR %s not found in device config" % (time.asctime(), device_name))

    return


def probe_devices():
    # iterate set of discovered device URLs
    # and probe their status values, storing in a dictionary
    # Also calculate sunset time every 6 hours as part of automated 
    # management of devices
    global gv_last_sunset_check
    global gv_json_config
    global gv_sunset_time
    global gv_sunrise_time
    global gv_actual_sunset_time 
    global gv_actual_sunrise_time 

    # loop forever
    while (1):
        # Sunset calculations
        now = time.time()
        if ((now - gv_last_sunset_check) >= 6*60*60): # every 6 hours
            # Re-calculate
            print("%s Getting Sunset times.." % (time.asctime()))
            json_data = get_url(gv_json_config['sunset']['url'], 20, 1)
            if json_data is not None:
                # Sunset
                sunset_str = json_data['results']['sunset']
                sunset_ts = sunset_api_time_to_epoch(
                        sunset_str,
                        gv_json_config['timezone'])
                sunset_local_time = time.localtime(sunset_ts)
                # Offset for sunset subtracts offset to make the time 
                # earlier
                sunset_offset_local_time = time.localtime(
                        sunset_ts - gv_json_config['sunset']['offset'])
                gv_sunset_time = int(time.strftime("%H%M", sunset_offset_local_time))
                gv_actual_sunset_time = time.strftime("%H:%M", sunset_local_time)

                # Sunrise
                sunrise_str = json_data['results']['sunrise']
                sunrise_ts = sunset_api_time_to_epoch(
                        sunrise_str,
                        gv_json_config['timezone'])
                sunrise_local_time = time.localtime(sunrise_ts)
                # Offset is added to delay the sunrise time
                sunrise_offset_local_time = time.localtime(
                        sunrise_ts + gv_json_config['sunset']['offset'])
                gv_sunrise_time = int(time.strftime("%H%M", sunrise_offset_local_time))
                gv_actual_sunrise_time = time.strftime("%H:%M", sunrise_local_time)

                print("%s Sunset time is %04d (with offset of %d seconds)" % (
                    time.asctime(),
                    gv_sunset_time,
                    gv_json_config['sunset']['offset']))

                print("%s Sunrise time is %04d (with offset of %d seconds)" % (
                    time.asctime(),
                    gv_sunrise_time,
                    gv_json_config['sunset']['offset']))

            gv_last_sunset_check = now

        # iterate set of discovered device URLs as snapshot list
        # avoids issues if the set is updated mid-way
        device_url_list = list(gv_jbhasd_zconf_url_set)
        successful_probes = 0
        failed_probes = 0
        purged_urls = 0
        control_changes = 0
        for url in device_url_list:
            json_data = get_url(url, gv_http_timeout_secs, 1)
            if (json_data is not None):
                device_name = json_data['name']
                if ('configured' in json_data and
                        json_data['configured'] == 0):
                    # Configure device
                    configure_device(url, device_name)
                else:
                    # Record what we found
                    gv_jbhasd_device_url_dict[device_name] = url
                    gv_jbhasd_device_ts_dict[device_name] = int(time.time())
                    successful_probes += 1

                    # check the json we got back against what we have 
                    # stored. But only compare controls for now  
                    # if its a first time store, then its a change
                    if (device_name in gv_jbhasd_device_status_dict):
                        controls_same = json_is_the_same(json_data['controls'], 
                                                         gv_jbhasd_device_status_dict[device_name]['controls'])
                        if (controls_same == 0):
                            control_changes += 1
                    else:
                        control_changes += 1

                    gv_jbhasd_device_status_dict[device_name] = json_data

            else:
                print("%s Failed to get status on %s" % (time.asctime(),
                                                         url))
                failed_probes += 1
        
        # Purge dead devices and URLs
        now = int(time.time())

        # Iterate known URLs and seek out URLs with no 
        # recorded status. These would probaby be duds
        # We do this with a set snapshot from the same list
        # we iterated above . Then we iterate the device url dict
        # and remove all urls from the snapshot set
        # The remaining URLs are then the duds
        device_url_set = set(device_url_list)
        for device_name in gv_jbhasd_device_url_dict:
            url = gv_jbhasd_device_url_dict[device_name]
            if url in device_url_set:
                device_url_set.remove(url)

        for url in device_url_set:
            reason = "never got a valid status response" 
            print("%s Purging URL:%s.. reason:%s" % (time.asctime(),
                                                     url,
                                                     reason))

            if url in gv_jbhasd_zconf_url_set:
                purged_urls += 1
                gv_jbhasd_zconf_url_set.remove(url)

        # iterate the known devices with status values 
        # that were previously recorded. 
        # Check for expired timestamps and purge
        device_name_list = list(gv_jbhasd_device_status_dict)
        for device_name in device_name_list:
            url = gv_jbhasd_device_url_dict[device_name]

            last_updated = now - gv_jbhasd_device_ts_dict[device_name]
            if last_updated >= gv_json_config['discovery']['device_purge_timeout']:
                purged_urls += 1
                reason = "expired last updated %d seconds ago" % (last_updated)
                print("%s Purging Device:%s URL:%s.. reason:%s" % (time.asctime(),
                                                                   device_name,
                                                                   url,
                                                                   reason))
                del gv_jbhasd_device_url_dict[device_name]
                del gv_jbhasd_device_ts_dict[device_name]
                del gv_jbhasd_device_status_dict[device_name]

                if url in gv_jbhasd_zconf_url_set:
                    gv_jbhasd_zconf_url_set.remove(url)

        # Automated devices
        check_automated_devices()

        # Analytics
        device_list = list(gv_jbhasd_device_status_dict)
        # get time in hhmm format
        current_time = int(time.strftime("%H%M", time.localtime()))
        for device_name in device_list:
            json_data = gv_jbhasd_device_status_dict[device_name]
            zone_name = json_data['zone']
            url = gv_jbhasd_device_url_dict[device_name]

            # use timestamp from ts dict as sample time
            # for analytics
            status_ts = gv_jbhasd_device_ts_dict[device_name]

            # system details
            # 'system' section may not be present 
            # for say simulated devices
            if 'system' in json_data:
                device_millis = json_data['system']['millis']
                free_heap = json_data['system']['free_heap']
                csv_row = "%d,%d,%s,%s,%s,%s,%s,%d" % (3,
                                                       status_ts, 
                                                       zone_name, 
                                                       device_name, 
                                                       "", 
                                                       "", 
                                                       "",
                                                       device_millis)
                analytics_file.write("%s\n" % (csv_row)) 
                csv_row = "%d,%d,%s,%s,%s,%s,%s,%d" % (4,
                                                       status_ts, 
                                                       zone_name, 
                                                       device_name, 
                                                       "", 
                                                       "", 
                                                       "",
                                                       free_heap)
                analytics_file.write("%s\n" % (csv_row)) 
                analytics_file.flush()

            # Switch status check 
            for control in json_data['controls']:
                control_name = control['name']
                control_type = control['type']

                if (control_type == 'switch'):
                    control_state = int(control['state'])
 
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


                if (control_type == 'temp/humidity'):
                    temp = control['temp']
                    humidity = control['humidity']
                    csv_row = "%d,%d,%s,%s,%s,%s,%s,%d" % (1,
                                                           status_ts, 
                                                           zone_name, 
                                                           device_name, 
                                                           control_name, 
                                                           temp, 
                                                           humidity,
                                                           0)
                    analytics_file.write("%s\n" % (csv_row)) 
                    analytics_file.flush()

        print("%s Probe.. successful:%d failed:%d "
              "changed:%d purged:%d" % (time.asctime(),
                                        successful_probes,
                                        failed_probes,
                                        control_changes,
                                        purged_urls))

        # loop sleep interval
        time.sleep(gv_json_config['discovery']['device_probe_interval'])
    return


def build_zone_web_page(num_cols):

    # safe snapshot of dict keys into list
    device_list = list(gv_jbhasd_device_status_dict)
    url_dict_copy = copy.deepcopy(gv_jbhasd_device_url_dict)

    # We'll build one string of data
    # for the jquery code defining
    # the clicking and load actions
    # The switch_id number will be incremented
    # as we define switches and matched between the 
    # generated HTML for the switch and jquery
    # code for the click action
    jquery_str = ""

    # For HTML content, its a list of columns
    # initialised to blank strings
    # also init size dict to 0 for each column
    dashboard_col_list = []
    dashboard_col_size_dict = {}
    for i in range(0, num_cols):
        dashboard_col_list.append("")
        dashboard_col_size_dict[i] = 0

    switch_id = 0

    # Track the size of each zone in terms of number
    # of controls and sensors
    # will use this to then to control a balanced 
    # distribution of widgets into vertical columns
    zone_size_dict = {}
    for device_name in device_list:
        json_data = gv_jbhasd_device_status_dict[device_name]
        device_name = json_data['name']
        zone_name = json_data['zone']
        device_size = (len(json_data['controls']) + 1)

        if (zone_name in zone_size_dict):
            zone_size_dict[zone_name] += device_size
        else:
            zone_size_dict[zone_name] = device_size

    # Alphabetic name sort to begin
    sorted_zone_list = sorted(zone_size_dict.keys())

    for zone in sorted_zone_list:
        # determine col_index
        # based on smallest accumulated size of 
        # existing columns
        col_index = 0
        smallest_col_size = 0
        for i in range (0, num_cols):
            # find smallest column
            # defaulting with first
            if i == 0 or dashboard_col_size_dict[i] < smallest_col_size:
                col_index = i
                smallest_col_size = dashboard_col_size_dict[i]

        # add zize of selected zone to tracked size per column
        dashboard_col_size_dict[col_index] += zone_size_dict[zone]
        
        #print("Putting zone:%s in col:%d" % (zone, col_index))

        # start the dash-box widget
        dashboard_col_list[col_index] += ('<div class="dash-box">'
                                          '<p class="dash-title">%s</p>'
                                          '<table border="0" padding="3" '
                                          'width="100%%">') % (zone)

        # Controls in this zone
        for device_name in device_list:
            json_data = gv_jbhasd_device_status_dict[device_name]
            zone_name = json_data['zone']
            url = url_dict_copy[device_name]

            if (zone == zone_name):
                # Controls
                for control in json_data['controls']:
                    control_name = control['name']
                    control_type = control['type']

                    if control_type == 'switch':
                        control_state = int(control['state'])
                        alternate_state = (control_state + 1) % 2

                        # prep args for transport
                        url_safe_device = urllib.parse.quote_plus(device_name)
                        url_safe_zone = urllib.parse.quote_plus(zone_name)
                        url_safe_control = urllib.parse.quote_plus(control_name)

                        # href URL for generated html
                        # This is a URL to the webserver
                        # carrying the device URL and directives
                        # to change the desired switch state
                        href_url = ('/api?device=%s'
                                    '&zone=%s'
                                    '&control=%s'
                                    '&state=%d') % (url_safe_device,
                                                    url_safe_zone,
                                                    url_safe_control,
                                                    alternate_state)

                        if (alternate_state == 1):
                            checked_str = ""
                        else:
                            checked_str = "checked"

                        # format checkbox css slider in table cell
                        # with id set to the desired switch_id string
                        # the checked_str also ensures the checkbox is 
                        # rendered in the current state
                        dashboard_col_list[col_index] += ('<tr>'
                                                          '<td class="dash-label">%s</td>'
                                                          '<td align="center">'
                                                          '<label class="switch">'
                                                          '<input type="checkbox" '
                                                          'id="switch%d" %s>'
                                                          '<div class="slider round"></div>'
                                                          '</label>'
                                                          '</td>'
                                                          '</tr>') % (control_name,
                                                                      switch_id,
                                                                      checked_str)

                        # Jquery code for the click state
                        # Generated with the same switch id
                        # to match the ckick action to the related
                        # url and checkbox switch
                        switch_str = click_get_reload_template
                        jquery_click_id = 'switch%d' % (switch_id)
                        switch_str = switch_str.replace("__ID__", jquery_click_id)
                        switch_str = switch_str.replace("__ACTION_URL__", href_url)
                        jquery_str += switch_str

                        # increment for next switch         
                        switch_id += 1

        # Spacing between controls and sensors
        dashboard_col_list[col_index] += '<tr><td></td></tr>'
        dashboard_col_list[col_index] += '<tr><td></td></tr>'
        dashboard_col_list[col_index] += '<tr><td></td></tr>'

        # sensors in this zone
        for device_name in device_list:
            json_data = gv_jbhasd_device_status_dict[device_name]
            zone_name = json_data['zone']
            url = url_dict_copy[device_name]

            if (zone == zone_name):
                # Sensors
                for sensor in json_data['controls']:
                    sensor_name = sensor['name']
                    sensor_type = sensor['type']

                    if (sensor_type == 'temp/humidity'):
                        temp = sensor['temp']
                        humidity = sensor['humidity']

                        # &#x1f321 thermometer temp
                        # &#x1f322 droplet humidity
                        dashboard_col_list[col_index] += (
                                '<tr>'
                                '<td class="dash-label" width="50%%">%s</td>'
                                '<td class="dash-label">'
                                '<table border="0" width="100%%">'
                                '<tr><td class="dash-label" align="center">&#x263C;</td>'
                                '<td class="dash-label" align="left">%s C</td></tr>'
                                '<tr><td class="dash-label" align="center">&#x1F4A7;</td>'
                                '<td class="dash-label" align="left">%s %%</td></tr>'
                                '</table></td>'
                                '</tr>') % (sensor_name,
                                            temp,
                                            humidity)

                        dashboard_col_list[col_index] += '<tr><td></td></tr>'
                        dashboard_col_list[col_index] += '<tr><td></td></tr>'

                    if (sensor_type == 'rgb'):
                        current_colour = sensor['current_colour']

                        dashboard_col_list[col_index] += (
                                '<tr>'
                                '<td class="dash-label">%s</td>'
                                '<td style="color:#%s" align="center">'
                                '<span title="%s">'
                                '&#x2B24;&#x2B24;&#x2B24;&#x2B24;&#x2B24;'
                                '</span>'
                                '</td>'
                                '</tr>') % (sensor_name,
                                            current_colour[4:],
                                            current_colour)

                        dashboard_col_list[col_index] += '<tr><td></td></tr>'
                        dashboard_col_list[col_index] += '<tr><td></td></tr>'


        # terminate the zone table and container div
        dashboard_col_list[col_index] += '</table></div>'

    # Build the dashboard portion
    # It's the timestamp and then a single row table, 
    # one cell per vertical column.
    # Table width is set to 50% force it compress
    # more. Otherwise it will tend to go for a 100% fill
    # and the odd column will be given more width.
    # Cells are vertically aligned to top to keep widgets 
    # top-down in layout and not vertically-centred
    dashboard_str = ('<div class="timestamp" align="right">'
                     'Updated %s</div>') % (time.asctime())
    dashboard_str += '<table border="0" width="50%%"><tr>'
    for col_str in dashboard_col_list:
        dashboard_str += '<td valign="top">'
        dashboard_str += col_str
        dashboard_str += '</td>'
    dashboard_str += '</tr></table>'

    # Build and return the web page
    # dropping in CSS, generated jquery code
    # and dashboard.
    web_page_str = web_page_template
    web_page_str = web_page_str.replace("__TITLE__", "JBHASD Zone Console")
    web_page_str = web_page_str.replace("__CSS__", web_page_css)
    web_page_str = web_page_str.replace("__DASHBOX_WIDTH__", str(gv_json_config['dashboard']['box_width']))
    web_page_str = web_page_str.replace("__SWITCH_FUNCTIONS__", jquery_str)
    web_page_str = web_page_str.replace("__DASHBOARD__", dashboard_str)
    web_page_str = web_page_str.replace("__REFRESH_URL__", "/zone")
    web_page_str = web_page_str.replace("__RELOAD__", str(gv_json_config['discovery']['device_probe_interval'] * 1000))

    return web_page_str


def build_device_web_page(num_cols):
    global gv_sunset_time
    global gv_actual_sunset_time
    global gv_sunrise_time
    global gv_actual_sunrise_time
    global gv_json_config

    # safe snapshot of dict keys into list
    device_list = list(gv_jbhasd_device_status_dict)
    url_dict_copy = copy.deepcopy(gv_jbhasd_device_url_dict)

    # We'll build one string of data
    # for the jquery code defining
    # the clicking and load actions
    # The switch_id number will be incremented
    # as we define switches and matched between the 
    # generated HTML for the switch and jquery
    # code for the click action
    jquery_str = ""

    # For HTML content, its a list of columns
    # initialised to blank strings
    # also init size dict to 0 for each column
    dashboard_col_list = []
    dashboard_col_size_dict = {}
    for i in range(0, num_cols):
        dashboard_col_list.append("")
        dashboard_col_size_dict[i] = 0

    device_id = 0
    switch_id = 0

    # Track the size of each device in terms of number
    # of controls and sensors
    # will use this to then to control a balanced 
    # distribution of widgets into vertical columns
    device_size_dict = {}
    for device_name in device_list:
        json_data = gv_jbhasd_device_status_dict[device_name]
        device_name = json_data['name']
        device_size = (len(json_data['controls']) + 2)
        device_size_dict[device_name] = device_size

    # start the dash-box widget
    dashboard_col_list[0] += (
            '<div class="dash-box">'
            '<p class="dash-title">Control Panel</p>'
            '<table border="0" padding="3" width="100%%">') 

    dashboard_col_list[0] += (
            '<tr>'
            '<td class="dash-label">Devices</td>'
            '<td align="center" class="dash-label">'
            '%s'
            '</td>'
            '</tr>') % (len(device_list))

    dashboard_col_list[0] += (
            '<tr>'
            '<td class="dash-label">Sunset</td>'
            '<td align="center" class="dash-label">'
            '%s'
            '</td>'
            '</tr>') % (gv_actual_sunset_time)

    dashboard_col_list[0] += (
            '<tr>'
            '<td class="dash-label">Sunrise</td>'
            '<td align="center" class="dash-label">'
            '%s'
            '</td>'
            '</tr>') % (gv_actual_sunrise_time)

    dashboard_col_list[0] += (
            '<tr>'
            '<td class="dash-label">Offset (secs)</td>'
            '<td align="center" class="dash-label">'
            '%s'
            '</td>'
            '</tr>') % (gv_json_config['sunset']['offset'])

    # reboot URL for all devices
    href_url = ('/api?device=all&reboot=1')

    # jquery code for click
    reboot_str = click_get_reload_template
    jquery_click_id = 'reboot_all'
    reboot_str = reboot_str.replace("__ID__", jquery_click_id)
    reboot_str = reboot_str.replace("__ACTION_URL__", href_url)
    jquery_str += reboot_str

    dashboard_col_list[0] += (
            '<tr>'
            '<td class="dash-label">Reboot All</td>'
            '<td align="center">'
            '<label class="switch">'
            '<input type="checkbox" id="%s">'
            '<div class="slider round"></div>'
            '</label>'
            '</td>'
            '</tr>') % (jquery_click_id)

    # reconfig URL for all devices
    href_url = ('/api?device=all&reconfig=1')

    # jquery code for click
    reconfig_str = click_get_reload_template
    jquery_click_id = 'reconfig_all'
    reconfig_str = reconfig_str.replace("__ID__", jquery_click_id)
    reconfig_str = reconfig_str.replace("__ACTION_URL__", href_url)
    jquery_str += reconfig_str

    dashboard_col_list[0] += (
            '<tr>'
            '<td class="dash-label">Reconfigure All</td>'
            '<td align="center">'
            '<label class="switch">'
            '<input type="checkbox" id="%s">'
            '<div class="slider round"></div>'
            '</label>'
            '</td>'
            '</tr>') % (jquery_click_id)

    dashboard_col_list[0] += '</table></div>'

    # account for size of master dash box
    dashboard_col_size_dict[0] += 6 

    for device_name in device_list:
        json_data = gv_jbhasd_device_status_dict[device_name]
        device_name = json_data['name']
        zone_name = json_data['zone']
        url = url_dict_copy[device_name]

        # determine col_index
        # based on smallest accumulated size of 
        # existing columns
        col_index = 0
        smallest_col_size = 0
        for i in range (0, num_cols):
            # find smallest column
            # defaulting with first
            if i == 0 or dashboard_col_size_dict[i] < smallest_col_size:
                col_index = i
                smallest_col_size = dashboard_col_size_dict[i]

        # add zize of selected zone to tracked size per column
        dashboard_col_size_dict[col_index] += device_size_dict[device_name]
        
        # start the dash-box widget
        dashboard_col_list[col_index] += (
                '<div class="dash-box">'
                '<p class="dash-title">%s<br>%s</p>'
                '<table border="0" padding="3" width="100%%">') % (device_name,
                                                                   zone_name)

        dashboard_col_list[col_index] += (
                '<tr>'
                '<td class="dash-label">Get JSON</td>'
                '<td align="center" class="dash-title">'
                '<a href="%s" target="json-window" title="View JSON">&#x1f4c4;</a>'
                '</td>'
                '</tr>') % (url)

        # reboot URL
        # carries device name and reboot=1
        # directive
        url_safe_device = urllib.parse.quote_plus(device_name)
        href_url = ('/api?device=%s'
                    '&reboot=1') % (url_safe_device)

        # jquery code for reboot click
        reboot_str = click_get_reload_template
        jquery_click_id = 'reboot_device%d' % (device_id)
        reboot_str = reboot_str.replace("__ID__", jquery_click_id)
        reboot_str = reboot_str.replace("__ACTION_URL__", href_url)
        jquery_str += reboot_str

        dashboard_col_list[col_index] += (
                '<tr>'
                '<td class="dash-label">Reboot</td>'
                '<td align="center">'
                '<label class="switch">'
                '<input type="checkbox" id="%s">'
                '<div class="slider round"></div>'
                '</label>'
                '</td>'
                '</tr>') % (jquery_click_id)

        # reconfig URL
        # carries device name and reconfig=1
        # directive
        url_safe_device = urllib.parse.quote_plus(device_name)
        href_url = ('/api?device=%s'
                    '&reconfig=1') % (url_safe_device)

        # jquery code for reboot click
        reconfig_str = click_get_reload_template
        jquery_click_id = 'reconfig_device%d' % (device_id)
        reconfig_str = reconfig_str.replace("__ID__", jquery_click_id)
        reconfig_str = reconfig_str.replace("__ACTION_URL__", href_url)
        jquery_str += reconfig_str

        dashboard_col_list[col_index] += (
                '<tr>'
                '<td class="dash-label">Reconfigure</td>'
                '<td align="center">'
                '<label class="switch">'
                '<input type="checkbox" id="%s">'
                '<div class="slider round"></div>'
                '</label>'
                '</td>'
                '</tr>') % (jquery_click_id)

        # apmode URL
        # carries device name and apmode=1
        # directive
        url_safe_device = urllib.parse.quote_plus(device_name)
        href_url = ('/api?device=%s'
                    '&apmode=1') % (url_safe_device)

        # jquery code for apmode click
        apmode_str = click_get_reload_template
        jquery_click_id = 'apmode_device%d' % (device_id)
        apmode_str = apmode_str.replace("__ID__", jquery_click_id)
        apmode_str = apmode_str.replace("__ACTION_URL__", href_url)
        jquery_str += apmode_str

        dashboard_col_list[col_index] += (
                '<tr>'
                '<td class="dash-label">AP Mode</td>'
                '<td align="center">'
                '<label class="switch">'
                '<input type="checkbox" id="%s">'
                '<div class="slider round"></div>'
                '</label>'
                '</td>'
                '</tr>') % (jquery_click_id)

        # Controls in this zone
        # Controls
        for control in json_data['controls']:
            control_name = control['name']
            control_type = control['type']

            if control_type == 'switch':
                control_state = int(control['state'])
                alternate_state = (control_state + 1) % 2

                # prep args for transport
                url_safe_device = urllib.parse.quote_plus(device_name)
                url_safe_zone = urllib.parse.quote_plus(zone_name)
                url_safe_control = urllib.parse.quote_plus(control_name)

                # href URL for generated html
                # This is a URL to the webserver
                # carrying the device URL and directives
                # to change the desired switch state
                href_url = ('/api?device=%s'
                            '&zone=%s'
                            '&control=%s'
                            '&state=%d') % (url_safe_device,
                                            url_safe_zone,
                                            url_safe_control,
                                            alternate_state)

                if (alternate_state == 1):
                    checked_str = ""
                else:
                    checked_str = "checked"

                # Jquery code for the click state
                # Generated with the same switch id
                # to match the ckick action to the related
                # url and checkbox switch
                switch_str = click_get_reload_template
                jquery_click_id = 'switch%d' % (switch_id)
                switch_str = switch_str.replace("__ID__", jquery_click_id)
                switch_str = switch_str.replace("__ACTION_URL__", href_url)
                jquery_str += switch_str

                # format checkbox css slider in table cell
                # with id set to the desired switch_id string
                # the checked_str also ensures the checkbox is 
                # rendered in the current state
                dashboard_col_list[col_index] += (
                        '<tr>'
                        '<td class="dash-label">%s</td>'
                        '<td align="center">'
                        '<label class="switch">'
                        '<input type="checkbox" id="%s" %s>'
                        '<div class="slider round"></div>'
                        '</label>'
                        '</td>'
                        '</tr>') % (control_name,
                                    jquery_click_id,
                                    checked_str)

                # increment for next switch
                switch_id += 1

        # Spacing between controls and sensors
        dashboard_col_list[col_index] += '<tr><td></td></tr>'
        dashboard_col_list[col_index] += '<tr><td></td></tr>'
        dashboard_col_list[col_index] += '<tr><td></td></tr>'

        # Sensors
        for sensor in json_data['controls']:
            sensor_name = sensor['name']
            sensor_type = sensor['type']

            if (sensor_type == 'temp/humidity'):
                temp = sensor['temp']
                humidity = sensor['humidity']

                # &#x1f321 thermometer temp
                # &#x1f322 droplet humidity
                dashboard_col_list[col_index] += (
                        '<tr>'
                        '<td class="dash-label" width="50%%">%s</td>'
                        '<td class="dash-label">'
                        '<table border="0" width="100%%">'
                        '<tr><td class="dash-label" align="center">&#x263C;</td>'
                        '<td class="dash-label" align="left">%s C</td></tr>'
                        '<tr><td class="dash-label" align="center">&#x1F4A7;</td>'
                        '<td class="dash-label" align="left">%s %%</td></tr>'
                        '</table></td>'
                        '</tr>') % (sensor_name,
                                    temp,
                                    humidity)

                dashboard_col_list[col_index] += '<tr><td></td></tr>'
                dashboard_col_list[col_index] += '<tr><td></td></tr>'

            if (sensor_type == 'rgb'):
                current_colour = sensor['current_colour']

                dashboard_col_list[col_index] += (
                        '<tr>'
                        '<td class="dash-label">%s</td>'
                        '<td style="color:#%s" align="center">'
                        '<span title="%s">'
                        '&#x2B24;&#x2B24;&#x2B24;&#x2B24;&#x2B24;'
                        '</span>'
                        '</td>'
                        '</tr>') % (sensor_name,
                                    current_colour[4:],
                                    current_colour)

                dashboard_col_list[col_index] += '<tr><td></td></tr>'
                dashboard_col_list[col_index] += '<tr><td></td></tr>'

        # terminate the zone table and container div
        dashboard_col_list[col_index] += '</table></div>'
        device_id += 1

    # Build the dashboard portion
    # It's the timestamp and then a single row table, 
    # one cell per vertical column.
    # Table width is set to 50% force it compress
    # more. Otherwise it will tend to go for a 100% fill
    # and the odd column will be given more width.
    # Cells are vertically aligned to top to keep widgets 
    # top-down in layout and not vertically-centred

    dashboard_str = ('<div class="timestamp" align="right">'
                     'Updated %s</div>') % (time.asctime())

    dashboard_str += '<table border="0" width="50%%"><tr>'
    for col_str in dashboard_col_list:
        dashboard_str += '<td valign="top">'
        dashboard_str += col_str
        dashboard_str += '</td>'
    dashboard_str += '</tr></table>'

    # Build and return the web page
    # dropping in CSS, generated jquery code
    # and dashboard.
    web_page_str = web_page_template
    web_page_str = web_page_str.replace("__TITLE__", "JBHASD Device Console")
    web_page_str = web_page_str.replace("__CSS__", web_page_css)
    web_page_str = web_page_str.replace("__DASHBOX_WIDTH__", str(gv_json_config['dashboard']['box_width']))
    web_page_str = web_page_str.replace("__SWITCH_FUNCTIONS__", jquery_str)
    web_page_str = web_page_str.replace("__DASHBOARD__", dashboard_str)
    web_page_str = web_page_str.replace("__REFRESH_URL__", "/device")
    web_page_str = web_page_str.replace("__RELOAD__", str(gv_json_config['discovery']['device_probe_interval'] * 1000))

    return web_page_str


def process_console_action(
        device, 
        zone, 
        control, 
        reboot, 
        reconfig, 
        apmode, 
        state, 
        program):


    # list used to buok handle
    # simple URL API calls for GET
    # use cases.. reboot. reconfigure, apmode
    command_url_list = []
    reboot_all = 0
    reconfig_all = 0

    if (device is not None and
        reboot is not None):

        if device == 'all':
            print("%s Rebooting all devices" % (time.asctime()))
            reboot_all = 1
            for device in gv_jbhasd_device_url_dict:
                url = gv_jbhasd_device_url_dict[device]

                print("%s Rebooting %s" % (time.asctime(),
                                           device))

                command_url_list.append('%s/reboot' % (url))

        elif device in gv_jbhasd_device_url_dict:
            url = gv_jbhasd_device_url_dict[device]

            print("%s Rebooting %s" % (time.asctime(),
                                       device))

            command_url_list.append('%s/reboot' % (url))
            
    if (device is not None and
        reconfig is not None):

        if device == 'all':
            print("%s Reconfiguring all devices" % (time.asctime()))
            reconfig_all = 1
            for device in gv_jbhasd_device_url_dict:
                url = gv_jbhasd_device_url_dict[device]

                print("%s Reconfiguring %s" % (time.asctime(),
                                               device))

                command_url_list.append('%s/reconfigure' % (url))

        elif device in gv_jbhasd_device_url_dict:
            url = gv_jbhasd_device_url_dict[device]

            print("%s Reconfiguring %s" % (time.asctime(),
                                           device))

            command_url_list.append('%s/reconfigure' % (url))

    if (device is not None and
        apmode is not None):

        if device in gv_jbhasd_device_url_dict:
            url = gv_jbhasd_device_url_dict[device]

            print("%s Rebooting %s into AP Mode" % (time.asctime(),
                                                    device))

            command_url_list.append('%s/apmode' % (url))

    if (device is not None and
        zone is not None and
        control is not None and
        state is not None):

        if device in gv_jbhasd_device_url_dict:
            url = gv_jbhasd_device_url_dict[device]

            print("%s Manually setting %s/%s to state:%s" % (time.asctime(),
                                                             zone,
                                                             control,
                                                             state))

            # Construct JSON POST body to set control state
            json_req = format_control_request(control, 
                                              'state',                          
                                              state)
            json_data = post_url(url + '/control', 
                                 json_req,
                                 gv_http_timeout_secs)
            if (json_data is not None):
                # update the status and ts as returned
                gv_jbhasd_device_status_dict[device] = json_data
                gv_jbhasd_device_ts_dict[device] = int(time.time())

    if (device is not None and
        zone is not None and
        control is not None and
        program is not None):

        if device in gv_jbhasd_device_url_dict:
            url = gv_jbhasd_device_url_dict[device]

            print("%s Manually setting %s/%s to program:%s" % (time.asctime(),
                                                               zone,
                                                               control,
                                                               program))

            # Construct JSON POST body to set program 
            json_req = format_control_request(control, 
                                              'program',                          
                                              program)
            json_data = post_url(url + '/control', 
                                 json_req,
                                 gv_http_timeout_secs)
            if (json_data is not None):
                # update the status and ts as returned
                gv_jbhasd_device_status_dict[device] = json_data
                gv_jbhasd_device_ts_dict[device] = int(time.time())


    # Bulk stuff
    for url in command_url_list:
        print("%s Issuing command url:%s" % (time.asctime(),
                                             url))

        json_data = get_url(url, gv_http_timeout_secs, 1)
        if (json_data is not None):
            # update the status and ts as returned
            gv_jbhasd_device_status_dict[device] = json_data
            gv_jbhasd_device_ts_dict[device] = int(time.time())


    # If a reboot all was performed
    # we need to wipe the dicts now
    if (reboot_all == 1 or reconfig_all == 1):
        reset_all_dicts()

    return 


#This class will handle any incoming request from
#the browser or devices
class web_console_zone_handler(object):
    @cherrypy.expose()

    def index(self, width=None):

        print("%s client:%s:%d params:%s" % (time.asctime(),
                                             cherrypy.request.remote.ip,
                                             cherrypy.request.remote.port,
                                             cherrypy.request.params))
        # Normal client without width
        if width is None:
            print("%s forcing reload to get width" % time.asctime())
            reload_str = web_page_reload_template
            reload_str = reload_str.replace("__REFRESH_URL__", "/zone")
            return reload_str

        # set default cols
        # Then calculate more accurate version based on 
        # supplied window width divided by dashbox width
        # plus an offset for padding consideration
        num_cols = gv_json_config['dashboard']['initial_num_columns']
        if width is not None:
            num_cols = int(int(width) / (gv_json_config['dashboard']['box_width'] + 
                                         gv_json_config['dashboard']['col_division_offset']))

        # return dashboard in specified number of 
        # columns
        return build_zone_web_page(num_cols)

    # Force trailling slash off on called URL
    index._cp_config = {'tools.trailing_slash.on': False}


class web_console_device_handler(object):
    @cherrypy.expose()

    def index(self, width=None):

        print("%s device client:%s:%d params:%s" % (time.asctime(),
                                                    cherrypy.request.remote.ip,
                                                    cherrypy.request.remote.port,
                                                    cherrypy.request.params))
        # Normal client without width
        if width is None:
            print("%s forcing reload to get width" % time.asctime())
            reload_str = web_page_reload_template
            reload_str = reload_str.replace("__REFRESH_URL__", "/device")
            return reload_str

        # set defautl cols
        # Then calculate more accurate version based on 
        # supplied window width divided by dashbox width
        # plus an offset for padding consideration
        num_cols = gv_json_config['dashboard']['initial_num_columns']
        if width is not None:
            num_cols = int(int(width) / (gv_json_config['dashboard']['box_width'] + 
                                         gv_json_config['dashboard']['col_division_offset']))

        # return dashboard in specified number of 
        # columns
        return build_device_web_page(num_cols)

    # Force trailling slash off on called URL
    index._cp_config = {'tools.trailing_slash.on': False}


class web_console_api_handler(object):
    @cherrypy.expose()

    def index(self, 
              device=None, 
              zone=None, 
              control=None, 
              state=None, 
              program=None, 
              reboot=None,
              reconfig=None,
              update=None,
              apmode=None):

        print("%s json client:%s:%d params:%s" % (time.asctime(),
                                                  cherrypy.request.remote.ip,
                                                  cherrypy.request.remote.port,
                                                  cherrypy.request.params))
        # process actions if present
        process_console_action(device, 
                               zone, 
                               control, 
                               reboot, 
                               reconfig,
                               apmode, 
                               state, 
                               program)

        # Return nothing
        return ""

    # Force trailling slash off on called URL
    index._cp_config = {'tools.trailing_slash.on': False}


def web_server():

    print("%s Starting console web server on port %d" % (time.asctime(),
                                                         gv_json_config['web']['port']))
    # Disable logging
    do_logging = 1
    if do_logging == 0:
        cherrypy.config.update({'environment': 'production',
                                'log.screen': False,
                                'log.access_file': '',
                                'log.error_file': ''})

    # Listen on our port on any IF
    cherrypy.server.socket_host = '0.0.0.0'
    cherrypy.server.socket_port = gv_json_config['web']['port']

    # Authentication
    # If the users section in web config is populated
    # We generate a config string with HTTP digest
    # enabled
    users = gv_json_config['web']['users']
    if len(users) > 0:
        ha1 = cherrypy.lib.auth_digest.get_ha1_dict_plain(users)

        # Generate a random digest auth key
        # Might help against is getting compromised
        random.seed()
        digest_key = hex(random.randint(0x1000000000000000,
                                        0xFFFFFFFFFFFFFFFF))
        digest_conf = {
                'tools.auth_digest.on': True,
                'tools.auth_digest.realm': 'localhost',
                'tools.auth_digest.get_ha1': ha1,
                'tools.auth_digest.key': digest_key,
        }

        conf = {
            '/': digest_conf
        }
    else:
        print("%s No users provisioned in config.. bypassing authentation" % (time.asctime()))
        conf = {}

    # Set webhooks for zone and device
    cherrypy.tree.mount(web_console_zone_handler(), '/zone', conf)
    cherrypy.tree.mount(web_console_device_handler(), '/device', conf)

    # webhook for API
    cherrypy.tree.mount(web_console_api_handler(), '/')
    cherrypy.tree.mount(web_console_api_handler(), '/api')

    # Cherrypy main loop
    cherrypy.engine.start()
    cherrypy.engine.block()

# main()

# Analytics
analytics_file = open("analytics.csv", "a")

thread_list = []

# config thread
config_t = threading.Thread(target = manage_config)
config_t.daemon = True
config_t.start()
thread_list.append(config_t)

# Allow some grace for config to load
time.sleep(2)

# device discovery thread
discover_t = threading.Thread(target = discover_devices)
discover_t.daemon = True
discover_t.start()
thread_list.append(discover_t)

# device probe thread
probe_t = threading.Thread(target = probe_devices)
probe_t.daemon = True
probe_t.start()
thread_list.append(probe_t)

# Web server
web_server_t = threading.Thread(target = web_server)
web_server_t.daemon = True
web_server_t.start()
thread_list.append(web_server_t)

while (1):
    dead_threads = 0
    for thread in thread_list:
         if (not thread.isAlive()):
             dead_threads += 1

    if (dead_threads > 0):
        print("%s Detected %d dead threads.. exiting" % (time.asctime(),
                                                         dead_threads))
        sys.exit(-1);

    time.sleep(5)
