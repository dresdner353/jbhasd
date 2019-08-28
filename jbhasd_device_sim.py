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

us_states_list = ['Alabama', 'Alaska', 'Arizona', 'Arkansas', 'California', 'Colorado', 'Connecticut', 'Delaware', 'Florida', 'Georgia', 'Hawaii', 'Idaho', 'Illinois', 'Indiana', 'Iowa', 'Kansas', 'Kentucky', 'Louisiana', 'Maine', 'Maryland', 'Massachusetts', 'Michigan', 'Minnesota', 'Mississippi', 'Missouri', 'Montana', 'Nebraska', 'Nevada', 'New Hampshire', 'New Jersey', 'New Mexico', 'New York', 'North Carolina', 'North Dakota', 'Ohio', 'Oklahoma', 'Oregon', 'Pennsylvania', 'Rhode Island', 'South Carolina', 'South Dakota', 'Tennessee', 'Texas', 'Utah', 'Vermont', 'Virginia', 'Washington', 'West Virginia', 'Wisconsin', 'Wyoming', 'District of Columbia', 'Puerto Rico', 'Guam', 'American Samoa', 'U.S. Virgin Islands', 'Northern Mariana Islands']

irish_counties_list = ['Antrim', 'Armagh', 'Carlow', 'Cavan', 'Clare', 'Cork', 'Derry', 'Donegal', 'Down', 'Dublin', 'Fermanagh', 'Galway', 'Kerry', 'Kildare', 'Kilkenny', 'Laois', 'Leitrim', 'Limerick', 'Longford', 'Louth', 'Mayo', 'Meath', 'Monaghan', 'Offaly', 'Roscommon', 'Sligo', 'Tipperary', 'Tyrone', 'Waterford', 'Westmeath', 'Wexford', 'Wicklow']

irish_rivers_list = ['Shannon', 'Barrow', 'Suir', 'Blackwater', 'Bann', 'Nore', 'Suck', 'Liffey', 'Erne', 'Foyle', 'Slaney', 'Boyne', 'Moy', 'Clare', 'Blackwater', 'Inny', 'Lee', 'Lagan', 'Brosna', 'Laune', 'Feale', 'Bandon', 'Blackwater', 'Annalee', 'Bride', 'Boyle ', 'Deel', 'Robe', 'Finn', 'Maigue', 'Fane ', 'Ballisodare', 'Dee', 'Fergus', 'Little Brosna', 'Mulkear ', 'Glyde']

switch_context_strs = ['init', 'network', 'manual', 'motion']

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


class device_status_server(object):
    @cherrypy.expose()
    @cherrypy.tools.json_out()

    def index(self):
        # determine port of called URL 
        parsed_url = urllib.parse.urlparse(cherrypy.url())
        url_port = parsed_url.port
        json_data = json_status_dict[url_port]
        device_name = json_data['name']

        print("%s /status port:%d device:%s" % (
            time.asctime(),
            url_port,
            device_name))

        return json.dumps(json_data, indent = 2)


    # Force trailling slash off on called URL
    index._cp_config = {'tools.trailing_slash.on': False}


class device_control_server(object):
    @cherrypy.expose()
    @cherrypy.tools.json_out()

    def index(self):
        # determine port of called URL 
        parsed_url = urllib.parse.urlparse(cherrypy.url())
        url_port = parsed_url.port
        json_data = json_status_dict[url_port]
        device_name = json_data['name']

        # POST JSON Body
        cl = cherrypy.request.headers['Content-Length']
        rawbody = cherrypy.request.body.read(int(cl)).decode("utf-8")
        json_request = json.loads(str(rawbody))

        print("%s /control port:%d device:%s \nJSON:\n%s" % (
            time.asctime(),
            url_port,
            device_name,
            json.dumps(json_request, 
                indent = 2)))

        for control in json_request['controls']:
            for json_control in json_data['controls']:
                if json_control['name'] == control['name']:
                    json_control['state'] = control['state']
                    json_control['context'] = 'network'

        return json.dumps(json_data, indent = 2)


    # Force trailling slash off on called URL
    index._cp_config = {'tools.trailing_slash.on': False}


