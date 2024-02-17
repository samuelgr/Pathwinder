# Pathwinder

*Documentation is incomplete, possibly outdated or incorrect, and subject to change because Pathwinder is still in development.*

Pathwinder acts on existing compiled compiled binary 32-bit (x86) and 64-bit (x64) applications by redirecting file operations from one location to another, subject to the *redirection rules* with which it is configured. Pathwinder presents applications with a consistent, but potentially illusionary, view of the filesystem. Applications continue to issue their file operations as usual, without modification, and Pathwinder can redirect them to other filesystem locations without the application being aware.

As a simple conceptual example, suppose an application located at `C:\Program Files\MyApp\MyApp.exe` stores its data in `C:\Program Files\MyApp\Data\`. A Pathwinder user can configure it with a redirection rule such that Pathwinder *redirects* all accesses to that directory to some other location, like `C:\Users\username\Documents\MyApp\`. The application *thinks* it is still reading from and writing to `C:\Program Files\MyApp\Data\`, but in fact it is actually reading from and writing to `C:\Users\username\Documents\MyApp\`.

Pathwinder is implemented as a hook module and needs to be loaded into an application using [Hookshot](https://github.com/samuelgr/Hookshot).


## Key Features

Pathwinder supports both 32-bit (x86) and 64-bit (x64) Windows applications and offers the ability to:

- Redirect file operations from one location to another.

- Present applications with a consistent view of the directory hierarchy.

All of Pathwinder's functionality is configurable via a configuration file. The affected application is not aware of any of the changes Pathwinder is making.


## Limitations

As a hook module, Pathwinder is subject to the same limitations as Hookshot. Pathwinder cannot operate in any way whatsoever on existing, already-running processes. Pathwinder is also limited in scope to just those applications into which it is loaded with the explicit permission of the end user. It makes no attempts to hide its presence (in fact, as a hook module, it does the opposite), and it is incapable of making system-wide or persistent changes. **All of these limitations are by design.**


## Concepts and Terminology

Pathwinder uses *filesystem rules*, which are defined in a configuration file, to know how and when it should redirect a file operation from one location to another. Filesystem rules themselves are identified by name and defined using an *origin directory*, a *target directory*, and an optional set of *file patterns*.

The easiest way to understand how a filesystem rule functions is this: if the application issues a file operation for a path that is located in the *origin directory* then the file operation is redirected to the *target directory*. Subdirectory paths are also supported. The term *origin directory* refers to the point of origin, or the source location, of a possible redirection, and the term *target directory* accordingly refers to the destination location of a possible redirection. File accesses issued by an application are checked against origin directories and, if they match, they are redirected to the equivalent hierarchy location rooted at the target directory.

As an example, suppose a filesystem rule exists with origin directory `C:\Origin` and target directory `C:\Target`. If the application attempts to access the file `C:\Origin\file.txt` then Pathwinder will redirect it to `C:\Target\file.txt`. Similarly, if the application attempts to access `C:\Origin\Subdirectory\subfile1.txt`, then Pathwinder will redirect it to `C:\Target\Subdirectory\subfile1.txt`.

Some additional complexity is introduced when the filesystem rule additionaly specifies file patterns. Following the example above, suppose the filesystem rule in question uses `*.txt` as its file pattern. If the application accesses `C:\Origin\file.txt` then Pathwinder would redirect it to `C:\Target\file.txt` because `file.txt` matches the pattern `*.txt`. However, `C:\Origin\file.docx` would not be redirected because `file.docx` does not match the pattern `*.txt`. This same logic applies to subdirectory accesses; `C:\Origin\Subdirectory\subfile1.txt` would similarly not be redirected because `Subdirectory` does not match the pattern `*.txt`.

Note that all filename and directory path matching is completely case-insensitive. Therefore, `C:\origin\FILE.TXT` and `C:\ORIGIN\file.tXt` would both match the example rule and be directed. Filename case is preserved, however, so the results of the redirection would respectively be `C:\Target\FILE.TXT` and `C:\Target\file.tXt`.

There are many nuances to exactly how filesystem rules work, particularly in tandem with one another, and those nuances are deliberately omitted from this section to keep the concepts discussed here relatively simple and introductory. A much more detailed discussion of how filesystem rules work is available [later in this document](#mechanics-of-filesystem-rules).


## Using Pathwinder

This section describes how to set up and run Pathwinder. Its target audience is end users.


### Getting Started

1. Ensure the system is running Windows 10 or 11. Pathwinder is built to target Windows 10 or 11 and does not support older versions of Windows.

1. Ensure the [Visual C++ Runtime for Visual Studio 2022](https://docs.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) is installed. Pathwinder is linked against this runtime and will not work without it. If running a 64-bit operating system, install both the x86 and the x64 versions of this runtime, otherwise install just the x86 version.

1. Download the [latest release of Hookshot](https://github.com/samuelgr/Hookshot/releases) and [follow the instructions](https://github.com/samuelgr/Hookshot/blob/master/USERS.md#getting-started) on how to set it up for a given application. This most likely means placing all of the Hookshot executables and DLLs into the same directory as the application's executable file.

1. Download the latest release of Pathwinder and place its DLL file&mdash;`Pathwinder.HookModule.32.dll` for 32-bit applications or `Pathwinder.HookModule.64.dll` for 64-bit applications&mdash;into the same directory as the executable file for the desired application.

1. Create or place a [configuration file](#configuring-pathwinder) into the same directory as Pathwinder's DLL file for the desired application.

1. Use Hookshot to run the desired application.


### Configuring Pathwinder

Pathwinder requires a configuration file be present to provide it with the rules it should follow when redirecting filesystem locations.  The configuration file, `Pathwinder.ini`, follows standard INI format: name-value pairs scoped into different sections.

Pathwinder will display a warning message box and automatically enable logging if it detects a configuration file error on application start-up. Consult the log file that Pathwinder places on the desktop for the details of any errors.

The subsections that follow describe how to configure Pathwinder using a configuration file.


#### Dynamic Reference Resolution

Many of the string-valued settings that exist within a Pathwinder configuration file identify full absolute directory paths, particularly those within filesystem rule definitions. However, use cases for Pathwinder are not always amenable to a single static absolute path being used as the origin or target directory for a filesystem rule, and therefore Pathwinder supports dynamic reference resolution as a way of generating these absolute paths automatically and at runtime.

For example, if the goal is to use a subdirectory of the current user's "Documents" folder as the target directory, one option is to hard-code it as `C:\Users\username\Documents`, but this only works for one user, and it also only works if the user has not changed their "Documents" folder location from the default. Clearly this is not a scalable solution. Much better would be if Pathwinder can query the system for that information and populate the target directory location automatically.

Another example is specifying an origin directory whose path is relative to the application's executable. This is especially important for configuration files intended to be redistributed, whereby each user may have installed the application to a different location. Pathwinder does require all directories to be full absolute paths, but it can automatically determine those paths to help with situations such as these.

Instead of hard-coding a complete path in a Pathwinder configuration file, it is possible to replace part, or all, of the path with a *dynamic reference*, which Pathwinder will resolve at runtime. A dynamic reference takes the form of a named variable enclosed between two percent symbols. The named variable is additionally namespaced within one of a few supported domains. The general format looks like `%DOMAIN::VARIABLE_NAME%`. The `DOMAIN::` part is optional and, if omitted, is assumed to be a default value referring to an environment variable.

Supported domains are as follows.

- **BUILTIN** contains some variables that Pathwinder internally computes and makes available, as follows.
   - `%BUILTIN::ExecutableCompleteFilename%` returns the complete filename of the running executable, for example `C:\Directory\MyApp.exe`.
   - `%BUILTIN::ExecutableBaseName%` returns just the filename part of the running executable, for example `MyApp.exe`.
   - `%BUILTIN::ExecutableDirectoryName%` returns the directory in which the running executable is located without a trailing backslash, for example `C:\Directory`.
   - `%BUILTIN::PathwinderCompleteFilename%` returns the complete filename of the Pathwinder DLL, for example `C:\Directory\Pathwinder.HookModule.64.dll`.
   - `%BUILTIN::PathwinderBaseName%` returns just the filename part of the Pathwinder DLL, for example `Pathwinder.HookModule.64.dll`.
   - `%BUILTIN::PathwinderDirectoryName%` returns the directory in which the Pathwinder DLL is located without a trailing backslash, for example `C:\Directory`.
- **CONF** contains all of the variables defined in the [variable definitions](#variable-definitions) section of the configuration file.
   - For example, `%CONF::SaveDirectoryPath%` resolves to the value of the "SaveDirectoryPath" defined in the variable definitions section.
- **ENV** contains environment variables.
   - This is the default domain if no domain is explicitly specified. For example, `%ENV::LOCALAPPDATA%` is equivalent to `%LOCALAPPDATA%`.
   - To see which environment variables are typically available, run any terminal application and then execute the command `set`.
- **FOLDERID** contains all of the [known folder identifiers](https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid#constants) made available by the Windows shell.
   - For example, `%FOLDERID::Desktop%` resolves to the "Desktop" directory for the current user.
   - Note that several of these known folders resolve to locations that typically cannot be accessed for file operations. Pathwinder will accept these locations in dynamic references but likely will not be able to use them for file operations.

Returning to the two motivating examples presented previously, it is clearly better to use dynamic references than it is to hard-code a fixed absolute path. Here is how both use cases would be achieved.
- `%FOLDERID::Documents%\MyAppData` would resolve to the subdirectory `MyAppData` of the current user's "Documents" folder.
- `%BUILTIN::ExecutableDirectoryName%\DataDirectory` would resolve to the subdirectory `DataDirectory` of the directory in which the currently-running executable is located.


#### Configuration File Structure

The structure of Pathwinder's configuration file consists of an initial section for global configuration options followed by an optional section of variable definitions and finally one section per filesystem rule. An example configuration file is shown below, containing default values for all of the global configuration settings and generally showing how to create a filesystem rule.

```ini
; Global configuration options appear at the top.
; No section name is present.
; Default values are shown here.

