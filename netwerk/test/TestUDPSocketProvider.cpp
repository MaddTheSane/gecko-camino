/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is UDP Socket Provider unit test.
 *
 * The Initial Developer of the Original Code is
 * Mook <mook.moz+cvs.mozilla.org@gmail.com>.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "stdio.h"
#include "TestCommon.h"
#include "nsCOMPtr.h"
#include "nsIServiceManager.h"
#include "nsIComponentRegistrar.h"
#include "nspr.h"
#include "nsServiceManagerUtils.h"
#include "nsISocketTransportService.h"
#include "nsISocketTransport.h"
#include "nsIOutputStream.h"
#include "nsIInputStream.h"

#define UDP_PORT 9050

#define UDP_ASSERT(condition, message) \
    PR_BEGIN_MACRO                     \
    NS_ASSERTION(condition, message);  \
    if (!(condition)) {                \
        returnCode = -1;               \
        break;                         \
    }                                  \
    PR_END_MACRO

#define UDP_ASSERT_PRSTATUS(message) \
    PR_BEGIN_MACRO \
    NS_ASSERTION(status == PR_SUCCESS, message); \
    if (status != PR_SUCCESS) { \
        PRErrorCode err = PR_GetError(); \
        fprintf(stderr, \
                "FAIL nspr: %s: (%08x) %s\n", \
                message, \
                err, \
                PR_ErrorToString(err, PR_LANGUAGE_I_DEFAULT)); \
        returnCode = -1; \
        break; \
    } \
    PR_END_MACRO

#define UDP_ASSERT_NSRESULT(message) \
    PR_BEGIN_MACRO \
    NS_ASSERTION(NS_SUCCEEDED(rv), message); \
    if (NS_FAILED(rv)) { \
        fprintf(stderr, "FAIL UDPSocket: %s: %08x\n", \
                message, rv); \
        returnCode = -1; \
        break; \
    } \
    PR_END_MACRO

int
main(int argc, char* argv[])
{
    if (test_common_init(&argc, &argv) != 0)
        return -1;

    int returnCode = 0;
    nsresult rv = NS_OK;
    PRFileDesc *serverFD = nsnull;

    do { // act both as a scope for nsCOMPtrs to be released before XPCOM
         // shutdown, as well as a easy way to abort the test
        PRStatus status = PR_SUCCESS;

        nsCOMPtr<nsIServiceManager> servMan;
        NS_InitXPCOM2(getter_AddRefs(servMan), nsnull, nsnull);
        nsCOMPtr<nsIComponentRegistrar> registrar = do_QueryInterface(servMan);
        UDP_ASSERT(registrar, "Null nsIComponentRegistrar");
        if (registrar)
            registrar->AutoRegister(nsnull);

        // listen for a incoming UDP connection on localhost
        serverFD = PR_OpenUDPSocket(PR_AF_INET);
        UDP_ASSERT(serverFD, "Cannot open UDP socket for listening");

        PRSocketOptionData socketOptions;
        socketOptions.option = PR_SockOpt_Nonblocking;
        socketOptions.value.non_blocking = PR_FALSE;
        status = PR_SetSocketOption(serverFD, &socketOptions);
        UDP_ASSERT_PRSTATUS("Failed to set server socket as blocking");

        PRNetAddr addr;
        status = PR_InitializeNetAddr(PR_IpAddrLoopback, UDP_PORT, &addr);
        UDP_ASSERT_PRSTATUS("Failed to initialize loopback address");

        status = PR_Bind(serverFD, &addr);
        UDP_ASSERT_PRSTATUS("Failed to bind server socket");

        // dummy IOService to get around bug 379890
        nsCOMPtr<nsISupports> ios =
            do_GetService("@mozilla.org/network/io-service;1");

        // and have a matching UDP connection for the client
        nsCOMPtr<nsISocketTransportService> sts =
            do_GetService("@mozilla.org/network/socket-transport-service;1", &rv);
        UDP_ASSERT_NSRESULT("Cannot get socket transport service");

        nsCOMPtr<nsISocketTransport> transport;
        const char *protocol = "udp";
        rv = sts->CreateTransport(&protocol, 1, NS_LITERAL_CSTRING("localhost"),
                                  UDP_PORT, nsnull, getter_AddRefs(transport));
        UDP_ASSERT_NSRESULT("Cannot create transport");
        
        PRUint32 count, read;
        const PRUint32 data = 0xFF0056A9;

        // write to the output stream
        nsCOMPtr<nsIOutputStream> outstream;
        rv = transport->OpenOutputStream(nsITransport::OPEN_BLOCKING,
                                         0, 0, getter_AddRefs(outstream));
        UDP_ASSERT_NSRESULT("Cannot open output stream");

        rv = outstream->Write((const char*)&data, sizeof(PRUint32), &count);
        UDP_ASSERT_NSRESULT("Cannot write to output stream");
        UDP_ASSERT(count == sizeof(PRUint32),
                   "Did not write enough bytes to output stream");

        // read from NSPR to check it's the same
        count = PR_RecvFrom(serverFD, &read, sizeof(PRUint32), 0, &addr, 1);
        UDP_ASSERT(count == sizeof(PRUint32),
                   "Did not read enough bytes from NSPR");
        status = (read == data ? PR_SUCCESS : PR_FAILURE);
        UDP_ASSERT_PRSTATUS("Did not read expected data from NSPR");

        // write to NSPR
        count = PR_SendTo(serverFD, &data, sizeof(PRUint32), 0, &addr, 1);
        status = (count == sizeof(PRUint32) ? PR_SUCCESS : PR_FAILURE);
        UDP_ASSERT_PRSTATUS("Did not write enough bytes to NSPR");
        
        // read from stream
        nsCOMPtr<nsIInputStream> instream;
        rv = transport->OpenInputStream(nsITransport::OPEN_BLOCKING,
                                        0, 0, getter_AddRefs(instream));
        UDP_ASSERT_NSRESULT("Cannot open input stream");
        
        rv = instream->Read((char*)&read, sizeof(PRUint32), &count);
        UDP_ASSERT_NSRESULT("Cannot read from input stream");
        UDP_ASSERT(count == sizeof(PRUint32),
                   "Did not read enough bytes from input stream");
        UDP_ASSERT(read == data, "Did not read expected data from stream");

    } while (false); // release all XPCOM things
    if (serverFD) {
        PRStatus status = PR_Close(serverFD);
        if (status != PR_SUCCESS) {
            PRErrorCode err = PR_GetError();
            fprintf(stderr, "FAIL: Cannot close server: (%08x) %s\n",
                    err, PR_ErrorToString(err, PR_LANGUAGE_I_DEFAULT));
        }
    }
    rv = NS_ShutdownXPCOM(nsnull);
    NS_ASSERTION(NS_SUCCEEDED(rv), "NS_ShutdownXPCOM failed");    

    return returnCode;
}

