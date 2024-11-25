
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
import os
import sys
import copy
from dateutil import tz
from zeroconf import ServiceBrowser, Zeroconf
import requests
import argparse
import concurrent.futures
import cherrypy

# Config
gv_home_dir = os.path.expanduser('~')
gv_config_file = gv_home_dir + '/.jbhasd_web_server'
gv_json_config = {}

def log_message(
        verbose,
        message):

    if verbose:
        print(
                "%s %s" % (
                    time.asctime(),
                    message
                    )
                )
        sys.stdout.flush()

    return


def set_default_config():
    global gv_config_file

    log_message(
            1,
            "Setting config defaults")
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

    log_message(
            1,
            "Loading config from %s" % (config_file))
    try:
        config_data = open(config_file).read()
        json_config = json.loads(config_data)
    except Exception as ex: 
        log_message(
                1,
                "load config failed: %s" % (ex))
        json_config = None

    return json_config


def save_config(json_config, config_file):
    log_message(
            1,
            "Saving config to %s" % (config_file))
    with open(gv_config_file, 'w') as outfile:
        indented_json_str = json.dumps(json_config, 
                                       indent=4, 
                                       sort_keys=True)
        outfile.write(indented_json_str)
        outfile.close()


def config_agent():
    global gv_json_config
    global gv_last_sunset_check
    global gv_sunset_time
    global gv_sunrise_time
    global gv_actual_sunset_time 
    global gv_actual_sunrise_time 
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


        # Sunset calculations
        # refresh this based on second interval
        # gv_json_config['sunset']['refresh']
        now = int(time.time())
        if ((now - gv_last_sunset_check) >= 
                gv_json_config['sunset']['refresh']):
            # Re-calculate
            log_message(
                    1,
                    "Refreshing Sunset times (every %d seconds).." % (
                        gv_json_config['sunset']['refresh']
                        )
                    )
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

                log_message(
                        1,
                        "Sunset time is %04d (with offset of %d seconds)" % (
                            gv_sunset_time,
                            gv_json_config['sunset']['offset']
                            )
                        )

                log_message(
                        1,
                        "Sunrise time is %04d (with offset of %d seconds)" % (
                            gv_sunrise_time,
                            gv_json_config['sunset']['offset']
                            )
                        )

            gv_last_sunset_check = now

        # standard config loop period
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

    #log_message(1, '%s -> %d' % (timer_time, time_ival))
    return time_ival

# Sunset globals
gv_last_sunset_check = 0
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

    log_message(
            1,
            "Resetting all device dictionaries")

    gv_device_dict = {}
    return


def purge_device(device_name, reason):
    # wipe single device from dicts etc
    global gv_device_dict

    log_message(
            1,
            "Purging Device:%s reason:%s" % (
                device_name, 
                reason
                )
            )

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
        device['failed_probes'] = 0
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

    log_message(
            1,
            'check_control(device=%s, zone=%s, control=%s' % (
                device_name,
                zone_name,
                control_name)
            )

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
                log_message(
                        1,
                        'Event:%s ev_rel:%d now_rel:%d threshold:%d last_programmed_interval:%d' % (
                            event_time,
                            event_time_rel_secs,
                            current_time_rel_secs,
                            program_threshold,
                            last_program_interval
                            )
                        )

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
                                log_message(
                                        1,
                                        'Substituted referenced RGB program %s' % (program_name
                                                                                   )
                                        )
                            elif program_name in gv_json_config['argb_programs']:
                                control_data['program'] = gv_json_config['argb_programs'][program_name]
                                log_message(
                                        1,
                                        'Substituted referenced ARGB program %s' % (program_name
                                                                                    )
                                        )

                        log_message(
                                1,
                                'Returning control data.. %s' % (control_data))
                        return control_data, event_time
                    else:
                        log_message(
                                1,
                                'Already programmed %d seconds ago' % (
                            last_program_interval))


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
                log_message(
                        1,
                        'Returning paired switch control data.. %s:%s -> %s:%s .. %s' % (
                            paired_switch['a_zone'], 
                            paired_switch['a_control'],
                            paired_switch['b_zone'], 
                            paired_switch['b_control'],
                            control_data
                            )
                        )
                return control_data, None

    # fall-through nothing to do
    return None, None


