import logging
import socket
import sys
import os
import time
import threading
import cherrypy
import urllib
import urllib.parse
import urllib.request
import urllib.error
import json
import random

from zeroconf import ServiceInfo, Zeroconf

json_status_tmpl = '{ "name": "__DEVICE_NAME__", "zone": "__ZONE__", "ota_enabled" : 1, "telnet_enabled" : 1, "manual_switches_enabled" : 1, "temp_offset" : "0", "ssid" : "cormac-L", "profile" : "Sonoff S20", "controls": [], "sensors": [] }'

us_states_list = ['Alabama', 'Alaska', 'Arizona', 'Arkansas', 'California', 'Colorado', 'Connecticut', 'Delaware', 'Florida', 'Georgia', 'Hawaii', 'Idaho', 'Illinois', 'Indiana', 'Iowa', 'Kansas', 'Kentucky', 'Louisiana', 'Maine', 'Maryland', 'Massachusetts', 'Michigan', 'Minnesota', 'Mississippi', 'Missouri', 'Montana', 'Nebraska', 'Nevada', 'New Hampshire', 'New Jersey', 'New Mexico', 'New York', 'North Carolina', 'North Dakota', 'Ohio', 'Oklahoma', 'Oregon', 'Pennsylvania', 'Rhode Island', 'South Carolina', 'South Dakota', 'Tennessee', 'Texas', 'Utah', 'Vermont', 'Virginia', 'Washington', 'West Virginia', 'Wisconsin', 'Wyoming', 'District of Columbia', 'Puerto Rico', 'Guam', 'American Samoa', 'U.S. Virgin Islands', 'Northern Mariana Islands']

irish_counties_list = ['Antrim', 'Armagh', 'Carlow', 'Cavan', 'Clare', 'Cork', 'Derry', 'Donegal', 'Down', 'Dublin', 'Fermanagh', 'Galway', 'Kerry', 'Kildare', 'Kilkenny', 'Laois', 'Leitrim', 'Limerick', 'Longford', 'Louth', 'Mayo', 'Meath', 'Monaghan', 'Offaly', 'Roscommon', 'Sligo', 'Tipperary', 'Tyrone', 'Waterford', 'Westmeath', 'Wexford', 'Wicklow']

# dictionary of simulated json status dicts
# indexed on port value
json_status_dict = {}

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


class device_web_server(object):
    @cherrypy.expose()

    def index(self, update_ip=None, update_port=None, control=None, state=None):
        print("Called %s" % (cherrypy.url()))

        # determine port of called URL 
        parsed_url = urllib.parse.urlparse(cherrypy.url())
        url_port = parsed_url.port
        json_data = json_status_dict[url_port]

        if (control is not None and 
            state is not None):
            for json_control in json_data['controls']:
                if json_control['name'] == control:
                    json_control['state'] = state

        return json.dumps(json_data)

    # Force trailling slash off on called URL
    index._cp_config = {'tools.trailing_slash.on': False}

def change_controls():
    # Randomise changes in the devices

    while(1):
        ports_list = list(json_status_dict)
        num_ports = len(ports_list)

        num_devices_to_change = (random.randint(1, 10000000) % num_ports) + 1
        print("Changing controls for %d devices" % (num_devices_to_change))
        for i in range(0, num_devices_to_change):
            port_index = random.randint(1, 10000000) % num_ports
            port = ports_list[port_index]
            device_json = json_status_dict[port]
            num_controls_in_device = len(device_json['controls'])
            num_controls_to_change = (random.randint(1, 10000000) % num_controls_in_device) + 1
            print("Changing %d controls for %s" % (num_controls_to_change, 
                                                   device_json['name']))
            for j in range(0, num_controls_to_change):
                control_index = random.randint(1, 10000000) % num_controls_in_device
                control_state = random.randint(0, 100) % 2
                print("Changing control %s state to %d" % (device_json['controls'][control_index]['name'], 
                                                           control_state))
                device_json['controls'][control_index]['state'] = control_state

        time.sleep(5)


# main

zeroconf = Zeroconf()
my_ip = get_ip()

# more involved start of cherrypy as we 
# want to have multiple separate ports, one per device
cherrypy.tree.mount(device_web_server(), '/json')
cherrypy.server.unsubscribe()
server_list = []
num_states = len(us_states_list)
num_counties = len(irish_counties_list)

for id in range(0, num_states):
    # DNS-SD/MDNS
    instance = 'jbhasd_sim%d' % (id)
    port = 9000 + id

    zone = us_states_list[id]
    print("Generating cherrypy server.. %s zone:%s port:%d" % (instance, zone, port))

    json_status = json_status_tmpl
    json_status = json_status.replace("__DEVICE_NAME__", instance)
    json_status = json_status.replace("__ZONE__", zone)
    json_data = json.loads(json_status)

    # Generate controls
    # PIck a number and then random index
    # We then module cycle through that index 
    # naming controls after the list entry
    num_controls = random.randint(1, 5) 
    control_index = random.randint(0, 1000000) % num_counties
    for i in range(0, num_controls):
        control_state = random.randint(0, 100) % 2
        control_name = irish_counties_list[control_index]
        json_data['controls'].append({'name': control_name, 'type' : 'switch', 'state' : control_state})
        control_index = (control_index + 1) % num_counties

    json_status_dict[port] = json_data

    # Cherrypy web service
    server = cherrypy._cpserver.Server()
    server.socket_port = port
    server._socket_host = '0.0.0.0'
    server.thread_pool = 2
    server.subscribe()
    server_list.append(server)

cherrypy.engine.start()

for id in range(0, num_states):
    # DNS-SD/MDNS
    instance = 'jbhasd_sim%d' % (id)
    svc_type = 'JBHASD'
    mdns_svc = '_' + svc_type + '._tcp.local.'
    mdns_host = instance + '.local.'
    mdns_name = instance + '._' + svc_type + '._tcp.local.'
    desc = {'desc': 'Nothing to see here folks'}
    ip = my_ip
    port = 9000 + id

    print("Generating DNS-SD info.. %s ip:%s port:%d" % (mdns_name, ip, port))

    info = ServiceInfo(mdns_svc,
                       mdns_name,
                       socket.inet_aton(ip), 
                       port, 
                       0, 
                       0,
                       desc, 
                       mdns_host)
    
    zeroconf.register_service(info)

random_t = threading.Thread(target = change_controls)
random_t.daemon = True
random_t.start()

# Cherrypy mainloop
cherrypy.engine.block()
