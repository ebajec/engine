@echo off

g++ -o test.exe  lib/main.cpp

start cmd.exe /k "test.exe & echo."
