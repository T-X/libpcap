/*
 * Copyright (c) 2002 - 2003
 * NetGroup, Politecnico di Torino (Italy)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include "rpcapd.h"
#include <pcap.h>		// for PCAP_ERRBUF_SIZE
#include "sockutils.h"	// for SOCK_ASSERT
#include "portability.h"
#include "fileconf.h"

static SERVICE_STATUS_HANDLE service_status_handle;
static SERVICE_STATUS service_status;

void svc_geterr(char *str);
static void WINAPI svc_main(DWORD argc, char **argv);
static void update_svc_status(DWORD state, DWORD progress_indicator);

int svc_start(void)
{
	int rc;
	SERVICE_TABLE_ENTRY ste[] =
	{
		{ PROGRAM_NAME, svc_main },
		{ NULL, NULL }
	};

	// This call is blocking. A new thread is created which will launch
	// the svc_main() function
	if ( (rc = StartServiceCtrlDispatcher(ste)) == 0)
		svc_geterr("StartServiceCtrlDispatcher()");

	return rc; // FALSE if this is not started as a service
}

void svc_geterr(char *str)
{
	char message[PCAP_ERRBUF_SIZE];
	char string[PCAP_ERRBUF_SIZE];
	int val;

	val = GetLastError();
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
				  FORMAT_MESSAGE_MAX_WIDTH_MASK,
				  NULL, val, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				  (LPSTR) string, PCAP_ERRBUF_SIZE, NULL);

	pcap_snprintf(message, PCAP_ERRBUF_SIZE, "%s failed with error %d: %s", str, val, string);

	SOCK_ASSERT(message, 1);
}

void WINAPI svc_control_handler(DWORD Opcode)
{
	switch(Opcode)
	{
		case SERVICE_CONTROL_STOP:
			//
			// XXX - is this sufficient to clean up the service?
			// To be really honest, only the main socket and
			// such these stuffs are cleared; however the threads
			// that are running are not stopped.
			// This can be seen by placing a breakpoint at the
			// end of svc_main(), in which you will see that is
			// never reached. However, as soon as you set the
			// service status to "stopped",	the
			// StartServiceCtrlDispatcher() returns and the main
			// thread ends. Then, Win32 has a good automatic
			// cleanup, so that all the threads which are still
			// running are stopped when the main thread ends.
			//
			send_shutdown_event();

			update_svc_status(SERVICE_STOP_PENDING, 0);
			break;

		/*
			Pause and Continue have an usual meaning and they are used just to be able
			to change the running parameters at run-time. In other words, they act
			like the SIGHUP signal on UNIX. All the running threads continue to run and
			they are not paused at all.
			Particularly,
			- PAUSE does nothing
			- CONTINUE re-reads the configuration file and creates the new threads that
			can be needed according to the new configuration.
		*/
		case SERVICE_CONTROL_PAUSE:
			update_svc_status(SERVICE_PAUSED, 0);
			break;

		case SERVICE_CONTROL_CONTINUE:
			update_svc_status(SERVICE_RUNNING, 0);
			fileconf_read();
			break;

		case SERVICE_CONTROL_INTERROGATE:
			// Fall through to send current status.
			//	WARNING: not implemented
			update_svc_status(SERVICE_RUNNING, 0);
			MessageBox(NULL, "Not implemented", "warning", MB_OK);
			break;

		case SERVICE_CONTROL_PARAMCHANGE:
			fileconf_read();
			break;
	}

	// Send current status.
	return;
}

void WINAPI svc_main(DWORD argc, char **argv)
{
	service_status_handle = RegisterServiceCtrlHandler(PROGRAM_NAME, svc_control_handler);

	if (!service_status_handle)
		return;

	service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
	service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE;
	// | SERVICE_ACCEPT_SHUTDOWN ;
	update_svc_status(SERVICE_RUNNING, 0);

	//
	// Service requests until we're told to stop.
	//
	main_startup();

	//
	// It returned, so we were told to stop.
	//
	update_svc_status(SERVICE_STOPPED, 0);
}

static void
update_svc_status(DWORD state, DWORD progress_indicator)
{
	service_status.dwWin32ExitCode = NO_ERROR;
	service_status.dwCurrentState = state;
	service_status.dwCheckPoint = progress_indicator;
	service_status.dwWaitHint = 0;
	SetServiceStatus(service_status_handle, &service_status);
}

/*
sc create rpcapd DisplayName= "Remote Packet Capture Protocol v.0 (experimental)" binpath= "C:\cvsroot\winpcap\wpcap\PRJ\Debug\rpcapd -d -f rpcapd.ini"
sc description rpcapd "Allows to capture traffic on this host from a remote machine."
*/
