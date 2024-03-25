# Pathwinder

Pathwinder redirects file operations from one location to another in a way that is completely transparent to the application. It does so by hooking low-level Windows system calls and modifying them, subject to the control of the user-supplied redirection rules with which it is configured.

The application itself sees a completely consistent, but potentially illusionary, view of the filesystem. It continues to issue its file operation requests as usual, and Pathwinder will apply the redirection rules and potentially redirect these requests to other filesystem locations without the application being aware.

Pathwinder is implemented as a hook module and needs to be loaded into an application using [Hookshot](https://github.com/samuelgr/Hookshot).


## Getting Started

1. Ensure the system is running Windows 10 or 11. Pathwinder is built to target Windows 10 or 11 and does not support older versions of Windows.

1. Ensure the [Visual C++ Runtime for Visual Studio 2022](https://docs.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) is installed. Pathwinder is linked against this runtime and will not work without it. If running a 64-bit operating system, install both the x86 and the x64 versions of this runtime, otherwise install just the x86 version.

1. Download the [latest release of Hookshot](https://github.com/samuelgr/Hookshot/releases) and [follow the instructions](https://github.com/samuelgr/Hookshot/blob/master/USERS.md#getting-started) on how to set it up for a given application. This most likely means placing all of the Hookshot executables and DLLs into the same directory as the application's executable file.

1. Download the latest release of Pathwinder and place its DLL file into the same directory as the executable file for the desired application.

1. Create or place a configuration file into the same directory as Pathwinder's DLL file for the desired application.

1. Use Hookshot to run the desired application.


## Configuring Pathwinder

Pathwinder requires a configuration file be present to provide it with the rules it should follow when redirecting filesystem locations.  The configuration file is called Pathwinder.ini and follows standard INI format: name-value pairs scoped into different sections.

Pathwinder will display a warning message box and automatically enable logging if it detects a configuration file error on application start-up. Consult the log file that Pathwinder places on the desktop for the details of any errors.


## Full Documentation

See the [Wiki](https://github.com/samuelgr/Pathwinder/wiki) for complete documentation.
