# TinyWeb Browser

A lightweight web browser built with GTK and WebKit for Linux and WSL2.

## Features
- Simple and minimal interface
- Command-line arguments for specifying URLs
- Bookmarks management
- Address bar navigation

## Dependencies
- GTK 3
- WebKit2GTK

## Installing the dependencies
### Debian/Ubuntu
```bash
sudo apt update
sudo apt install build-essential libgtk-3-dev libwebkit2gtk-4.0-dev pkg-config
```
### Fedora/RHEL/CentOS
```bash
sudo dnf install gcc gtk3-devel webkit2gtk3-devel pkg-config
```
### Arch Linux
```bash
sudo pacman -S base-devel gtk3 webkit2gtk pkg-config
```
### OpenSUSE
```bash
sudo zypper install gcc gtk3-devel webkit2gtk-devel pkg-config
```

## Building
```bash
gcc -o tinyweb tinyweb.c $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0)
```

## Usage
From your terminal, run the following commands.

Run tinyweb with the hardcoded homepage:
```bash
./tinyweb
```

Run tinyweb and set the homepage at runtime:
```bash
./tinyweb -h https://example.com
```
Run tinyweb with a specific URL at runtime:
```bash
./tinyweb https://example.com
```

The rest is pretty intuitive UI stuff. Enjoy :)

- Steve
