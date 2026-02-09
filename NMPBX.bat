@echo off

set str=%1
set str=%str:tel:=%
set str=%str:callto:=%
set "str=%str:"=%"

start /min "" "C:\Users\sam.driver\Code\linphone-desktop\build\OUTPUT\bin\NMPBX.exe" "call sip-address=%str%"
