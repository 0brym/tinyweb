# TinyWeb Browser

A lightweight web browser built with GTK and WebKit.

## Features
- Simple and minimal interface
- Command-line arguments for specifying URLs
- Bookmarks management
- Address bar navigation

## Dependencies
- GTK 3
- WebKit2GTK

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

The rest is pretty intuitive. Enjoy :)

- Steve
