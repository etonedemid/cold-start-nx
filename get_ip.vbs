Set WshShell = CreateObject("WScript.Shell")
WshShell.Run "cmd /c ipconfig > Z:\cold-start-nx\my_ip.txt", 0, True
