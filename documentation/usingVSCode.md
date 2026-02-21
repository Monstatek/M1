# Configuring, building, and debugging Using VSCode
## Requirements
- VSCode
- [STM32CubeIDE for Visual Studio Code](https://marketplace.visualstudio.com/items?itemName=stmicroelectronics.stm32-vscode-extension)
- [Cortex-Debug](https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug)
## Procedure
### Install VSCode
1. Goto [VSCode](https://code.visualstudio.com/download) and download the latest version
2. Open the installer and follow the instructions provided by the installer
### Install VSCode Extensions
**NOTE:** If you already have VSCode and have many different plug-ins and extensions installed it is best to install the following extensions in a new profile.  Follow the instructions [here](https://code.visualstudio.com/docs/configure/profiles) to create a new profile in VSCode.
#### STM32CubeIDE
1. In VSCode navigate to the extensions side panel and search for 'STM32CubeIDE' or use the link at the top of these instructions
2. Select 'Install' on 'STM32CubeIDE for Visual Studio Code' by STMicroelectronics
    - if you use the link provided in these instructions, you will be prompted to open VSCode from your web browser
3. The installation will install a series of extensions for building and debugging STMicroelectronics microprocessors.  Several items will come up in the bottom left of the VSCode window that will need to be approved.
4. Let the extensions finish installing before moving on.
#### Cortex-Debug
1. In the extensions side panel search for 'Cortex-Debug' or use the link at the top of these instructions
2. Select 'Install' on the 'Cortex-Debug' plugin by marus25
    - if you use the link provided in these instructions, you will be prompted to open VSCode from your web browser
3. Let the extension finish installing.
### Open the Project
1. In VSCode goto 'File' and select 'Open Folder'
2. Browse to the project folder and select 'Open'
3. You will be asked what configuration of the project you would like in the search bar at the top of the VSCode Window, choose the option that works best for your circumstances
4. CMake will start to configure the project
    - CMake will likely fail to configure the project to begin with, but there should be a message in the bottom right corner of the screen asking if you would like to configure the discovered CMake project(s) as STM32Cube project(s), select 'Yes' on the notification
5. After the project is converted to an STM32Cube project, CMake will be able to configure the project properly with the compilers included with the STM32CubeIDE extension
### Build the project
1. After the project is configured and the build files are written, go to the CMake extension side panel
2. Under 'Project Status' find 'Build'
3. Under 'Build' you will find '[Targets in preset]' place your mouse over those words and to the right there will be a pencil icon that appears, select the icon
4. In the search bar there will be a selection for the build targets, select 'MonstaTek_M1_v0800' or similar depending on the revision of firmware that is available
5. Once the correct target is selected, move your mouse to the 'Build' line and select the icon to the right of the word 'Build', this will start building the project
6. Building will take some time to complete
### Running the project
#### As Debug
1. In VSCode go to the STM32Cube side panel
2. Under 'STM32CUBE RESOURCES' select STLink Drivers, if you are using an STLink debug probe, otherwise ensure that you have the proper drivers installed for your debug probe 
3. Once you are finished installing any drivers you need for your debug probe, go to the 'Run and Debug' side panel
3. At the top of the panel select the type of link you have with the device
    - If you have a different debug probe than an STLink or a J-link you will need to edit the /.vscode/launch.json file in the project to add the specific settings for your debug probe
    - If you are using a J-Link probe ensure the settings in the 'launch.json' file and the file paths are correct for your installation
    - If you are using an STLink probe with these instructions the file paths in the launch.json file should direct towards your user folder on Windows and AppData since that is where the STM32 extension installs the compilers and other tools to.
4. Ensure that your M1 is properly connected to the debug probe:
    - on the M1 device the following GPIO pins have the listed purpose when debugging:
        - GPIO 10 = SWDClk
        - GPIO 11 = SWDIO
        - GPIO 12 = UART 1 TX
        - GPIO 13 = UART 1 RX
        - GND = Ground
5. In the Run and Debug panel select the play icon at the top of the panel
    - If you run into an issue while launching the debugger, follow the necessary steps to determine what went wrong, and repair the issue.
### As Release
1. Ensure the drivers for your specific debug probe are installed and your probe is connected properly
2. Go to the CMake side panel and ensure that the release version of the project is built
3. Go to the .vscode/tasks.json file and ensure the path in the '[Release] ...' task for your probe is correct
4. In the search bar in VSCode type, '>Tasks: Run Task' and press enter
5. A list of tasks to run will show up and you can select the '[Release] ...' task for your debug probe