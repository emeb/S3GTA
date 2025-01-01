# S3GTA Software
This directory contains the ESP IDF source and control files for the S3GTA module

## Prerequisites
You will need ESP IDF V5.2.3 to build this.

## Building
Ensure you've got ESP IDF V5.2.3 installed and properly setup, then do the following:

```
idf.py build
```

Then connect your USB / ESP programming dongle to the S3GTA and run:

```
idf.py flash monitor
```
