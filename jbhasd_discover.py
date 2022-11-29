# Simple Python3 script to 
# use zeroconf to discover JBHASD devices on the LAN
# and print their details and URLs
# waits a long time and exits

from zeroconf import ServiceBrowser, Zeroconf
import socket
import time
import requests
import sys
import os
import json

class MyListener(object):  
    def remove_service(self, zeroconf, type, name):
        return

    def add_service(self, zeroconf, type, name):

        info = zeroconf.get_service_info(type, name)
        address = socket.inet_ntoa(info.addresses[0])
        #address = socket.inet_ntoa(info.address)
        port = info.port
        url = 'http://%s:%d/status' % (address, port)
        server = info.server
        print("Discovered: %s URL: %s" % (server, url))

        response = None
        api_session = requests.session()

        try:
            response = api_session.get(url)
        except:
            print("Error in urlopen (%s)" % (url))

        if response is not None:
            response_dict = response.json()
            print("json:\n%s" % (
                json.dumps(
                    response_dict, 
                    indent = 4)))

        sys.stdout.flush()



zeroconf = Zeroconf()  
listener = MyListener()  
browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  
time.sleep(30)
zeroconf.close()

