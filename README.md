# Hotreloader

This project is couple with my [WebServer](https://github.com/MatteoTamine1709/WebServer) project.
It's a simple hotreloader that upon connection (Using signals) will receive from the Webserver the path of the directory where all the endpoints are stored.
It will then watch for changes in the directory and reload the endpoints (compiling it to a shared library) when a change is detected.
It will then notify the Webserver.

The connection protocol is as follows:
First with open a FIFO file at /tmp/fifo.

Using the `pidof WebServer` command, the Hotreloader will get the pid of the Webserver.
It will then write down to the fifo the pid of the Hotreloader and send a `SIGUSR1` signal to the Webserver.

Upon reception, the Webserver will write down to the fifo the path of the directory where the endpoints are stored.
It will then send a `SIGUSR1` signal to the Hotreloader.

Upon reception, the Hotreloader will start watching the directory and reload the endpoints when a change is detected.
If a file is modified, it will write down to the fifo the path of the file.
It will then send a `SIGUSR2` signal to the Webserver.

On `SIGINT` or `SIGTERM` the Hotreloader will send a `SIGUSR1` signal to the Webserver to notify it's stopping and exit.
