# j2534

j2534 is a library written specifically for the [Tactrix Openport 2.0](https://www.tactrix.com/index.php?option=com_virtuemart&page=shop.product_details&flypage=flypage.tpl&product_id=17&Itemid=53&redirected=1&Itemid=53 "Tactrix Openport 2.0") cable.

This library implements most of the SAE J2534-1 API functions as used by the [RomRaider - Open Source ECU Tuning](https://www.romraider.com/ "RomRaider - Open Source ECU Tuning") project.

The library can be compiled on Linux or Windows for either 32 or 64 bits systems.  The library depends on [libusb](https://libusb.info/ "libusb") version 1.08 or higher.


## Linux Compilation
- Install pkg-config
- Install libusb-1.0-devel
- Run make on the command line to compile the library
- Run make install if you wish to install the library in `/usr/local/lib/`


## Windows Compilation
To compile under Windows you can use 'Build Tools for Visual Studio' or install the Visual Studio Community IDE and the build tools package.  

- From the [libusb](https://libusb.info/ "libusb") web site download the Windows binaries
- Extract the archive using [7-Zip](https://www.7-zip.org/ "7-Zip")
- Copy or move the libusb folder that matches your VS version into the base j2534 project directory
- Rename the VS20xx folder to libusb
- Copy or move the libusb include folder from the archive into the renamed (libusb) folder in the project

### Command line build
To build on the command line, open '__platform__ Native Tools Command Prompt' where __platform__ is either x86 or x64 and change into the j2534 project directory.

To build the Release version of the project, on the command line enter:  
&nbsp;&nbsp;&nbsp;&nbsp;`msbuild.exe j2534.sln /p:Configuration="Release"`

### Visual Studio build
To build in Visual Studio, launch Visual Studio and open the solution file in the j2534 project directory.  Select Debug|Release and x86|x64 in the toolbar and from the Build menu select Build Solution.


## Using the library
Before using this library, remove the SD card from the Openport 2.0

### Linux
USB devices require write permission, add a udev rule entry in `/etc/udev/rules.d/`
with the contents such as this to allow write access:  
`SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTR{idProduct}=="cc4d", GROUP="dialout", MODE="0666"`

Your User ID must also be a member of the `dialout` group on the system.

### Windows
Before using the this library with the Openport 2.0 cable the system driver from Tactrix has to be replaced with the WinUSB driver so that libusb can interface your application with the Openport 2.0 cable.  The easiest way to complete this is to use [Zadig](https://zadig.akeo.ie/ "Zadig").  Follow the instructions on the Zadig web site after you download the application.

The SAE J2534-1 specification indicates that J2534 drivers can be located via the Windows Registry.  After building the project copy the `j2534.dll` and the `libusb-1.0.dll` into the appropriate system directory. The default locations are:  
x86 - `C:\Windows\SysWOW64\`  
x64 - `C:\Windows\System32\`

For convenience a registry merge file has been provided in the extras directory to populate the Registry with the information pertaining to the J2534 DLL location and supported protocols. Note: the SAE J2534-1 specification does not provide any guidance for x64 systems.  
If you wish to use a different location for the `j2534.dll` and the `libusb-1.0.dll` on your system, adjust the registry merge file accordingly.

If you wish to revert back to the Tactrix provided driver for the Openport 2.0, open Device Manager, the device should be listed under the Universal Serial Bus devices section.  
- Right-click and select Update Driver
- Choose Browse my computer for drivers
- Click Let me pick from a list of available drivers
- Select Tactrix Openport 2.0 J2534 Vehicle Interface
- Click Next

The driver should now be reverted.  
To switch back to the WinUSB driver, repeat the process to update the driver.  