def change_device_status():
    # Randomise changes in the devices

    while(1):
        ports_list = list(json_status_dict)
        num_ports = len(ports_list)

        # controls
        num_devices_to_change = random.randint(0, 40)
        print("Changing controls for %d devices" % (num_devices_to_change))
        for i in range(0, num_devices_to_change):
            port_index = random.randint(1, 10000000) % num_ports
            port = ports_list[port_index]
            device_json = json_status_dict[port]
            num_controls_in_device = len(device_json['controls'])
            num_controls_to_change = random.randint(1, 10000000) % (num_controls_in_device + 1)
            print("Changing %d controls for %s" % (num_controls_to_change, 
                                                   device_json['name']))
            for j in range(0, num_controls_to_change):
                control_index = random.randint(1, 10000000) % num_controls_in_device
                control_type = device_json['controls'][control_index]['type']
                if control_type == 'switch':
                    control_state = random.randint(0, 100) % 2
                    control_context = random.randint(0, 100) % 4
                    print("Changing switch %s state to %d (%s)" % (
                        device_json['controls'][control_index]['name'], 
                        control_state, switch_context_strs[control_context]))
                    device_json['controls'][control_index]['state'] = control_state
                    device_json['controls'][control_index]['context'] = switch_context_strs[control_context]

                if control_type == 'temp/humidity':
                    sensor_temp = "%.2f" % (random.uniform(-30, 95))
                    sensor_humidity = "%.2f" % (random.uniform(0, 100))
                    print("Changing sensor %s temp:%s humidity:%s" % (
                        device_json['controls'][control_index]['name'], 
                        sensor_temp,
                        sensor_humidity))
                    device_json['controls'][control_index]['temp'] = sensor_temp
                    device_json['controls'][control_index]['humidity'] = sensor_humidity

        time.sleep(10)


# main

zeroconf = Zeroconf()
my_ip = get_ip()

# more involved start of cherrypy as we 
# want to have multiple separate ports, one per device
cherrypy.tree.mount(device_status_server(), '/')
cherrypy.tree.mount(device_status_server(), '/status')
cherrypy.tree.mount(device_control_server(), '/control')

# Dummy status handlers for the other API functions
cherrypy.tree.mount(device_status_server(), '/reboot')
cherrypy.tree.mount(device_status_server(), '/apmode')
cherrypy.tree.mount(device_status_server(), '/reset')
cherrypy.tree.mount(device_status_server(), '/reconfigure')
cherrypy.tree.mount(device_status_server(), '/configure')
cherrypy.server.unsubscribe()
# Logging off
cherrypy.config.update({'log.screen': False,
                        'log.access_file': '',
                        'log.error_file': ''})
server_list = []
num_states = len(us_states_list)
num_counties = len(irish_counties_list)
num_rivers = len(irish_rivers_list)

for id in range(0, num_states):
    # DNS-SD/MDNS
    instance = 'JBHASD-BEEFED%02X' % (id)
    port = 9000 + id

    zone = us_states_list[id]
    print("Generating cherrypy server.. %s zone:%s port:%d" % (instance, zone, port))

    json_data = {}
    json_data['name'] = instance
    json_data['zone'] = zone
    json_data['controls'] = []

    # Generate controls
    # PIck a number and then random index
    # We then module cycle through that index 
    # naming controls after the list entry
    num_controls = random.randint(1, 10) 
    control_index = random.randint(0, 1000000) % num_counties
    sensor_index = random.randint(0, 1000000) % num_rivers
    for i in range(0, num_controls):
        control_type = random.randint(0, 100) % 2
        if control_type == 0:
            # switch
            control_state = random.randint(0, 100) % 2
            control_name = irish_counties_list[control_index]
            switch = {}
            switch['name'] = control_name
            switch['type'] = 'switch'
            switch['state'] = control_state
            switch['context'] = 'init'
            json_data['controls'].append(switch)
            control_index = (control_index + 1) % num_counties
        else:
            # sensor
            sensor_temp = "%.2f" % (random.uniform(-30, 95))
            sensor_humidity = "%.2f" % (random.uniform(0, 100))
            sensor_name = irish_rivers_list[sensor_index]
            sensor = {}
            sensor['name'] = sensor_name
            sensor['type'] = 'temp/humidity'
            sensor['temp'] = sensor_temp
            sensor['humidity'] = sensor_humidity
            json_data['controls'].append(sensor)
            sensor_index = (sensor_index + 1) % num_rivers

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
    instance = 'JBHASD-BEEFED%02X' % (id)
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

random_t = threading.Thread(target = change_device_status)
random_t.daemon = True
random_t.start()

# Cherrypy mainloop
cherrypy.engine.block()
