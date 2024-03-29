# Pathwinder

Pathwinder redirects file operations from one location to another in a way that is completely transparent to the application. It does so by hooking low-level Windows system calls and modifying them, subject to the control of the user-supplied redirection rules with which it is configured.

The application itself sees a completely consistent, but potentially illusionary, view of the filesystem. It continues to issue its file operation requests as usual, and Pathwinder will apply the redirection rules and potentially redirect these requests to other filesystem locations without the application being aware.

Pathwinder is implemented as a hook module and needs to be loaded into an application using [Hookshot](https://github.com/samuelgr/Hookshot).

## Differences from Filesystem Links

A natural question after reading a basic description of Pathwinder's functionality is, "how is this different from something like a symbolic link?"

Pathwinder *can* behave in a way that is similar to using a filesystem link, such as a symbolic link, but that is the [simplest and most basic](https://github.com/samuelgr/Pathwinder/wiki/Mechanics-of-Filesystem-Rules#Entire-Directory-Replacement) way of configuring it. Even then, Pathwinder is different:

- On Windows, creating filesystem links generally requires administrative privileges. Pathwinder does not impose this requirement.
    - Windows might demand administrative privileges when writing Pathwinder's configuration file, depending on where it is located.

- Pathwinder works where filesystem links do not. Because filesystem links themselves are special objects that require support from the underlying filesystem, they are not suitable for all situations. Pathwinder binaries and configuration files are just normal files, so Pathwinder can support situations like these without issue. For example:
    - Cloud syncing services typically cannot properly handle filesystem links. Creating a filesystem link within a directory that syncs to the cloud normally results in either a sync error or the link's target being synced (rather than the link itself).
    - Filesystems used on removable storage devices like USB keys typically do not support filesystem links, and attempting to create them would fail with an error.

- Making a filesystem link is a system-wide change, visible to all applications that access it. Pathwinder, on the other hand, does not make any persistent changes to the filesystem, nor does it make any system-wide changes whatsoever. Pathwinder only redirects file operations for those applications that the end user has configured, and even then, only while they are running.

Pathwinder additionally offers several features that cannot be replicated by using filesystem links alone.

- A filesystem link for a directory redirects *all filesystem operations* that access that directory. This is not necessarily true for Pathwinder; it supports file patterns so that only those filenames that match the patterns are redirected.

- Pathwinder supports an "overlay" mode that has the effect of merging the contents of multiple directories so they all appear to be part of the same directory.
    - This is very similar to how an [overlay filesystem works](https://docs.kernel.org/filesystems/overlayfs.html#upper-and-lower), except that Pathwinder is not limited to merging just two directories.

- Unlike filesystem links, which use a fixed origin and target path, Pathwinder determines the origin and target of redirections [dynamically and at run-time](https://github.com/samuelgr/Pathwinder/wiki/Configuration#Dynamic-Reference-Resolution).
    - This means that, if Pathwinder is configured correctly, two different users logged into the same machine could run the same program at the exact same time and Pathwinder would be able to redirect file operations differently for each (such as to their own per-user locations).

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

A basic configuration file is shown below, with specific emphasis on how to create filesystem rules. Refer to the [Configuration page](https://github.com/samuelgr/Pathwinder/wiki/Configuration) for more details.

```ini
; This section defines a filesystem rule called "MyRedirectionRule" but the actual name can be arbitrary.
[FilesystemRule:MyRedirectionRule]
OriginDirectory = C:\SomeDirectoryPath\Origin
TargetDirectory = C:\SomeOtherDirectoryPath\Target

; Multiple filesystem rules can exist in a configuration file.
[FilesystemRule:AnotherRule]
OriginDirectory = C:\AnotherRuleDirectoryPath\Origin
TargetDirectory = C:\AnotherRuleDirectoryPath2\Target

; A filesystem rule can include one or more file patterns.
; File patterns are allowed to include wildcards '*' and '?' but they are not regular expressions.
FilePattern = *.txt
FilePattern = save???.sav
FilePattern = OneSpecificFile.dat
```

## Further Reading

See the [Wiki](https://github.com/samuelgr/Pathwinder/wiki) for complete documentation.
