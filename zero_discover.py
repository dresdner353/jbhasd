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
http_timeout_secs = 2

class MyListener(object):  
    def remove_service(self, zeroconf, type, name):
        print("\nService %s removed" % (name))
        return

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        address = socket.inet_ntoa(info.address)
        port = info.port
        url = 'http://%s:%d' % (address, port)
        url_set.add(url)
        #print("\nDiscovered: %s\nURL: %s\nInfo:%s\n" % (name, url, info))
        json_url = '%s/json' % (url)

        response = None
        try:
            response = urllib.request.urlopen(json_url, 
                                              timeout = http_timeout_secs)
        except:
            print("Error in urlopen (status check)")

        if response is not None:
            response_str = response.read()
            print("%s\n%s\n" % (json_url, response_str))

zeroconf = Zeroconf()  
listener = MyListener()  
browser = ServiceBrowser(zeroconf, "_JBHASD._tcp.local.", listener)  

while 1:
    time.sleep(5)
