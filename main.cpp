#include <iostream>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <thread>
#include <sys/inotify.h>

bool running = true;
int signalFD;
std::string path;
bool connected = false;
pid_t targetPID = -1;

int openPipe(const std::string &path) {
    if (mkfifo(path.c_str(), 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        std::cerr << "Failed to create named pipe." << std::endl;
        return 1;
    }
    int fd = open(path.c_str(), O_RDWR);
    if (fd == -1) {
        std::cerr << "Failed to open named pipe." << std::endl;
        return 1;
    }
    return fd;
}

void sendMyPID() {
    pid_t pid = getpid();
    write(signalFD, &pid, sizeof(pid_t));
    kill(targetPID, SIGUSR1);
}

void signalHandler(int signal) {
    if (signal == SIGINT) {
        std::cout << "Stopping" << std::endl; 
        close(signalFD);
        kill(targetPID, SIGUSR1);
        running = false;
        return exit(0);
    }
    if (signal == SIGUSR1) {
        char buffer[1024];
        size_t n_read = read(signalFD, buffer, 1024);
        if (n_read == -1) {
            std::cerr << "Failed to read from pipe." << std::endl;
            return;
        }
        buffer[n_read] = '\0';
        path = buffer;
        path += "/";
        std::cout << "Open " << path << std::endl;
        connected = true;
    }

}

int connection() {
    FILE *command = popen("pidof WebServer","r");
    char line[1024];
    pid_t previousPID = targetPID;
    fgets(line, 1024, command);
    targetPID = strtoul(line, NULL, 10);
    pclose(command);
    if (targetPID == previousPID || targetPID == 0)
        return 0;
    connected = false;
    sendMyPID();
    while (!connected) {
        std::cout << "Waiting for connection..." << std::endl;
        sleep(1);
    }
    std::cout << "Connected to " << targetPID << std::endl;
    kill(targetPID, SIGUSR1);
    return 1;
}


int main(int argc, char **argv) {
    struct sigaction sa = {};
    sa.sa_handler = signalHandler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGUSR1, &sa, nullptr);
    sigaction(SIGUSR2, &sa, nullptr);

    const std::string fifoPipePath = "/tmp/fifo";
    signalFD = openPipe(fifoPipePath);

    while (connection() == 0)
        sleep(5);
    int fd = inotify_init();
    int wd = inotify_add_watch(fd, path.c_str(), IN_MODIFY);
    if (wd == -1) {
        std::cerr << "Failed to add watch." << std::endl;
        return 1;
    }
    char buffer[1024];
    while (running) {
        int n_read = read(fd, buffer, 1024);
        if (n_read == -1) {
            std::cerr << "Failed to read from inotify." << std::endl;
            return 1;
        }
        for (char *p = buffer; p < buffer + n_read;) {
            struct inotify_event *event = (struct inotify_event *)p;
            std::string fileName = event->name;
            bool isCpp = fileName.find(".cpp") != std::string::npos;
            if (event->mask & IN_MODIFY && isCpp) {
                std::cout << "File " << event->name << " was modified." << std::endl;
                // std::cout << "Compiling " << event->name << std::endl;
                std::string outputFile = fileName.substr(0, fileName.find_last_of(".")) + ".so";
                char cwd[1024];
                std::string includePath = "./";
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    includePath = cwd;
                    includePath += "/";
                }
                std::string command = "g++ -shared -fPIC " + fileName + " -o " + outputFile + " -std=c++20 -I" + includePath;
                system(command.c_str());
                kill(targetPID, SIGUSR2);
            }
            p += sizeof(struct inotify_event) + event->len;
        }
    }
    inotify_rm_watch(fd, wd);
    return 0;
}
