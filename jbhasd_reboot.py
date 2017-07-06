# Simple Python3 script to 
# use zeroconf to discover JBHASD devices on the LAN
# and print their details and URLs
# waits a long time and exits

from six.moves import input  
from zeroconf import ServiceBrowser, Zeroconf
import socket
import time
import urllib
import urllib.parse
import urllib.request
import urllib.error

url_set = set()

class MyListener(object):  
    def remove_service(self, zeroconf, type, name):
        return

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        if info is not None:
            address = socket.inet_ntoa(info.address)
            port = info.port
            url = 'http://%s:%d' % (address, port)
            url_set.add(url)
        return

zeroconf = Zeroconf()  
listener = MyListener()  
browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  

discovery_secs = 60
http_timeout_secs = 2

print("Giving %d seconds for discovery.." % (discovery_secs))
time.sleep(discovery_secs)

print( "Discovered..")
for url in url_set:
    reboot_url = '%s/json?reboot=1' % (url)
    print(reboot_url)

    response = None
    try:
        response = urllib.request.urlopen(reboot_url, 
                                          timeout = http_timeout_secs)
    except:
        print("Error in urlopen (status check)")

