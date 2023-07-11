#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

bool running = true;
int signalFD;
std::string path;
std::string includePath;
bool connected = false;
pid_t targetPID = -1;
std::unordered_map<int, std::string> wdToPath;
std::unordered_map<int, std::string> folderWdToPath;
int fd = -1;

void watchDir(int fd, std::string path) {
    int wd = inotify_add_watch(fd, path.c_str(), IN_MODIFY);
    if (wd < 0) {
        std::cerr << "Failed to add watch for " << path << std::endl;
        exit(-1);
    }
    folderWdToPath[wd] = path;
    DIR *dir = opendir(path.c_str());
    if (dir == NULL) {
        std::cerr << "Failed to open directory " << path << std::endl;
        exit(-1);
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        bool isCpp =
            std::string(entry->d_name).find(".cpp") != std::string::npos;
        if (entry->d_type == DT_REG && isCpp) {
            int wd = inotify_add_watch(
                fd, (path + "/" + std::string(entry->d_name)).c_str(),
                IN_MODIFY);
            if (wd < 0) {
                std::cerr << "Failed to add watch for " << path << std::endl;
                exit(-1);
            }
            wdToPath[wd] = path + "/" + std::string(entry->d_name);
        }
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") != 0 &&
                strcmp(entry->d_name, "..") != 0) {
                char subpath[PATH_MAX];
                snprintf(subpath, PATH_MAX, "%s/%s", path.c_str(),
                         entry->d_name);
                watchDir(fd, subpath);
            }
        }
    }

    closedir(dir);
}

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
        std::cout << "Open " << path << std::endl;
    }
}

void closeInotify() {
    for (auto &[wd, path] : wdToPath) inotify_rm_watch(fd, wd);
    for (auto &[wd, path] : folderWdToPath) inotify_rm_watch(fd, wd);
    wdToPath.clear();
    folderWdToPath.clear();
    close(fd);
    fd = -1;
}

int connection() {
    FILE *command = popen("pidof WebServer", "r");
    char line[1024];
    pid_t previousPID = targetPID;
    fgets(line, 1024, command);
    targetPID = strtoul(line, NULL, 10);
    pclose(command);
    if (targetPID == previousPID || targetPID == 0) return 0;
    path = "";
    connected = false;
    closeInotify();
    sendMyPID();
    while (path.empty()) {
        std::cout << "Waiting for connection..." << std::endl;
        sleep(1);
    }
    std::cout << "Connected to " << targetPID << std::endl;
    fd = inotify_init();
    watchDir(fd, path.c_str());
    kill(targetPID, SIGUSR1);
    connected = true;
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

    while (connection() == 0) sleep(5);
    std::vector<std::thread> threads;
    std::mutex m;
    std::cout << "Compiling..." << std::endl;
    for (auto &a : wdToPath) {
        threads.emplace_back([&]() {
            const auto &[_, p] = a;
            std::cout << "Compiling " << p << std::endl;
            std::string outputFile = p.substr(0, p.find_last_of(".")) + ".so";
            char cwd[1024];
            std::string command = "g++ " + p + " -o " + outputFile +
                                  " -O3 -lxml2 -I/usr/include/libxml2 "
                                  "-fno-gnu-unique -shared -fPIC "
                                  "-std=c++20 -I" +
                                  path + "/..";
            system(command.c_str());
            m.lock();
            write(signalFD, p.c_str(), p.size());
            kill(targetPID, SIGUSR2);
            m.unlock();
        });
    }
    for (auto &t : threads) t.join();
    std::cout << "Compiled" << std::endl;

    std::thread([&]() {
        while (running) {
            connection();
            sleep(5);
        }
    }).detach();
    char buffer[1024];
    while (running) {
        std::cout << "Waiting for changes..." << std::endl;
        int n_read = read(fd, buffer, 1024);
        bool wasConnected = connected;
        while (!connected) sleep(5);
        if (!wasConnected) continue;
        for (char *p = buffer; p < buffer + n_read;) {
            struct inotify_event *event = (struct inotify_event *)p;
            std::string fileName = wdToPath[event->wd];
            bool isCpp = fileName.find(".cpp") != std::string::npos;
            if (event->mask & IN_MODIFY && !(event->mask & IN_ISDIR) && isCpp) {
                std::cout << "File " << fileName << " was modified."
                          << std::endl;
                std::string outputFile =
                    fileName.substr(0, fileName.find_last_of(".")) + ".so";
                char cwd[1024];
                std::string command = "g++ " + fileName + " -o " + outputFile +
                                      " -O3 -lxml2 -I/usr/include/libxml2 "
                                      "-fno-gnu-unique -shared "
                                      "-fPIC -std=c++20 -I" +
                                      path + "/..";
                system(command.c_str());
                write(signalFD, fileName.c_str(), fileName.size());
                kill(targetPID, SIGUSR2);
            } else if (event->mask & IN_MODIFY &&
                       folderWdToPath.find(event->wd) != folderWdToPath.end()) {
                std::cout << "Directory " << folderWdToPath[event->wd]
                          << " was modified." << std::endl;
                closeInotify();
                fd = inotify_init();
                watchDir(fd, path);
            }
            p += sizeof(struct inotify_event) + event->len;
        }
    }
    closeInotify();
    return 0;
}