class ZeroConfListener(object):  
    def remove_service(self, zeroconf, type, name):
        # Can ignore this as we will self-purge
        # unresponsive devices
        return

    def update_service(self, zeroconf, type, name):
        # treat update as add
        self.add_service(zeroconf, type, name)
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
                log_message(
                        1,
                        "Discovered %s (%s)" % (
                            device_name,
                            url
                            )
                        )

                # register empty device in global device dict
                device = {}
                device['name'] = device_name
                device['url'] = url
                device['failed_probes'] = 0
                device['status'] = {}
                device['status']['name'] = device_name
                device['status']['zone'] = 'Unknown'
                device['status']['controls'] = []
                device['last_updated'] = now
                device['program_reg'] = {}
                gv_device_dict[device_name] = device


        return


def discovery_agent():

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
        log_message(
                1,
                "Error in GET Name:%s URL:%s" % (
            url_name, 
            url
            )
                )

    if response:
        response_str = response.text

        if parse_json:
            try:
                json_data = response.json()
            except:
                log_message(
                        1,
                        "Error in JSON parse.. Name:%s URL:%s Data:%s" % (
                            url_name, 
                            url, 
                            response_str
                            )
                        )
                return None
            return json_data

    return response_str
     

def post_url(url, json_data, url_timeout):
    # General purpose URL POSTer
    # for JSON payloads
    # return contents parsed as json

    log_message(
            1,
            "POST %s \n%s\n" % (url, json_data))

    response = None
    try:
        response = requests.post(url,
                                 json = json_data,
                                 timeout = url_timeout)
    except:
        log_message(
                1,
                "Error in POST URL:%s" % (url))

    if response:
        try:
            json_data = response.json()
        except:
            log_message(
                    1,
                    "Error in JSON parse.. URL:%s Data:%s" % (
                        url, 
                        response.text
                        )
                    )
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
                log_message(
                        1,
                        "Automatically setting %s/%s to %s" % (
                            zone_name,
                            control_name,
                            control_data
                            )
                        )

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

    log_message(
            1,
            "Configure device %s" % (device_name))
    if (device_name in gv_json_config['devices']): 
        # matched to stored profile
        log_message(
                1,
                "Matched device %s to stored profile.. configuring" % (device_name
                                                                       )
                )
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
        log_message(
                1,
                "Sending config (%d bytes):\n%s\n" % (
                    len(device_config),
                    device_config
                    )
                )

        # POST to /configure function of URL
        requests.post(url + '/configure', 
                      json = config_dict, 
                      timeout = gv_http_timeout_secs)

    else:
        log_message(
                1,
                "ERROR %s not found in device config" % (device_name))

    return