LogLevel = 0
DryRun = no

; Comments are supported,
; as long as they are on lines of their own.


[Definitions]
; Variable definitions go here.
; They can be referenced elsewhere in this section or even inside filesystem rule sections.
; This section does not exist by default.


; Filesystem rules exist in their own named sections. None exist by default.
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

More details on all of the supported options are available in the subsections that follow.


##### Global Settings

At global scope (i.e. at the top of the file, before any sections are listed), the following settings are supported.

- **LogLevel**, an integer value used to configure logging. This is useful when testing configuration files and when troubleshooting. When logging is enabled, messages produced by Pathwinder are written to a file on the current user's desktop that is named to identify Pathwinder, the executable name, and the process ID number. A value for this setting should only be specified once.
   - 0 means logging is disabled. This is the default.
   - 1 means to include error messages only.
   - 2 means to include errors and warnings.
   - 3 means to include errors, warnings, and informational messages.
   - 4 means to include errors, warnings, informational messages, and several internal debugging messages.
   - 5 means to include errors, warnings, informational messages, and all internal debugging messages. This level is extremely verbose.
- **DryRun**, a Boolean value that is used to tell Pathwinder whether or not it should actually redirect file operations rather than just log the decisions it makes.
   - Enabling this setting, by setting it to `yes`, means that Pathwinder will output to its log file all of the redirection decisions it makes, but will not actually execute any redirections. This is useful for testing a configuration file, and it works best with LogLevel set to 3.


