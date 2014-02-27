@echo off
cd "%1" && c:\git\bin\git.exe archive --format zip -o "%2" -v -9 HEAD