def probe_agent():
    # iterate set of discovered device URLs
    # and probe their status values, storing in a dictionary
    global gv_json_config
    global gv_device_dict

    # loop forever
    while (1):
        # iterate set of discovered device URLs as snapshot list
        # avoids issues if the set is updated mid-way
        successful_probes = 0
        failed_probes = 0
        purged_urls = 0
        control_changes = 0
        for device_name in list(gv_device_dict.keys()):

            if not device_name in gv_device_dict:
                continue

            device = gv_device_dict[device_name]
            now = int(time.time())

            # skip any devices recently probed
            if now - device['last_updated'] < gv_json_config['discovery']['device_probe_interval']:
                continue

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
                device['failed_probes'] += 1
                log_message(
                        1,
                        'Failed to probe: %s .. response:%s' % (
                            url,
                            json_data
                            )
                        )
        
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

        log_message(
                1,
                "Probe.. successful:%d failed:%d purged:%d" % (
                    successful_probes,
                    failed_probes,
                    purged_devices
                    )
                )

        # loop sleep interval
        time.sleep(2)

    return


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
            log_message(
                    1,
                    "Rebooting all devices")
            reboot_all = 1
            for device_name in gv_device_dict:
                url = gv_device_dict[device_name]['url']

                log_message(
                        1,
                        "Rebooting %s" % (device_name))

                command_url_list.append('%s/reboot' % (url))
            purge_all_devices()

        elif device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message(
                    1,
                    "Rebooting %s" % (device_name))

            command_url_list.append('%s/reboot' % (url))
            
    elif (device_name and reconfig):

        if device_name == 'all':
            log_message(
                    1,
                    "Reconfiguring all devices")
            reconfig_all = 1
            for device_name in gv_device_dict:
                url = gv_device_dict[device_name]['url']

                log_message(
                        1,
                        "Reconfiguring %s" % (device_name))

                command_url_list.append('%s/reconfigure' % (url))

            # Dont purge devices here as the probe stage will
            # invoke the reconfigure

        elif device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message(
                    1,
                    "Reconfiguring %s" % (device_name))

            command_url_list.append('%s/reconfigure' % (url))

            # Dont purge device_name here as the probe stage will
            # invoke the reconfigure

    elif (device_name and apmode):

        if device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message(
                    1,
                    "Rebooting %s into AP Mode" % (device_name))

            command_url_list.append('%s/apmode' % (url))

    elif (device_name and zone and control_name and state):

        if device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message(
                    1,
                    "Manually setting %s/%s/%s to state:%s" % (
                        device_name,
                        zone,
                        control_name,
                        state
                        )
                    )

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

                        log_message(
                                1,
                                "Manually setting (%s) %s/%s/%s to state:%s" % (
                                    url,
                                    device_name,
                                    zone,
                                    control_name,
                                    state
                                    )
                                )

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

            log_message(
                    1,
                    "Manually setting %s/%s/%s to rgb_program:%s" % (
                        device_name,
                        zone,
                        control_name,
                        rgb_program
                        )
                    )

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
                log_message(
                        1,
                        "program not found")

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

                            log_message(
                                    1,
                                    "Manually setting (%s) %s/%s/%s to rgb_program:%s" % (
                                        url,
                                        device_name,
                                        zone,
                                        control_name,
                                        rgb_program
                                        )
                                    )

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
            log_message(
                    1,
                    "program not found")

    elif (device_name and zone and control_name and argb_program):

        if device_name in gv_device_dict:
            url = gv_device_dict[device_name]['url']

            log_message(
                    1,
                    "Manually setting %s/%s/%s to argb_program:%s" % (
                        device_name,
                        zone,
                        control_name,
                        argb_program
                        )
                    )

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
                log_message(
                        1,
                        "program not found")

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

                            log_message(
                                    1,
                                    "Manually setting (%s) %s/%s/%s to argb_program:%s" % (
                                        url,
                                        device_name,
                                        zone,
                                        control_name,
                                        argb_program
                                        )
                                    )

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
            log_message(
                    1,
                    "program not found")

    # Bulk stuff
    for url in command_url_list:
        log_message(
                1,
                "Issuing command url:%s" % (
            url))

        # Not going to track response data
        # for bulk operations
        get_url(url, gv_http_timeout_secs, 1)

    return 


class web_console_data_handler(object):
    @cherrypy.expose()

    def index(self):
        global gv_startup_time
        global gv_actual_sunrise_time
        global gv_actual_sunset_time

        log_message(
                1,
                "json client:%s:%d params:%s" % (
                    cherrypy.request.remote.ip,
                    cherrypy.request.remote.port,
                    cherrypy.request.params
                    )
                )

        data_dict = {}
        data_dict['devices'] = gv_device_dict
        data_dict['rgb_programs'] = list(gv_json_config['rgb_programs'].keys())
        data_dict['argb_programs'] = list(gv_json_config['argb_programs'].keys())
        data_dict['system'] = {}
        data_dict['system']['startup_time'] = gv_startup_time
        data_dict['system']['sunrise_time'] = gv_actual_sunrise_time
        data_dict['system']['sunset_time'] = gv_actual_sunset_time
        data_dict['system']['sunset_offset'] = gv_json_config['sunset']['offset']

        return json.dumps(data_dict, indent = 4)

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

        log_message(
                1,
                "json client:%s:%d params:%s" % (
                    cherrypy.request.remote.ip,
                    cherrypy.request.remote.port,
                    cherrypy.request.params
                    )
                )

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


