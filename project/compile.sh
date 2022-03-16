#!/bin/bash
gcc -pthread server.c -o server -lsqlite3
gcc -Wno-format clientI.c -o clientI -Wno-deprecated-declarations -Wno-format-security -lm `pkg-config --cflags --libs gtk+-3.0` -export-dynamic
