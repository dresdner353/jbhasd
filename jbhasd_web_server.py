
# Cormac Long April 2017
#
# Simple webserver script
# to detect specified on-network JBHASD devices
# and present a crude web I/F for turning 
# them off/on as required
# It also uses a register of automated devices
# and time parameters to use in automating the on and
# off times for the devices.

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

web_page_template = """
<head>
    <title>__TITLE__</title>
    <meta id="META" name="viewport" content="width=device-width, initial-scale=1.0" >
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.0.0-beta1/dist/css/bootstrap.min.css" rel="stylesheet" integrity="sha384-giJF6kkoqNQ00vy+HMDP7azOuL0xtbfIcaT9wjKHr8RbDVddVHyTfAAsrekwKmP1" crossorigin="anonymous">
    <link href="https://fonts.googleapis.com/icon?family=Material+Icons" rel="stylesheet">
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.5.1/jquery.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.0.0-beta1/dist/js/bootstrap.bundle.min.js" integrity="sha384-ygbV9kiqUc6oa4msXn9868pTtWMgiQaeYH7/t7LECLbyPA2x65Kgf80OJFdroafW" crossorigin="anonymous"></script>

    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js"></script>
    <style type="text/css">__CSS__</style>

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
    
    function refreshPage() {
        if (window_focus == true) {
            $.get("__REFRESH_URL__", function(data, status){
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

web_page_css = """

.material-icons.md-18 { font-size: 18px; }
.material-icons.md-24 { font-size: 24px; }
.material-icons.md-36 { font-size: 36px; }
.material-icons.md-48 { font-size: 48px; }

"""

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
                $.get("__REFRESH_URL__", function(data, status){
                    // clear refresh timer before reload
                    clearInterval(refresh_timer);
                    $("#dashboard").html(data);
                });
            });
        });
"""

# action code for combos
# click function resets refresh timer to 
# 2 minutes
change_get_reload_template = """
        $('#__ID__').on('change', function() {
            $.get('__ACTION_URL__&__ARG__=' + this.value, function(data, status) {
                $.get("__REFRESH_URL__", function(data, status){
                    // clear refresh timer before reload
                    clearInterval(refresh_timer);
                    $("#dashboard").html(data);
                });
            });
        });

        $('#__ID__').on('click', function() {
          // opened combo.. set refresh to a longer interval
          clearInterval(refresh_timer);
          refresh_timer = setInterval(refreshPage, 120000);
        })
"""


### End switch on click template