def web_server(dev_mode):

    log_message(
            1,
            'Starting web server.. port:%d dev_mode:%s' % (
                gv_json_config['web']['port'],
                dev_mode)
            )

    # engine config for production
    # lockds down exception logging from web I/F
    if not dev_mode:
        cherrypy.config.update(
                {
                    'environment': 'production',
                    'log.screen': False,
                    'log.access_file': '',
                    'log.error_file': ''
                    }
                )

    # Listen on our port on any IF
    cherrypy.server.socket_host = '0.0.0.0'
    cherrypy.server.socket_port = gv_json_config['web']['port']

    # SSL
    if 'ssl' in gv_json_config['web']:
        cherrypy.server.ssl_module = 'pyopenssl'
        cherrypy.server.ssl_certificate = gv_json_config['web']['ssl']['cert']
        cherrypy.server.ssl_private_key = gv_json_config['web']['ssl']['key']

    # www dir and related static config for hosting
    www_dir = '%s/www' % (
            os.path.dirname(os.path.realpath(__file__))
            )

    # static hosting from www dir and index file 
    # is dash.html
    static_conf = {
            '/': {
                'tools.staticdir.on': True,
                'tools.staticdir.dir': www_dir,
                'tools.staticdir.index': 'dash.html',
                }
            }

    # API config
    # none by default
    api_conf = {}

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

        # merge in digest settings for static and 
        # api access
        static_conf['/'].update(digest_conf)
        api_conf['/'] = digest_conf

    else:
        log_message(
                1,
                "No users provisioned in config.. bypassing authentation")

    cherrypy.tree.mount(None, '/', static_conf)

    # webhook for status data
    cherrypy.tree.mount(web_console_data_handler(), '/data', api_conf)

    # webhook for action API
    cherrypy.tree.mount(web_console_api_handler(), '/api', api_conf)

    # Cherrypy main loop
    cherrypy.engine.start()
    cherrypy.engine.block()


def thread_exception_wrapper(
        fn, 
        *args, 
        **kwargs):

    try:
        # call fn arg with other args
        return fn(*args, **kwargs)

    except Exception:
        # generate a formal backtrace as a string
        # and raise this as a new exception with that string as title
        exception_detail = sys.exc_info()
        exception_list = traceback.format_exception(
                *exception_detail,
                limit = 200)
        exception_str = ''.join(exception_list)

        raise Exception(exception_str)  


# main()
gv_startup_time = time.asctime()

parser = argparse.ArgumentParser(
        description = 'JBHASD Web Server'
        )

parser.add_argument(
        '--dev', 
        help = 'Enable Development mode', 
        action = 'store_true'
        )

args = vars(parser.parse_args())
dev_mode = args['dev']

# Thread management 
executor = concurrent.futures.ThreadPoolExecutor(
        max_workers = 20)
future_dict = {}

future_dict['Config Agent'] = executor.submit(
        thread_exception_wrapper,
        config_agent)

# Allow some grace for config to load
time.sleep(5)

# device discovery thread
future_dict['Discovery Agent'] = executor.submit(
        thread_exception_wrapper,
        discovery_agent)

# device probe thread
future_dict['Status Probe Agent'] = executor.submit(
        thread_exception_wrapper,
        probe_agent)

# web server thread
future_dict['Web Server'] = executor.submit(
        thread_exception_wrapper,
        web_server,
        dev_mode)

# main loop
while (True):
    exception_dict = {}
    for key in future_dict:
        future = future_dict[key]
        if future.done():
            if future.exception():
                exception_dict[key] = future.exception()

    if (len(exception_dict) > 0):
        log_message(
                1,
                'Exceptions Detected:\n%s' % (
                    exception_dict)
                )
        os._exit(1) 
        
    time.sleep(5)
