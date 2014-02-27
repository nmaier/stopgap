@echo off
"C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\signtool.exe" sign /a /tr http://www.startssl.com/timestamp "%1"