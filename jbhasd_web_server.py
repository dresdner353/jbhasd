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
import sys
import copy
from dateutil import tz
from zeroconf import ServiceBrowser, Zeroconf
import cherrypy

### Begin Web page template
# Templated web page as the header section with CSS and jquery generated
# code
# The body section is a single div with the generated dashboard HTML
web_page_template = """
<head>
    <title>JBHASD Console</title>
    <meta id="META" name="viewport" content="width=device-width; initial-scale=1.0" >
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
            $.get("/?width=" + window.innerWidth, function(data, status){
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
# switch checkbox. The action gets a given URL and replaces the 
# dashboard content with the result
# Its a tidier alternative to page refreshes
switch_click_template = """
        $("#__ID__").click(function(){
                $.get("__URL__&width=" + window.innerWidth, function(data, status){
                // clear refresh timer before reload
                clearInterval(refresh_timer);
                $("#dashboard").html(data);
            });
        });
"""
### End switch on click template


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


# Discovery and probing of devices
gv_zeroconf_refresh_interval = 60
gv_probe_refresh_interval = 10
gv_device_purge_timeout = 60
gv_web_port = 8080
gv_web_ip = '127.0.0.1' # will be updated with get_ip() call

# Sunset config
# set with co-ords of Dublin Spire, Ireland
gv_sunset_url = 'http://api.sunrise-sunset.org/json?lat=53.349809&lng=-6.2624431&formatted=0'
gv_last_sunset_check = -1
gv_sunset_lights_on_offset = -3600
gv_sunset_on_time = "2000" # noddy default

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

# Long poll timestamp
# this will set to current epoch
# everytime we perform a full probe
# or any time we get a push
gv_poll_timestamp = 0

# presentation settings on dashboard
gv_dashbox_width = 210
gv_initial_num_cols = 1
gv_col_division_offset = 20

# Zone Switchname
gv_switch_tlist = [
#   Zone                 Switch              On          Off       Override
    ("Livingroom",       "Uplighter",        "sunset",   "0100",   "1200" ),
    ("Playroom",         "Uplighter",        "sunset",   "0100",   "1200" ),
    ("Kitchen",          "Counter Lights",   "sunset",   "0100",   "1200" ),

    ("Attic Prototype",  "Sonoff Switch",    "1200",     "1205",   "2359" ),
    ("Attic Prototype",  "Sonoff Switch",    "1230",     "1232",   "1200" ),
    ("Attic Prototype",  "Sonoff Switch",    "1330",     "1400",   "2359" ),
    ("Attic Prototype",  "Sonoff Switch",    "1500",     "1501",   "2359" ),

    ("Attic Sonoff",     "Socket A",         "1120",     "1150",   "2359" ),
    ("Attic Sonoff",     "Green LED A",      "1125",     "1145",   "2359" ),

    ("Attic Sonoff",     "Socket B",         "1500",     "1600",   "1200" ),
    ("Attic Sonoff",     "Green LED B",      "1505",     "1510",   "1200" ),
]

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

    global gv_sunset_on_time

    # represents state of switch
    # -1 do nothing.. not matched
    # 0 off
    # 1 on
    desired_state = -1

    for zone, switch, on_time, off_time, override_time in gv_switch_tlist:
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
                on_time = gv_sunset_on_time
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
        # extract dns-sd info, build URL
        # and store in set
        info = zeroconf.get_service_info(type, name)
        if info is not None:
            address = socket.inet_ntoa(info.address)
            port = info.port
            url = "http://%s:%d/json" % (address, port)
            if not url in gv_jbhasd_zconf_url_set:
                gv_jbhasd_zconf_url_set.add(url)
                print("%s Discovered device..\n  name:%s \n  URL:%s" % (time.asctime(),
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
        time.sleep(gv_zeroconf_refresh_interval)
        zeroconf.close()


def fetch_url(url, url_timeout, parse_json):
    # General purpoe URL fetcher
    # return contents of page and parsed as json
    # if the parse_json arg is 1
    response_str = None

    #print("%s Fetching URL:%s, timeout:%d" % (time.asctime(), url, url_timeout)) 

    response = None
    try:
        response = urllib.request.urlopen(url,
                                          timeout = url_timeout)
    except:
        print("%s Error in urlopen URL:%s" % (time.asctime(), url))
 
    if response is not None:
        try:
            response_str = response.read()
        except:
            print("%s Error in response.read() URL:%s" % (time.asctime(), url))
            return None

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
    # iterate set of discovered device URLs
    # and probe their status values, storing in a dictionary
    # Also calculate sunset time every 6 hours as part of automated 
    # management of devices
    global gv_last_sunset_check, gv_sunset_lights_on_offset, gv_sunset_on_time, gv_poll_timestamp

    # loop forever
    while (1):
        # Sunset calculations
        now = time.time()
        if ((now - gv_last_sunset_check) >= 6*60*60): # every 6 hours
            # Re-calculate
            print("%s Getting Sunset times.." % (time.asctime()))
            json_data = fetch_url(gv_sunset_url, 20, 1)
            if json_data is not None:
                sunset_str = json_data['results']['sunset']
                sunset_ts = sunset_api_time_to_epoch(sunset_str)
                sunset_local_time = time.localtime(sunset_ts + gv_sunset_lights_on_offset)
                gv_sunset_on_time = int(time.strftime("%H%M", sunset_local_time))
                print("%s Sunset on-time is %s (with offset of %d seconds)" % (time.asctime(),
                                                                               gv_sunset_on_time,
                                                                               gv_sunset_lights_on_offset))

            gv_last_sunset_check = now

        # iterate set of discovered device URLs as snapshot list
        # avoids issues if the set is updated mid-way
        device_url_list = list(gv_jbhasd_zconf_url_set)
        gv_web_ip = get_ip()
        successful_probes = 0
        failed_probes = 0
        purged_urls = 0
        control_changes = 0
        gv_web_ip_safe = urllib.parse.quote_plus(gv_web_ip)
        for url in device_url_list:
            url_w_update = '%s?update_ip=%s&update_port=%d' % (url,
                                                               gv_web_ip_safe, 
                                                               gv_web_port)
            json_data = fetch_url(url_w_update, gv_http_timeout_secs, 1)
            if (json_data is not None):
                device_name = json_data['name']
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
                print("Failed to get status on %s" % (url))
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
            if last_updated >= gv_device_purge_timeout:
                purged_urls += 1
                reason = "expired last updated %d seconds ago" % (last_updated)
                print("%s Purging Device:%s URL:%s.. reason:%s" % (time.asctime(),
                                                                   device_name,
                                                                   url,
                                                                   reason))
                del gv_jbhasd_device_url_dict[device_name]
                del gv_jbhasd_device_ts_dict[device_name]
                del gv_jbhasd_device_status_dict[device_name]
                gv_jbhasd_zconf_url_set.remove(url)

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
                    print("%s Automatically setting %s/%s to state:%s" % (time.asctime(),
                                                                          zone_name,
                                                                          control_name,
                                                                          desired_state))
                    control_safe = urllib.parse.quote_plus(control_name)
                    command_url = '%s?control=%s&state=%s' % (url,
                                                              control_safe,
                                                              desired_state)
                    print("%s Issuing command url:%s" % (time.asctime(),
                                                         command_url))

                    json_data = fetch_url(command_url, gv_http_timeout_secs, 1)
                    if (json_data is not None):
                        gv_jbhasd_device_status_dict[device_name] = json_data
                        gv_jbhasd_device_ts_dict[device_name] = int(time.time())

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

        # update poll timestamp to drive 
        # long poll reaction
        print("%s Probe.. successful:%d failed:%d "
              "changed:%d purged:%d" % (time.asctime(),
                                        successful_probes,
                                        failed_probes,
                                        control_changes,
                                        purged_urls))
        # treat detected control changes or purges as 
        # a cue to triggering poll requests
        if (control_changes > 0 or 
            purged_urls > 0):
            gv_poll_timestamp = time.time()

        # loop sleep interval
        time.sleep(gv_probe_refresh_interval)
    return


def build_web_page(num_cols):

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
        device_size = len(json_data['controls']) + len(json_data['sensors'])

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
                                          '<table border="0" padding="3" width="100%%">') % (zone)

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
                    href_url = ('/?device=%s'
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
                                                      '<input type="checkbox" id="switch%d" %s>'
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
                    switch_str = switch_click_template
                    jquery_click_id = 'switch%d' % (switch_id)
                    switch_str = switch_str.replace("__ID__", jquery_click_id)
                    switch_str = switch_str.replace("__URL__", href_url)
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
                for sensor in json_data['sensors']:
                    sensor_name = sensor['name']
                    sensor_type = sensor['type']

                    if (sensor_type == 'temp/humidity'):
                        temp = sensor['temp']
                        humidity = sensor['humidity']

                        # &#x1f321 thermometer temp
                        # &#x1f322 droplet humidity
                        dashboard_col_list[col_index] += ('<tr>'
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

        # terminate the zone table and container div
        dashboard_col_list[col_index] += '</table></div>'

    # Build the dashboard portion
    # It's the timestamp
    # and then a single row table, one cell
    # per vertical column
    dashboard_str = '<div class="timestamp" align="right">Updated %s</div>' % (time.asctime())
    dashboard_str += '<table border="0" width="100%%"><tr>'
    col_width = int(100 / num_cols)
    for col_str in dashboard_col_list:
        dashboard_str += '<td valign="top" width="%d%%">' % (col_width)
        dashboard_str += col_str
        dashboard_str += '</td>'
    dashboard_str += '</tr></table>'

    # Build and return the web page
    # dropping in CSS, generated jquery code
    # and dashboard.
    web_page_str = web_page_template
    web_page_str = web_page_str.replace("__CSS__", web_page_css)
    web_page_str = web_page_str.replace("__DASHBOX_WIDTH__", str(gv_dashbox_width))
    web_page_str = web_page_str.replace("__SWITCH_FUNCTIONS__", jquery_str)
    web_page_str = web_page_str.replace("__DASHBOARD__", dashboard_str)
    web_page_str = web_page_str.replace("__RELOAD__", str(gv_probe_refresh_interval * 1000))

    return web_page_str


def process_console_action(device, zone, control, state):

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

            # Format URL and pass control name through quoting function
            # Will handle any special character formatting for spaces
            # etc
            control_safe = urllib.parse.quote_plus(control)
            command_url = '%s?control=%s&state=%s' % (url,
                                                      control_safe,
                                                      state)
            print("%s Issuing command url:%s" % (time.asctime(),
                                                 command_url))

            json_data = fetch_url(command_url, gv_http_timeout_secs, 1)
            if (json_data is not None):
                # update the status and ts as returned
                gv_jbhasd_device_status_dict[device] = json_data
                gv_jbhasd_device_ts_dict[device] = int(time.time())
    return


def process_device_update(update):
    if (update is not None):
        device_name = update
        if device_name in gv_jbhasd_device_url_dict:
            url = gv_jbhasd_device_url_dict[device_name]
            json_data = fetch_url(url, gv_http_timeout_secs, 1)
            if (json_data is not None):
                #print("Updated status for %s URL:%s" % (device_name, url))
                device_name = json_data['name']
                gv_jbhasd_device_url_dict[device_name] = url
                gv_jbhasd_device_status_dict[device_name] = json_data
                gv_jbhasd_device_ts_dict[device_name] = int(time.time())
        else:
            print("%s Cant match push for %s to URL" % (time.asctime(),
                                                        device_name))
 
    return

#This class will handle any incoming request from
#the browser or devices
class web_console_handler(object):
    @cherrypy.expose()

    def index(self, update=None, device=None, zone=None, control=None, state=None, poll=None, width=None):
        global gv_poll_timestamp

        # set defautl cols
        # Then calculate more accurate version based on 
        # supplied window width divided by dashbox width
        # plus an offset for padding consideration
        num_cols = gv_initial_num_cols
        if width is not None:
            num_cols = int(int(width) / (gv_dashbox_width + 
                                         gv_col_division_offset))

        print("%s client:%s:%d params:%s" % (time.asctime(),
                                             cherrypy.request.remote.ip,
                                             cherrypy.request.remote.port,
                                             cherrypy.request.params))
        if update is not None:
            process_device_update(update)
            gv_poll_timestamp = time.time()
            return "Thank You"

        else:
            # normal page fetch incl poll mode
            # take a snap of this timesamp 
            # first
            poll_snapshot = gv_poll_timestamp

            # process actions if present
            process_console_action(device, zone, control, state)

            # if we're in poll mode
            # we need to stall until there is a 
            # change
            if poll is not None:
                # take a snapshot of the current
                # poll timestamp and loop until
                # it changes. Then return the page
                while (poll_snapshot == gv_poll_timestamp):
                    print("poll wait.. %s:%d" % (cherrypy.request.remote.ip,
                                                 cherrypy.request.remote.port))
                    time.sleep(1)

            # return dashboard in specified number of 
            # columns
            return build_web_page(num_cols)

    # Force trailling slash off on called URL
    index._cp_config = {'tools.trailing_slash.on': False}


def web_server():
    global gv_poll_timestamp

    # init poll timestamp
    gv_poll_timestamp = time.time()

    print("Starting console web server on port %d" % (gv_web_port))
    # Logging off
    cherrypy.config.update({'log.screen': False,
                            'log.access_file': '',
                            'log.error_file': ''})

    # Listen on our port on any IF
    cherrypy.server.socket_host = '0.0.0.0'
    cherrypy.server.socket_port = gv_web_port

    # Cherrypy main loop
    cherrypy.quickstart(web_console_handler())

# main()

# Analytics
analytics_file = open("analytics.csv", "a")

thread_list = []

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

#web_poll_server_t = threading.Thread(target = web_poll_server)
#web_poll_server_t.daemon = True
#web_poll_server_t.start()
#thread_list.append(web_poll_server_t)

while (1):
    dead_threads = 0
    for thread in thread_list:
         if (not thread.isAlive()):
             dead_threads += 1

    if (dead_threads > 0):
        print("Detected %d dead threads.. exiting" % (dead_threads))
        sys.exit(-1);

    time.sleep(5)