### Begin Web page reload template
web_page_reload_template = """
<head>
    <meta id="META" name="viewport" content="width=device-width, initial-scale=1.0" >
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js"></script>
    <script>

    $(document).ready(function(){
        reload();
    });

    function reload() {
        $.get("__REFRESH_URL__", function(data, status){
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


# Config
gv_home_dir = os.path.expanduser('~')
gv_config_file = gv_home_dir + '/.jbhasd_web_server'
gv_json_config = {}

def log_message(message):
    print("%s %s" % (
        time.asctime(),
        message))
    sys.stdout.flush()


def set_default_config():
    global gv_config_file

    log_message("Setting config defaults")
    json_config = {}
    # discovery
    json_config['discovery'] = {}
    json_config['discovery']['device_probe_interval'] = 10
    json_config['discovery']['device_purge_threshold'] = 120

    # web
    json_config['web'] = {}
    json_config['web']['port'] = 8080
    json_config['web']['users'] = {}

    # dashboard
    json_config['dashboard'] = {}
    json_config['dashboard']['initial_num_columns'] = 1
    json_config['dashboard']['col_division_offset'] = 20

    # sunset
    json_config['sunset'] = {}
    json_config['sunset']['url'] = 'http://api.sunrise-sunset.org/json?lat=53.349809&lng=-6.2624431&formatted=0'
    json_config['sunset']['offset'] = 1800
    json_config['sunset']['refresh'] = 3600

    # Timed & paired controls
    json_config['device_programs'] = []
    json_config['paired_switches'] = []

    # Device config
    json_config['device_profiles'] = []
    json_config['devices'] = []
    json_config['rgb_programs'] = {}
    json_config['argb_programs'] = {}

    # Timezone
    json_config['timezone'] = 'Europe/Dublin'

    return json_config


def load_config(config_file):

    log_message("Loading config from %s" % (config_file))
    try:
        config_data = open(config_file).read()
        json_config = json.loads(config_data)
    except Exception as ex: 
        log_message("load config failed: %s" % (ex))
        json_config = None

    return json_config


def save_config(json_config, config_file):
    log_message("Saving config to %s" % (config_file))
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


def get_event_time(timer_time):

    global gv_sunset_time
    global gv_sunrise_time

    if (timer_time == "sunset"):
        time_ival = gv_sunset_time
    elif (timer_time == "sunrise"):
        time_ival = gv_sunrise_time
    else:
        time_ival = int(timer_time.replace(':', ''))

    #log_message('%s -> %d' % (timer_time, time_ival))
    return time_ival

# Sunset globals
gv_last_sunset_check = -1
gv_actual_sunset_time = "20:00"
gv_actual_sunrise_time = "05:00"
gv_sunset_time = get_event_time(gv_actual_sunset_time)
gv_sunrise_time = get_event_time(gv_actual_sunrise_time)

# device global dictionaries

# device dict 
# keyed on name
gv_device_dict = {}

# timeout for all fetch calls
gv_http_timeout_secs = 10

# Dict to map switch context to Unicode
# symbol
gv_context_symbol_dict = {
        'network' : 'settings_remote',
        'manual' : 'touch_app',
        'init' : '',
        'motion' : 'directions_walk',
        }

# Maps switch context to title
# hover text
gv_context_title_dict = {
        'network' : 'Network/Timer Controlled',
        'manual' : 'Manual Button Press',
        'init' : ' ',
        'motion' : 'Motion Detected',
        }


def purge_all_devices():
    # wipe all dicts for tracked devices, states etc
    global gv_device_dict

    log_message("Resetting all device dictionaries")

    gv_device_dict = {}
    return


def purge_device(device_name, reason):
    # wipe single device from dicts etc
    global gv_device_dict

    log_message("Purging Device:%s reason:%s" % (
        device_name, 
        reason))

    if (device_name in gv_device_dict):
        del gv_device_dict[device_name]

    return


def track_device_status(device_name, url, json_data):
    # track device status data and timestamp
    global gv_device_dict

    # device might have been purged and no longer
    # tracked in dict.. in which case.. skip
    if device_name in gv_device_dict:
        device = gv_device_dict[device_name]
        device['status'] = json_data
        device['last_updated'] = int(time.time())
    return


def track_control_program(device_name, control_name, event):
    global gv_device_dict

    device = gv_device_dict[device_name]

    if not control_name in device['program_reg']:
        device['program_reg'][control_name] = {}

    # Track the time we programmed the given control
    device['program_reg'][control_name][event] = int(time.time())
    return


def sunset_api_time_to_epoch(time_str, local_timezone):
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


def check_control(
        device_name,
        zone_name, 
        control_name):

    global gv_device_dict
    global gv_sunset_time
    global gv_sunrise_time
    global gv_json_config

    log_message('check_control(device=%s, zone=%s, control=%s' % (
        device_name,
        zone_name,
        control_name))

    current_time = int(time.strftime("%H%M", time.localtime()))
    current_time_rel_secs = ((int(current_time / 100) * 60 * 60) + 
            ((current_time % 100) * 60))

    device = gv_device_dict[device_name]

    # device programs
    for device_program in gv_json_config['device_programs']:

        # skip programs based on non-match of zone/control 
        # and if they are disabled
        if (device_program['zone'] != zone_name or 
                device_program['control'] != control_name or
                not device_program['enabled']):
            continue

        for event in device_program['events']:

            # Event times
            # either a 'times' list or a single 'time'
            if 'times' in event:
                times_list = event['times']
            else:
                times_list = []
                times_list.append(event['time'])

            for event_time_str in times_list:
                event_time = get_event_time(event_time_str)

                # Convert HHMM int values into relative seconds for day
                event_time_rel_secs = ((int(event_time / 100) * 60 * 60) + 
                        ((event_time % 100) * 60))

                last_program_epoch = 0
                if (control_name in device['program_reg'] and
                        event_time in device['program_reg'][control_name]):
                    last_program_epoch = device['program_reg'][control_name][event_time]
                last_program_interval = int(time.time()) - last_program_epoch

                program_threshold = (current_time_rel_secs - event_time_rel_secs) % 86400 
                log_message('Event:%s ev_rel:%d now_rel:%d threshold:%d last_programmed_interval:%d' % (
                    event_time,
                    event_time_rel_secs,
                    current_time_rel_secs,
                    program_threshold,
                    last_program_interval))

                if (program_threshold < 60):
                    if (last_program_interval > 60):
                        control_data = copy.deepcopy(event['params'])
                        control_data['name'] = control_name

                        # rgb/argb references
                        if ('program' in control_data and 
                                type(control_data['program']) == str):
                            program_name = control_data['program']
                            if program_name in gv_json_config['rgb_programs']:
                                control_data['program'] = gv_json_config['rgb_programs'][program_name]
                                log_message('Substituted referenced RGB program %s' % (program_name))
                            elif program_name in gv_json_config['argb_programs']:
                                control_data['program'] = gv_json_config['argb_programs'][program_name]
                                log_message('Substituted referenced ARGB program %s' % (program_name))

                        log_message('Returning control data.. %s' % (control_data))
                        return control_data, event_time
                    else:
                        log_message('Already programmed %d seconds ago' % (
                            last_program_interval))
                else:
                    log_message('Event time %s is outside of 1 min interval' % (
                        event_time_str))



    # paired switches
    # anything found on the b-side of the
    # will have its state to the state of the paired
    # a-side
    for paired_switch in gv_json_config['paired_switches']:
        if (paired_switch['b_zone'] == zone_name and 
                paired_switch['b_control'] == control_name):
            a_state = get_control_state(
                    paired_switch['a_zone'], 
                    paired_switch['a_control'])
            b_state = get_control_state(
                    paired_switch['b_zone'], 
                    paired_switch['b_control'])

            if (a_state != -1 and 
                    b_state != -1 and 
                    a_state != b_state):
                control_data = {}
                control_data['name'] = control_name
                control_data['state'] = a_state
                log_message('Returning paired switch control data.. %s:%s -> %s:%s .. %s' % (
                    paired_switch['a_zone'], 
                    paired_switch['a_control'],
                    paired_switch['b_zone'], 
                    paired_switch['b_control'],
                    control_data))
                return control_data, None

    # fall-through nothing to do
    log_message('No timer or paired data found')
    return None, None


class ZeroConfListener(object):  
    def remove_service(self, zeroconf, type, name):
        # Can ignore this as we will self-purge
        # unresponsive devices
        return

    def add_service(self, zeroconf, type, name):
        # extract dns-sd info, build URL
        # and store in set

        info = zeroconf.get_service_info(type, name)
        if info:
            # work around python 3.x variations on support of 
            # addresses array in the zeroconf service info
            if hasattr(info, 'addresses'):
                address = socket.inet_ntoa(info.addresses[0])
            else:
                address = socket.inet_ntoa(info.address)
            port = info.port
            url = "http://%s:%d" % (address, port)
            now = int(time.time())

            # Name is formatted 
            # JBHASD-XXXXXXXX._JBHASD._tcp.local.
            # So split on '.' and isolate field field
            fields = name.split('.')
            device_name = fields[0]

            # register in gloval device dict
            if not device_name in gv_device_dict:
                log_message("Discovered %s (%s)" % (
                    device_name,
                    url))

                # register empty device in global device dict
                device = {}
                device['name'] = device_name
                device['url'] = url
                device['status'] = {}
                device['status']['name'] = device_name
                device['status']['zone'] = 'Unknown'
                device['status']['controls'] = []
                device['last_updated'] = now
                device['program_reg'] = {}
                gv_device_dict[device_name] = device


        return


def discover_devices():

    zeroconf = None
    while (1):
        if zeroconf == None:
            # Zeroconf service listener for JBHASD devices
            zeroconf = Zeroconf()
            listener = ZeroConfListener()  
            zeroconf.add_service_listener(
                    "_JBHASD._tcp.local.", 
                    listener)

        # let it run for 120 seconds
        time.sleep(120)

        # close all resources to it will 
        # be recreated fresh
        # We need to do this to ensure that 
        # we continually refresh the set of URLs
        # inb conjunction with the purging model
        zeroconf.close()
        zeroconf = None

    return


def get_url(url, url_timeout, parse_json):
    # General purpose URL GETer
    # return contents of page and parsed as json
    # if the parse_json arg is 1

    global gv_device_dict

    # Try to determine the URL name
    url_name = "Unknown"
    for device_name in gv_device_dict:
        if (url == gv_device_dict[device_name]):
            url_name = device_name

    response_str = None
    response = None
    try:
        response = requests.get(url,
                                timeout = url_timeout)
    except:
        log_message("Error in GET Name:%s URL:%s" % (
            url_name, 
            url))

    if response:
        response_str = response.text

        if parse_json:
            try:
                json_data = response.json()
            except:
                log_message("Error in JSON parse.. Name:%s URL:%s Data:%s" % (
                    url_name, 
                    url, 
                    response_str))
                return None
            return json_data

    return response_str
     

def post_url(url, json_data, url_timeout):
    # General purpose URL POSTer
    # for JSON payloads
    # return contents parsed as json

    log_message("POST %s \n%s\n" % (url, json_data))

    response = None
    try:
        response = requests.post(url,
                                 json = json_data,
                                 timeout = url_timeout)
    except:
        log_message("Error in POST URL:%s" % (url))

    if response:
        try:
            json_data = response.json()
        except:
            log_message("Error in JSON parse.. URL:%s Data:%s" % (
                url, 
                response.text))
            return None

        return json_data

    return response


def get_control_state(zone_name, control_name):
    global gv_device_dict
    # Get state of specified control
    # return 0 or 1 or -1 (not found)

    state = -1

    device_list = list(gv_device_dict.keys())
    for device_name in device_list:
        device = gv_device_dict[device_name]

        json_data = device['status']
        device_zone_name = json_data['zone']

        if (device_zone_name != zone_name):
            continue

        # Control name check 
        for control in json_data['controls']:
            device_control_name = control['name']
            device_control_type = control['type']

            if (device_control_type == 'switch' and 
                    device_control_name == control_name):
                state = int(control['state'])

    return state


def check_automated_devices():
    device_list = list(gv_device_dict.keys())
    # get time in hhmm format
    for device_name in device_list:
        device = gv_device_dict[device_name]

        json_data = device['status']
        zone_name = json_data['zone']
        url = device['url']

        # Control status check 
        for control in json_data['controls']:
            control_name = control['name']
            control_type = control['type']

            # ignore controls that cannot be programmed
            if not control_type in ['switch', 'rgb', 'argb']:
                continue

            control_data, event_time = check_control(
                    device_name,
                    zone_name, 
                    control_name)
 
            if (control_data):
                log_message("Automatically setting %s/%s to %s" % (
                    zone_name,
                    control_name,
                    control_data))

                # Build controls request
                json_req = {}
                json_req['controls'] = []
                json_req['controls'].append(control_data)
                json_data = post_url(url + '/control', 
                                     json_req,
                                     gv_http_timeout_secs)
                if (json_data):
                    track_device_status(device_name, url, json_data)
                    if event_time:
                        track_control_program(device_name, control_name, event_time)

    return


def configure_device(url, device_name):

    log_message("Configure device %s" % (device_name))
    if (device_name in gv_json_config['devices']): 
        # matched to stored profile
        log_message("Matched device %s to stored profile.. configuring" % (device_name))
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

        # Copy over all top-level fields except controls
        for key in device_specific_dict:
            if (key != "controls"):
                config_dict[key] = device_specific_dict[key]

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
        # alternative but we have to match on the original control
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
            # values and custom name
            for config_control in config_dict['controls']:
                if config_control['name'] == control_name:
                    config_control.update(control)
                    config_control['name'] = custom_name


        # Format config dict to JSON
        device_config = json.dumps(
                config_dict, 
                indent=4, 
                sort_keys=True)
        log_message("Sending config (%d bytes):\n%s\n" % (
            len(device_config),
            device_config))

        # POST to /configure function of URL
        requests.post(url + '/configure', 
                      json = config_dict, 
                      timeout = gv_http_timeout_secs)

        # Remove it now from internal lists etc
        purge_device(device_name, "Reconfiguring")

    else:
        log_message("ERROR %s not found in device config" % (device_name))

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

    global gv_device_dict

    # loop forever
    while (1):
        # Sunset calculations
        # refresh this based on second interval
        # gv_json_config['sunset']['refresh']
        now = int(time.time())
        if ((now - gv_last_sunset_check) >= 
                gv_json_config['sunset']['refresh']):
            # Re-calculate
            log_message("Refreshing Sunset times (every %d seconds).." % (
                gv_json_config['sunset']['refresh']))
            json_data = get_url(gv_json_config['sunset']['url'], 20, 1)
            if json_data:
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

                log_message("Sunset time is %04d (with offset of %d seconds)" % (
                    gv_sunset_time,
                    gv_json_config['sunset']['offset']))

                log_message("Sunrise time is %04d (with offset of %d seconds)" % (
                    gv_sunrise_time,
                    gv_json_config['sunset']['offset']))

            gv_last_sunset_check = now

        # iterate set of discovered device URLs as snapshot list
        # avoids issues if the set is updated mid-way
        total_probes = len(gv_device_dict)
        successful_probes = 0
        failed_probes = 0
        purged_urls = 0
        control_changes = 0
        for device_name in list(gv_device_dict.keys()):
            device = gv_device_dict[device_name]
            url = device['url']
            json_data = get_url(url, gv_http_timeout_secs, 1)
            if (json_data and 'name' in json_data):
                device_name = json_data['name']
                if ('configured' in json_data and
                        json_data['configured'] == 0):
                    # Configure device
                    configure_device(url, device_name)
                else:
                    successful_probes += 1
                    # Track what we got back
                    # this is done after the compare above to ensure old is 
                    # checked against new
                    track_device_status(device_name, url, json_data)

            else:
                failed_probes += 1
                log_message('Failed to probe: %s .. response:%s' % (
                    url,
                    json_data))
        
        # Purge dead devices and URLs
        now = int(time.time())

        # iterate the known devices with status values 
        # that were previously recorded. 
        # Check for expired timestamps and purge
        device_name_list = list(gv_device_dict.keys())
        purged_devices = 0
        for device_name in device_name_list:
            device = gv_device_dict[device_name]

            last_updated = now - device['last_updated']
            if last_updated >= gv_json_config['discovery']['device_purge_timeout']:
                purged_devices += 1
                reason = "expired.. device %s (%s) last updated %d seconds ago" % (
                        device_name, 
                        device['url'],
                        last_updated)
                purge_device(device_name, reason)

        # Automated devices
        check_automated_devices()

        log_message("Probe.. total:%d successful:%d failed:%d purged:%d" % (
            total_probes,
            successful_probes,
            failed_probes,
            purged_devices))

        # loop sleep interval
        time.sleep(gv_json_config['discovery']['device_probe_interval'])
    return


def get_google_icon(control_name):
    icon_register = [
            {
                'google_icon' : 'wb_incandescent',
                'keywords' : [
                    'light',
                    'lamp',
                    ]
            },
            {
                'google_icon' : 'wb_iridescent',
                'keywords' : [
                    'rgb',
                    'argb',
                    'strip',
                    'white',
                    ]
            },
            {
                'google_icon' : 'speaker',
                'keywords' : [
                    'stereo',
                    'music',
                    'subwoofer',
                    'monitor',
                    'mixer',
                    ]
            },
            {
                'google_icon' : 'tv',
                'keywords' : [
                    'tv',
                    'cinema',
                    'avr',
                    ]
            },
            {
                'google_icon' : 'ac_unit',
                'keywords' : [
                    'fan',
                    'cooler',
                    'hvac',
                    'temp',
                    ]
            },
            {
                'google_icon' : 'nature_people',
                'keywords' : [
                    'tree',
                    'bush',
                    'hedge',
                    ]
            },
            {
                'google_icon' : 'pets',
                'keywords' : [
                    'dog',
                    'cat',
                    'deer',
                    ]
            },
            {
                'google_icon' : 'sensor_door',
                'keywords' : [
                    'door',
                    ]
            },
            {
                'google_icon' : 'videogame_asset',
                'keywords' : [
                    'xbox',
                    'playstation',
                    'nintendo',
                    ]
            },
            {
                'google_icon' : 'outlet',
                'keywords' : [
                    'socket',
                    'outlet',
                    'power',
                    ]
            },
            {
                'google_icon' : 'roofing',
                'keywords' : [
                    'roof',
                    'fascia',
                    'gutter',
                    ]
            },
            {
                'google_icon' : 'desktop_mac',
                'keywords' : [
                    'desk',
                    'computer',
                    'pc',
                    'mac',
                    ]
            },
    ]

    control_name_lcase = control_name.lower()
    for icon_obj in icon_register:
        for keyword in icon_obj['keywords']:
            if keyword in control_name_lcase:
                return icon_obj['google_icon']

    return 'outlet'


def build_zone_web_page():
    global gv_device_dict
    global gv_json_config

    # safe snapshot of dict keys into list
    device_list = list(gv_device_dict.keys())

    jquery_str = ""
    switch_id = 0

    switch_card_dict = {}
    other_card_dict = {}

    # collect zones in a set
    zone_set = set()

    # Controls population
    for device_name in device_list:
        device = gv_device_dict[device_name]
        json_data = device['status']

        zone_name = json_data['zone']
        zone_set.add(zone_name)
        url = device['url']

        # init card lists for zone
        if not zone_name in switch_card_dict:
            switch_card_dict[zone_name] = {}
            other_card_dict[zone_name] = {}

        # Controls
        for control in json_data['controls']:
            control_name = control['name']
            control_type = control['type']

            # prep args for transport
            url_safe_device = urllib.parse.quote_plus(device_name)
            url_safe_zone = urllib.parse.quote_plus(zone_name)
            url_safe_control = urllib.parse.quote_plus(control_name)

            if control_type == 'switch':
                control_state = int(control['state'])
                control_context = control['context']
                alternate_state = (control_state + 1) % 2

                href_url_off = (
                        '/api?device=%s'
                        '&zone=%s'
                        '&control=%s'
                        '&state=%d') % (
                                url_safe_device,
                                url_safe_zone,
                                url_safe_control,
                                0)

                href_url_on = (
                        '/api?device=%s'
                        '&zone=%s'
                        '&control=%s'
                        '&state=%d') % (
                                url_safe_device,
                                url_safe_zone,
                                url_safe_control,
                                1)


                card = (
                    '<div class="card text-center border-0">'
                    '<div class="card-body">'
                    '<i class="material-icons md-48">%s</i>'
                    '<i class="material-icons md-18">%s</i>'
                    '<p>%s</p>'
                    '<div class="btn-group">'
                    '<button type="button" id="switch%d" class="btn %s">Off</button>'
                    '<button type="button" id="switch%d" class="btn %s">On</button>'
                    '</div>'
                    '&nbsp;'
                    '</div>'
                    '</div>'
                    ) % (
                            get_google_icon(control_name),
                            gv_context_symbol_dict[control_context],
                            control_name,
                            switch_id,
                            'btn-primary' if control_state == 0 else 'btn-secondary',
                            switch_id + 1,
                            'btn-primary' if control_state == 1 else 'btn-secondary',
                            )

                switch_card_dict[zone_name][control_name] = card

                switch_str = click_get_reload_template
                jquery_click_id = 'switch%d' % (switch_id)
                switch_str = switch_str.replace("__ID__", jquery_click_id)
                switch_str = switch_str.replace("__ACTION_URL__", href_url_off)
                jquery_str += switch_str


                switch_str = click_get_reload_template
                jquery_click_id = 'switch%d' % (switch_id + 1)
                switch_str = switch_str.replace("__ID__", jquery_click_id)
                switch_str = switch_str.replace("__ACTION_URL__", href_url_on)
                jquery_str += switch_str

                # increment for next switch         
                switch_id += 2

            if control_type == 'temp/humidity':
                temp = control['temp']
                humidity = control['humidity']

                card = (
                    '<div class="card text-center border-0">'
                    '<div class="card-body">'
                    '<i class="material-icons md-48">%s</i>'
                    '<p>%s</p>'
                    '<p class="card-title">&#x1F321; %s C</p>'
                    '<p class="card-title">&#x1F4A7; %s %%</p>'
                    '</div>'
                    '</div>'
                    ) % (
                            get_google_icon(control_name),
                            control_name,
                            round(temp, 2),
                            round(humidity, 2)
                            )

                other_card_dict[zone_name][control_name] = card

            if control_type == 'rgb':
                current_colour = control['current_colour']

                prog_combo = None
                if 'rgb_programs' in gv_json_config:
                    prog_combo = (
                            '<center><select class="form-select form-select-sm" style="width:auto;"'
                            'aria-label=".form-select-sm example" '
                            'id="combo%d">'
                            '<option value="">Program</option>'
                            ) % (
                                    switch_id
                                    )

                    for program in gv_json_config['rgb_programs']:
                        prog_combo += (
                                '<option value="%s">%s</option>'
                                ) % (
                                        program,
                                        program.title())

                    prog_combo += (
                            '</select></center>'
                            )

                    href_url_program = (
                            '/api?zone=%s'
                            '&control=%s') % (
                                url_safe_zone,
                                url_safe_control)

                    combo_str = change_get_reload_template 
                    jquery_click_id = 'combo%d' % (switch_id)
                    combo_str = combo_str.replace("__ID__", jquery_click_id)
                    combo_str = combo_str.replace("__ACTION_URL__", href_url_program)
                    combo_str = combo_str.replace("__ARG__", 'rgb_program')
                    jquery_str += combo_str

                    switch_id += 1

                card = (
                    '<div class="card text-center border-0">'
                    '<div class="card-body">'
                    '<i class="material-icons md-48">%s</i>'
                    '<p>%s</p>'
                    '<span style="color:#%s">'
                    '&#x25cf;&#x25cf;&#x25cf;&#x25cf;&#x25cf;</span>'
                    '%s'
                    '</div>'
                    '</div>'
                    ) % (
                            get_google_icon(control_name),
                            control_name,
                            current_colour[4:],
                            prog_combo if prog_combo else ''
                            )

                other_card_dict[zone_name][control_name] = card

            if control_type == 'argb':

                prog_combo = None
                if 'argb_programs' in gv_json_config:
                    prog_combo = (
                            '<center><select class="form-select form-select-sm" style="width:auto;"'
                            'aria-label=".form-select-sm example" '
                            'id="combo%d">'
                            '<option value="">Program</option>'
                            ) % (
                                    switch_id
                                    )

                    for program in gv_json_config['argb_programs']:
                        prog_combo += (
                                '<option value="%s">%s</option>'
                                ) % (
                                        program,
                                        program.title())

                    prog_combo += (
                            '</select></center>'
                            )

                    href_url_program = (
                            '/api?zone=%s'
                            '&control=%s') % (
                                url_safe_zone,
                                url_safe_control)

                    combo_str = change_get_reload_template 
                    jquery_click_id = 'combo%d' % (switch_id)
                    combo_str = combo_str.replace("__ID__", jquery_click_id)
                    combo_str = combo_str.replace("__ACTION_URL__", href_url_program)
                    combo_str = combo_str.replace("__ARG__", 'argb_program')
                    jquery_str += combo_str

                    switch_id += 1

                colour_list = []
                if 'colours' in control['program']:
                    num_colours = len(control['program']['colours'])
                    # limit to 25 LEDs truncated across full list
                    if num_colours <= 25:
                        # entire list
                        colour_list = control['program']['colours']
                    else:
                        # every step colours based on division by 25
                        # min step size of 2
                        step = round(num_colours / 25)
                        if step < 2:
                            step = 2
                        for i in range(0, num_colours - 1, step):
                            colour_list.append(control['program']['colours'][i])

                # Iterate colours and format in stack of
                # 5 dots
                i = 0
                colour_str = ''
                for colour in colour_list:
                    colour_int = int(colour, 0)
                    colour_str += (
                            '<span style="color:#%06X">'
                            '&#x25cf;'
                            '</span>') % (colour_int)
                    i += 1
                    if (i % 5 == 0):
                        colour_str += '<br>'

                card = (
                    '<div class="card text-center border-0">'
                    '<div class="card-body">'
                    '<i class="material-icons md-48">%s</i>'
                    '<p>%s</p>'
                    '%s'
                    '%s'
                    '</div>'
                    '</div>'
                    ) % (
                            get_google_icon(control_name),
                            control_name,
                            colour_str,
                            prog_combo if prog_combo else ''
                            )

                other_card_dict[zone_name][control_name] = card

    dashboard_str = (
            '<ul class="nav nav-tabs">'
            '  <li class="nav-item">'
            '    <a class="nav-link active" aria-current="page" href="/zone">Zones</a>'
            '  </li>'
            '  <li class="nav-item">'
            '    <a class="nav-link" href="/device">Devices</a>'
            '</ul>'
            )

    dashboard_str += '<div class="container">'
    zone_list = list(zone_set)
    zone_list.sort()
    for zone_name in zone_list:
        num_controls = len(switch_card_dict[zone_name]) + len(other_card_dict[zone_name])
        dashboard_str += (
                '<div class="card border-start-0 border-end-0 border-bottom-0">'
                '<div class="card-body">'
                '<center><h6>%s</h6></center>'
                '<center><p><small>%d %s</small></p></center>'
                '<div class="container">'
                '<div class="row">'
                ) % (
                        zone_name,
                        num_controls,
                        'device' if num_controls == 1 else 'devices'
                        )

        for control_name in switch_card_dict[zone_name]:
            card = switch_card_dict[zone_name][control_name]
            dashboard_str += (
                    '<div class="col">%s</div>'
                    ) % (card)

        for control_name in other_card_dict[zone_name]:
            card = other_card_dict[zone_name][control_name]
            dashboard_str += (
                    '<div class="col">%s</div>'
                    ) % (card)


        dashboard_str += (
                '</div>'
                '</div>'
                '</div>'
                '</div>'
                ) 

    dashboard_str += (
            '</div>'
            '<br>'
            '<center><i><small><p class="fw-lighter">Last updated: %s</p></small></i><center>'
            ) % (time.asctime())

    # Build and return the web page
    # dropping in CSS, generated jquery code
    # and dashboard.
    web_page_str = web_page_template
    web_page_str = web_page_str.replace("__TITLE__", "JBHASD Zone Console")
    web_page_str = web_page_str.replace("__CSS__", web_page_css)
    web_page_str = web_page_str.replace("__SWITCH_FUNCTIONS__", jquery_str)
    web_page_str = web_page_str.replace("__DASHBOARD__", dashboard_str)
    web_page_str = web_page_str.replace("__REFRESH_URL__", "/zone")
    web_page_str = web_page_str.replace("__RELOAD__", str(gv_json_config['discovery']['device_probe_interval'] * 1000))

    #print(dashboard_str)

    return web_page_str


def build_device_web_page():
    global gv_sunset_time
    global gv_actual_sunset_time
    global gv_sunrise_time
    global gv_actual_sunrise_time
    global gv_json_config
    global gv_device_dict
    global gv_startup_time

    # build snapshot list of device dicts sorted on Name
    device_name_list = list(gv_device_dict.keys())
    now = int(time.time())
    device_list = []

    for device_name in device_name_list:
        device = gv_device_dict[device_name]
        json_data = device['status']
        device_list.append(json_data)

    device_list = sorted(
            device_list, 
            key=lambda k: k['zone'])

    jquery_str = ""
    device_id = 0

    dashboard_str = (
            '<ul class="nav nav-tabs">'
            '  <li class="nav-item">'
            '    <a class="nav-link " href="/zone">Zones</a>'
            '  </li>'
            '  <li class="nav-item">'
            '    <a class="nav-link active" aria-current="page" href="/device">Devices</a>'
            '</ul>'
            )

    # reboot URL for all devices
    href_url = ('/api?device=all&reboot=1')

    # jquery code for click
    reboot_str = click_get_reload_template
    jquery_reboot_all_click_id = 'reboot_all'
    reboot_str = reboot_str.replace("__ID__", jquery_reboot_all_click_id)
    reboot_str = reboot_str.replace("__ACTION_URL__", href_url)
    jquery_str += reboot_str

    # reconfig URL for all devices
    href_url = ('/api?device=all&reconfig=1')

    # jquery code for click
    reconfig_str = click_get_reload_template
    jquery_reconfig_all_click_id = 'reconfig_all'
    reconfig_str = reconfig_str.replace("__ID__", jquery_reconfig_all_click_id)
    reconfig_str = reconfig_str.replace("__ACTION_URL__", href_url)
    jquery_str += reconfig_str

    dashboard_str += (
            '<div class="card">'
            '<div class="card-body">'
            '<table border="0" padding="5">'
            '<tr><td>Started:</td><td>%s</td></tr>'
            '<tr><td>Discovered Devices:</td><td>%s</td></tr>'
            '<tr><td>Sunrise:</td><td>%s</td></tr>'
            '<tr><td>Sunset:</td><td>%s</td></tr>'
            '<tr><td>Sunrise/Sunset Offset:</td><td>%s (secs)</td></tr>'
            '</table>'
            '<br>'
            '<button type="button" id="%s" class="btn btn-primary">'
            'Reboot All Devices</button>&nbsp;'
            '<button type="button" id="%s" class="btn btn-primary">'
            'Reconfigure All Devices</button>&nbsp;'
            '  </div>'
            '</div>'
            ) % (
                    gv_startup_time,
                    len(device_list),
                    gv_actual_sunrise_time,
                    gv_actual_sunset_time,
                    gv_json_config['sunset']['offset'],
                    jquery_reboot_all_click_id,
                    jquery_reconfig_all_click_id 
                    )

    dashboard_str += (
            '<table class="table">'
            '  <thead>'
            '    <tr>'
            '      <th scope="col">Device Name</th>'
            '      <th scope="col">Zone</th>'
            '      <th scope="col">Admin</th>'
            '      <th scope="col">URL</th>'
            '      <th scope="col">Updated</th>'
            '      <th scope="col">Version</th>'
            '      <th scope="col">Uptime</th>'
            '      <th scope="col">Controls</th>'
            '      <th scope="col">Status Restarts</th>'
            '      <th scope="col">Signal Restarts</th>'
            '      <th scope="col">Free Memory</th>'
            '    </tr>'
            '  </thead>'
            '  <tbody>'
            )

    for device in device_list:
        device_id += 1
        device_name = device['name']
        zone_name = device['zone']

        # System data (only present in real devices)
        # So we condtionally access 
        version = 'N/A'
        uptime = 'N/A'
        memory = 0
        signal_restarts = 0
        status_restarts = 0
        if ('system' in device):
            version = device['system']['compile_date'].replace('JBHASD-VERSION ', '')
            uptime = device['system']['uptime']
            memory = device['system']['free_heap']
            signal_restarts = device['system']['signal_wifi_restarts']
            status_restarts = device['system']['status_wifi_restarts']

        device = gv_device_dict[device_name]
        url = device['url']
        last_update_ts = device['last_updated']

        # device reboot URL
        # carries device name and reboot=1
        # directive
        url_safe_device = urllib.parse.quote_plus(device_name)
        href_url = ('/api?device=%s'
                    '&reboot=1') % (url_safe_device)

        # jquery code for reboot click
        reboot_str = click_get_reload_template
        jquery_reboot_click_id = 'reboot_device%d' % (device_id)
        reboot_str = reboot_str.replace("__ID__", jquery_reboot_click_id)
        reboot_str = reboot_str.replace("__ACTION_URL__", href_url)
        jquery_str += reboot_str
        reboot_button_str = (
                '<button type="button" id="%s" class="btn btn-primary btn-sm">'
                '<i class="material-icons md-18">power_settings_new</i>'
                '</button>&nbsp;'
                ) % (
                        jquery_reboot_click_id
                        )

        # device reconfig URL
        # carries device name and reconfig=1
        # directive
        href_url = ('/api?device=%s'
                    '&reconfig=1') % (url_safe_device)

        # jquery code for reconfigure click
        reconfig_str = click_get_reload_template
        jquery_reconfig_click_id = 'reconfig_device%d' % (device_id)
        reconfig_str = reconfig_str.replace("__ID__", jquery_reconfig_click_id)
        reconfig_str = reconfig_str.replace("__ACTION_URL__", href_url)
        jquery_str += reconfig_str
        reconfig_button_str = (
                '<button type="button" id="%s" class="btn btn-primary btn-sm">'
                '<i class="material-icons md-18">autorenew</i>'
                '</button>&nbsp;'
                ) % (
                        jquery_reconfig_click_id
                        )

        # device apmode URL
        # carries device name and apmode=1
        # directive
        href_url = ('/api?device=%s'
                    '&apmode=1') % (url_safe_device)

        # jquery code for reconfigure click
        apmode_str = click_get_reload_template
        jquery_apmode_click_id = 'apmode_device%d' % (device_id)
        apmode_str = apmode_str.replace("__ID__", jquery_apmode_click_id)
        apmode_str = apmode_str.replace("__ACTION_URL__", href_url)
        jquery_str += apmode_str
        apmode_button_str = (
                '<button type="button" id="%s" class="btn btn-primary btn-sm">'
                '<i class="material-icons md-18">settings_input_antenna</i>'
                '</button>&nbsp;'
                ) % (
                        jquery_apmode_click_id
                        )
        dashboard_str += (
                '    <tr>'
                '      <td>%s</td>'
                '      <td>%s</td>'
                '      <td>%s</td>'
                '      <td><a href="%s" target="json">%s</a></td>'
                '      <td>%02d secs</td>'
                '      <td>%s</td>'
                '      <td>%s</td>'
                '      <td>%s</td>'
                '      <td>%s</td>'
                '      <td>%s</td>'
                '      <td>%.1f Kb</td>'
                '    </tr>'
                ) % (
                    device_name,
                    zone_name,
                    reboot_button_str + reconfig_button_str + apmode_button_str,
                    url,
                    url,
                    now - last_update_ts,
                    version,
                    uptime,
                    len(device['status']['controls']),
                    status_restarts,
                    signal_restarts,
                    memory / 1024
                    )

    dashboard_str += (
            '  </tbody>'
            '</table>'
            )

    # Build and return the web page
    # dropping in CSS, generated jquery code
    # and dashboard.
    web_page_str = web_page_template
    web_page_str = web_page_str.replace("__TITLE__", "JBHASD Zone Console")
    web_page_str = web_page_str.replace("__CSS__", web_page_css)
    web_page_str = web_page_str.replace("__SWITCH_FUNCTIONS__", jquery_str)
    web_page_str = web_page_str.replace("__DASHBOARD__", dashboard_str)
    web_page_str = web_page_str.replace("__REFRESH_URL__", "/device")
    web_page_str = web_page_str.replace("__RELOAD__", str(gv_json_config['discovery']['device_probe_interval'] * 1000))

    #print(dashboard_str)



    return web_page_str


def process_console_action(
        device_name, 
        zone, 
        control_name, 
        reboot, 
        reconfig, 
        apmode, 
        state, 
        rgb_program,
        argb_program):

    global gv_device_dict
    global gv_json_config

    # list used to buok handle
    # simple URL API calls for GET
    # use cases.. reboot. reconfigure, apmode
    command_url_list = []
    reboot_all = 0
    reconfig_all = 0

    if (device_name and reboot):

        if device_name == 'all':
            log_message("Rebooting all devices")
            reboot_all = 1
            for device_name in gv_device_dict:
                url = gv_device_dict[device_name]['url']

                log_message("Rebooting %s" % (device_name))

                command_url_list.append('%s/reboot' % (url))
            purge_all_devices()

        elif device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message("Rebooting %s" % (device_name))

            command_url_list.append('%s/reboot' % (url))
            purge_device(device_name, "Rebooting")
            
    elif (device_name and reconfig):

        if device_name == 'all':
            log_message("Reconfiguring all devices")
            reconfig_all = 1
            for device_name in gv_device_dict:
                url = gv_device_dict[device_name]['url']

                log_message("Reconfiguring %s" % (device_name))

                command_url_list.append('%s/reconfigure' % (url))

            # Dont purge devices here as the probe stage will
            # invoke the reconfigure

        elif device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message("Reconfiguring %s" % (device_name))

            command_url_list.append('%s/reconfigure' % (url))

            # Dont purge device_name here as the probe stage will
            # invoke the reconfigure

    elif (device_name and apmode):

        if device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message("Rebooting %s into AP Mode" % (device_name))

            command_url_list.append('%s/apmode' % (url))

    elif (device_name and zone and control_name and state):

        if device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message("Manually setting %s/%s/%s to state:%s" % (
                device_name,
                zone,
                control_name,
                state))

            control_data = {}
            control_data['name'] = control_name
            control_data['state'] = state
            json_req = {}
            json_req['controls'] = []
            json_req['controls'].append(control_data)
            json_data = post_url(url + '/control', 
                                 json_req,
                                 gv_http_timeout_secs)
            if (json_data):
                track_device_status(device_name, url, json_data)

    elif (zone and control_name and state):

        # Check all devices
        for device_name in gv_device_dict:
            json_data = gv_device_dict[device_name]['status']

            if ('zone' in json_data and 
                    json_data['zone'] == zone and
                    'controls' in json_data):
                for control in json_data['controls']:
                    if control['name'] == control_name:
                        url = gv_device_dict[device_name]['url']

                        log_message("Manually setting (%s) %s/%s/%s to state:%s" % (
                            url,
                            device_name,
                            zone,
                            control_name,
                            state))

                        control_data = {}
                        control_data['name'] = control_name
                        control_data['state'] = state
                        json_req = {}
                        json_req['controls'] = []
                        json_req['controls'].append(control_data)
                        json_data = post_url(url + '/control', 
                                             json_req,
                                             gv_http_timeout_secs)
                        if (json_data):
                            track_device_status(device_name, url, json_data)

    elif (device_name and zone and control_name and rgb_program):

        if device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message("Manually setting %s/%s/%s to rgb_program:%s" % (
                device_name,
                zone,
                control_name,
                rgb_program))

            if ('rgb_programs' in gv_json_config and 
                    rgb_program in gv_json_config['rgb_programs']):

                control_data = {}
                control_data['name'] = control_name
                control_data['program'] = gv_json_config['rgb_programs'][rgb_program]
                json_req = {}
                json_req['controls'] = []
                json_req['controls'].append(control_data)
                json_data = post_url(url + '/control', 
                                     json_req,
                                     gv_http_timeout_secs)
                if (json_data):
                    track_device_status(device_name, url, json_data)

            else:
                log_message("program not found")

    elif (zone and control_name and rgb_program):
        if ('rgb_programs' in gv_json_config and 
            rgb_program in gv_json_config['rgb_programs']):

            # Check all devices
            for device_name in gv_device_dict:
                json_data = gv_device_dict[device_name]['status']

                if ('zone' in json_data and 
                        json_data['zone'] == zone and
                        'controls' in json_data):
                    for control in json_data['controls']:
                        if control['name'] == control_name:
                            url = gv_device_dict[device_name]['url']

                            log_message("Manually setting (%s) %s/%s/%s to rgb_program:%s" % (
                                url,
                                device_name,
                                zone,
                                control_name,
                                rgb_program))

                            control_data = {}
                            control_data['name'] = control_name
                            control_data['program'] = gv_json_config['rgb_programs'][rgb_program]
                            json_req = {}
                            json_req['controls'] = []
                            json_req['controls'].append(control_data)
                            json_data = post_url(url + '/control', 
                                                 json_req,
                                                 gv_http_timeout_secs)
                            if (json_data):
                                track_device_status(device_name, url, json_data)

        else:
            log_message("program not found")

    elif (device_name and zone and control_name and argb_program):

        if device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message("Manually setting %s/%s/%s to argb_program:%s" % (
                device_name,
                zone,
                control_name,
                argb_program))

            if ('argb_programs' in gv_json_config and 
                    argb_program in gv_json_config['argb_programs']):

                control_data = {}
                control_data['name'] = control_name
                control_data['program'] = gv_json_config['argb_programs'][argb_program]
                json_req = {}
                json_req['controls'] = []
                json_req['controls'].append(control_data)
                json_data = post_url(url + '/control', 
                                     json_req,
                                     gv_http_timeout_secs)
                if (json_data):
                    track_device_status(device_name, url, json_data)

            else:
                log_message("program not found")

    elif (zone and control_name and argb_program):
        if ('argb_programs' in gv_json_config and 
            argb_program in gv_json_config['argb_programs']):

            # Check all devices
            for device_name in gv_device_dict:
                json_data = gv_device_dict[device_name]['status']

                if ('zone' in json_data and 
                        json_data['zone'] == zone and
                        'controls' in json_data):
                    for control in json_data['controls']:
                        if control['name'] == control_name:
                            url = gv_device_dict[device_name]['url']

                            log_message("Manually setting (%s) %s/%s/%s to argb_program:%s" % (
                                url,
                                device_name,
                                zone,
                                control_name,
                                argb_program))

                            control_data = {}
                            control_data['name'] = control_name
                            control_data['program'] = gv_json_config['argb_programs'][argb_program]
                            json_req = {}
                            json_req['controls'] = []
                            json_req['controls'].append(control_data)
                            json_data = post_url(url + '/control', 
                                                 json_req,
                                                 gv_http_timeout_secs)
                            if (json_data):
                                track_device_status(device_name, url, json_data)

        else:
            log_message("program not found")

    # Bulk stuff
    for url in command_url_list:
        log_message("Issuing command url:%s" % (
            url))

        # Not going to track response data
        # for bulk operations
        get_url(url, gv_http_timeout_secs, 1)

    return 


#This class will handle any incoming request from
#the browser or devices
class web_console_zone_handler(object):
    @cherrypy.expose()

    def index(self):

        log_message("client:%s:%d params:%s" % (
            cherrypy.request.remote.ip,
            cherrypy.request.remote.port,
            cherrypy.request.params))

        return build_zone_web_page()

    # Force trailling slash off on called URL
    index._cp_config = {'tools.trailing_slash.on': False}


class web_console_device_handler(object):
    @cherrypy.expose()

    def index(self):

        log_message("device client:%s:%d params:%s" % (
            cherrypy.request.remote.ip,
            cherrypy.request.remote.port,
            cherrypy.request.params))

        return build_device_web_page()

    # Force trailling slash off on called URL
    index._cp_config = {'tools.trailing_slash.on': False}


class web_console_api_handler(object):
    @cherrypy.expose()

    def index(self, 
              device=None, 
              zone=None, 
              control=None, 
              state=None, 
              rgb_program=None, 
              argb_program=None, 
              reboot=None,
              reconfig=None,
              update=None,
              apmode=None):

        log_message("json client:%s:%d params:%s" % (
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
                               rgb_program,
                               argb_program)

        # Return nothing
        return ""

    # Force trailling slash off on called URL
    index._cp_config = {'tools.trailing_slash.on': False}


def web_server():

    log_message("Starting console web server on port %d" % (
        gv_json_config['web']['port']))
    # Disable logging
    if not gv_json_config['web']['logging']:
        cherrypy.config.update({'environment': 'production',
                                'log.screen': False,
                                'log.access_file': '',
                                'log.error_file': ''})

    # Listen on our port on any IF
    cherrypy.server.socket_host = '0.0.0.0'
    cherrypy.server.socket_port = gv_json_config['web']['port']

    # SSL
    if 'ssl' in gv_json_config['web']:
        cherrypy.server.ssl_module = 'pyopenssl'
        cherrypy.server.ssl_certificate = gv_json_config['web']['ssl']['cert']
        cherrypy.server.ssl_private_key = gv_json_config['web']['ssl']['key']

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
        log_message("No users provisioned in config.. bypassing authentation")
        conf = {}

    # Set webhooks for zone and device
    cherrypy.tree.mount(web_console_zone_handler(), '/', conf)
    cherrypy.tree.mount(web_console_zone_handler(), '/zone', conf)
    cherrypy.tree.mount(web_console_device_handler(), '/device', conf)

    # webhook for API
    cherrypy.tree.mount(web_console_api_handler(), '/api')

    # Cherrypy main loop
    cherrypy.engine.start()
    cherrypy.engine.block()

# main()
gv_startup_time = time.asctime()

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
         if (not thread.is_alive()):
             dead_threads += 1

    if (dead_threads > 0):
        log_message("Detected %d dead threads.. exiting" % (dead_threads))
        sys.exit(-1);

    time.sleep(5)
