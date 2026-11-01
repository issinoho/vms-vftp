"""unix_ftpd.py <listen> <port> <root> <allowed,ips> [cert.pem] - a private FTP server for the
Unix-FTP live test (tools/live_unix_ftp.sh starts and stops it): pyftpdlib, run as
you, one login (vftptest, the password in tools/live_ftp_pass, D11), confined to
<root>, connections only from the given addresses. Commands are logged to
<root>/../ftpd.log (pyftpdlib logs "PASS ******", never the password). With a
certificate (and its key in the same file) it is FTPS: AUTH TLS, and TLS required
on the control and the data connections, so nothing goes in clear (D12)."""
import logging
import os
import sys

from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler, TLS_FTPHandler
from pyftpdlib.servers import FTPServer

listen, port, root, allowed = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4].split(",")
cert = sys.argv[5] if len(sys.argv) > 5 else None
top = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(top, "tools", "live_ftp_pass")) as f:
    password = f.readline().rstrip("\n")


class Handler(TLS_FTPHandler if cert else FTPHandler):
    def on_connect(self):
        if self.remote_ip not in allowed:
            self.respond("421 Not from here.")
            self.close_when_done()


auth = DummyAuthorizer()
auth.add_user("vftptest", password, root, perm="elradfmw")
del password
Handler.authorizer = auth
Handler.passive_ports = range(60100, 60200)
Handler.banner = "VFTP test FTP server (pyftpdlib%s) ready." % (", TLS" if cert else "")
if cert:
    Handler.certfile = cert
    Handler.keyfile = cert
    Handler.tls_control_required = True
    Handler.tls_data_required = True
logging.basicConfig(filename=os.path.join(os.path.dirname(root), "ftpd.log"), level=logging.DEBUG,
                    format="%(asctime)s %(message)s")
server = FTPServer((listen, port), Handler)
server.max_cons = 8
server.serve_forever()