##### Variable Definitions

This section exists for convenience. It allows arbitrary string-valued variables to be defined that can be referenced either in this same section or in filesystem rules sections when specifying origin and target directory paths. Variable definitions take the form of any other setting. Below is an example of how this section might prove useful.

```ini
[Definitions]
PerUserDataBaseDirectory = %FOLDERID::Documents%\MyAppData

; The subfolder "MyAppData" of the current user's documents directory is now available as a variable and can be referenced as
; %CONF::PerUserDataBaseDirectory%

; Variable names can be arbitrary, but all characters must be alphanumeric or these special characters only: . - _
```


##### Filesystem Rules

Within a configuration file, a section named "FilesystemRule:*name*" defines a filesystem rule called *name*. The name can be arbitrary, but it must be unique among all filesystem rules in the same configuration file, and it is used for logging to identify which filesystem rule caused Pathwinder to decide to redirect a particular file operation.

Defining a filesystem rule requires that the two directories below be included in a filesystem rule section. Definitions and conceptual explanations for what these directories signify are available [earlier in this document](#concepts-and-terminology), and more details on exactly how filesystem rules work is available in the [next section](#mechanics-of-filesystem-rules).

- **OriginDirectory**, a string value that identifies the full absolute path of a filesystem rule's *origin directory*.
- **TargetDirectory**, a string value that identifies the full absolute path of a filesystem rule's *target directory*.

Filesystem rules can additionally define one or more file patterns, which can be used to restrict the scope of a filesystem rule to just those file names that match the file patterns. These would be specified using the optional configuration setting below.

- **FilePattern**, a string value that represents one or more file names that should be included within the scope of the filesystem rule.
   - Multiple file patterns can be specified for each filesystem rule.
   - If no file patterns are specified, it is assumed that there is no restriction on the scope of a filesystem rule. This is semantically equivalent to including `FilePattern = *` as a configuration setting, although if this is the goal then it is better to omit file patterns entirely.




## Mechanics of Filesystem Rules

(to be filled in)
